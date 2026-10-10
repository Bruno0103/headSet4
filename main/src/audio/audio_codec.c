/*
 * WM8960 - port do codec1_setup() de Super_Headphones_BT_and_MIC_V2.0.ino.
 *
 * Regra deste arquivo: todo registrador usado e escrito POR INTEIRO na
 * inicializacao (nao dependo dos valores de reset). Depois disso, so uso
 * wm8960_update() para mexer em campos isolados.
 *
 * NAO TESTADO EM HARDWARE. Confira as palavras de registrador com o datasheet.
 */
#include "audio_codec.h"

#include "driver/i2c_master.h"
#include "board_i2c.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board_config.h"
#include "wm8960.h"

static const char *TAG = "audio_codec";

/* ---- campos de registradores (nomes curtos, so para leitura) ---- */
#define PWR1_VMID_2X50K   (1u << 7)   /* VMIDSEL = 01: playback/gravacao */
#define PWR1_VMID_2X250K  (2u << 7)   /* VMIDSEL = 10: standby de baixissimo consumo (mantem o VMID carregado) */
#define PWR1_VREF         (1u << 6)
#define PWR1_AINL         (1u << 5)
#define PWR1_AINR         (1u << 4)
#define PWR1_ADCL         (1u << 3)
#define PWR1_ADCR         (1u << 2)

#define PWR2_DACL         (1u << 8)
#define PWR2_DACR         (1u << 7)
#define PWR2_LOUT1        (1u << 6)
#define PWR2_ROUT1        (1u << 5)
#define PWR2_OUT3         (1u << 1)   /* VMID como "terra" do headphone (capless) */
#define PWR2_PLL_EN       (1u << 0)

#define PWR3_LMIC         (1u << 5)
#define PWR3_RMIC         (1u << 4)
#define PWR3_LOMIX        (1u << 3)
#define PWR3_ROMIX        (1u << 2)

#define PATH_MN1          (1u << 8)   /* INPUT1 -> entrada inversora do PGA */
#define PATH_MIC2B        (1u << 3)   /* PGA -> boost mixer */
#define VOL_UPDATE        (1u << 8)   /* IPVU / OUT1VU / ADCVU: grava o ganho */
#define VOL_ZEROCROSS     (1u << 7)   /* troca de volume so no cruzamento por zero */
#define MIX_DAC2OUT       (1u << 8)   /* DAC -> mixer de saida */
#define MIX_IN3OUT        (1u << 7)   /* INPUT3 -> mixer de saida (0 dB) */
#define BYPASS_B2O        (1u << 7)   /* boost mixer -> mixer de saida (mic ambiente analogico) */

#define CLK1_SYSCLKDIV2   (2u << 1)
#define CLK1_CLKSEL_PLL   (1u << 0)
#define PLL1_SDM_FRAC     (1u << 5)
#define PLL1_PRESCALE_DIV2 (1u << 4)

#define DACCTL1_DACMU     (1u << 3)
#define DACCTL1_DEEMPH    (3u << 1)
#define DACCTL1_ADCHPD    (1u << 0)   /* 1 = HPF do ADC desligado */

#define HP_VOL_0DB        0x79        /* LOUT1VOL/ROUT1VOL: 0x79 = 0 dB, 1 dB/passo; <= 0x2F = mudo */
#define HP_RANGE_DB       60          /* AVRCP 1 -> -60 dB, AVRCP 127 -> 0 dB */

/* ---- tabela de taxas ----
 * Os valores do PLL assumem MCLK = 24 MHz (12 MHz apos o prescale /2):
 *   44.1k: 12 MHz * 7.5264 / 8 = 11.2896 MHz = 256*44100  (N=7, K=0x86C226, igual ao sketch)
 *   48k  : 12 MHz * 8.192  / 8 = 12.288  MHz = 256*48000  (N=8, K=0x3126E8)
 * 32k/16k/8k reaproveitam o PLL de 48k e usam o divisor de ADC/DAC (/1.5, /3, /6). */
typedef struct {
    uint32_t fs;
    uint8_t  pll_n;
    uint32_t pll_k;
    uint8_t  div;       /* ADCDIV = DACDIV: 0=/1, 1=/1.5, 3=/3, 6=/6 */
} rate_cfg_t;

static const rate_cfg_t RATES[] = {
    { 44100, 7, 0x86C226, 0 },
    { 48000, 8, 0x3126E8, 0 },
    { 32000, 8, 0x3126E8, 1 },
    { 16000, 8, 0x3126E8, 3 },
    {  8000, 8, 0x3126E8, 6 },
};

static uint8_t  s_pll_n;
static uint32_t s_pll_k;
static bool     s_powered;

/* ---- presets de filtros ---- */
const audio_filters_t AUDIO_FILTERS_MUSIC = {
    .adc_hpf = true,                      /* o resto desligado: igual ao sketch */
};

const audio_filters_t AUDIO_FILTERS_CALL = {   /* ponto de partida: ajuste ouvindo */
    .adc_hpf = true,
    .alc = true, .alc_limiter = false,
    .alc_target = 11,                     /* -6 dBFS */
    .alc_max_gain = 4,                    /* +12 dB */
    .alc_min_gain = 0,
    .alc_hold = 0, .alc_decay = 3, .alc_attack = 2,
    .noise_gate = true, .noise_gate_threshold = 11,   /* -60 dBFS */
};

#define W(reg, val)  ESP_RETURN_ON_ERROR(wm8960_write((reg), (val)), TAG, #reg)

/* ------------------------------------------------------------------ */

/* Liga os blocos de entrada/saida na ordem do init: referencias -> entradas/ADC -> mixers -> DAC/headphone.
 * O DAC fica mudo (R5 nao e tocado aqui); quem toca tira o mudo depois do clock estabilizar. */
static esp_err_t power_blocks_on(void)
{
    W(WM8960_R_PWR1, PWR1_VMID_2X50K | PWR1_VREF | PWR1_AINL | PWR1_AINR | PWR1_ADCL | PWR1_ADCR);
    W(WM8960_R_PWR3, PWR3_LMIC | PWR3_RMIC | PWR3_LOMIX | PWR3_ROMIX);
    W(WM8960_R_PWR2, PWR2_DACL | PWR2_DACR | PWR2_LOUT1 | PWR2_ROUT1 | PWR2_OUT3);
    return ESP_OK;
}

esp_err_t audio_codec_power_up(void)
{
    if (s_powered) return ESP_OK;
    ESP_RETURN_ON_ERROR(wm8960_update(WM8960_R_DACCTL1, DACCTL1_DACMU, DACCTL1_DACMU), TAG, "mute");
    ESP_RETURN_ON_ERROR(power_blocks_on(), TAG, "power up");
    vTaskDelay(pdMS_TO_TICKS(10));            /* bias das entradas/saidas assenta (VMID ja esta carregado) */
    s_powered = true;
    ESP_LOGI(TAG, "codec ligado");
    return ESP_OK;
}

/* Modo "standby" do datasheet (R25 VMIDSEL=10 + VREF): saidas, DAC, ADC, mixers e PLL desligados;
 * so o VMID e mantido por 2x250k, entao o proximo power_up nao tem rampa nem estalo. */
esp_err_t audio_codec_power_down(void)
{
    if (!s_powered) return ESP_OK;
    ESP_RETURN_ON_ERROR(wm8960_update(WM8960_R_DACCTL1, DACCTL1_DACMU, DACCTL1_DACMU), TAG, "mute");
    W(WM8960_R_PWR2, 0);                      /* DAC, LOUT1/ROUT1, OUT3 e PLL */
    W(WM8960_R_PWR3, 0);                      /* microfones e mixers de saida */
    W(WM8960_R_PWR1, PWR1_VMID_2X250K | PWR1_VREF);   /* entradas e ADC off */
    s_pll_n = 0;                              /* o PLL caiu: set_sample_rate precisa reprogramar */
    s_pll_k = 0;
    s_powered = false;
    ESP_LOGI(TAG, "codec desligado (standby)");
    return ESP_OK;
}

/* ------------------------------------------------------------------ */

esp_err_t audio_codec_set_sample_rate(uint32_t hz)
{
    const rate_cfg_t *r = NULL;
    for (size_t i = 0; i < sizeof RATES / sizeof RATES[0]; i++) {
        if (RATES[i].fs == hz) r = &RATES[i];
    }
    ESP_RETURN_ON_FALSE(r, ESP_ERR_NOT_SUPPORTED, TAG, "taxa %lu Hz nao suportada", (unsigned long)hz);

    if (r->pll_n != s_pll_n || r->pll_k != s_pll_k) {
        ESP_RETURN_ON_ERROR(wm8960_update(WM8960_R_PWR2, PWR2_PLL_EN, 0), TAG, "PLL off");
        W(WM8960_R_PLL1, PLL1_SDM_FRAC | PLL1_PRESCALE_DIV2 | r->pll_n);
        W(WM8960_R_PLL2, (r->pll_k >> 16) & 0xFF);
        W(WM8960_R_PLL3, (r->pll_k >> 8) & 0xFF);
        W(WM8960_R_PLL4, r->pll_k & 0xFF);
        ESP_RETURN_ON_ERROR(wm8960_update(WM8960_R_PWR2, PWR2_PLL_EN, PWR2_PLL_EN), TAG, "PLL on");
        vTaskDelay(pdMS_TO_TICKS(20));        /* tempo de lock do PLL */
        s_pll_n = r->pll_n;
        s_pll_k = r->pll_k;
    }
    W(WM8960_R_CLOCK1, (r->div << 6) | (r->div << 3) | CLK1_SYSCLKDIV2 | CLK1_CLKSEL_PLL);
    return ESP_OK;
}

esp_err_t audio_codec_set_volume(uint8_t v)
{
    if (v > 127) v = 127;
    uint16_t hp = (v == 0) ? 0 : (uint16_t)(HP_VOL_0DB - ((127 - v) * HP_RANGE_DB) / 127);
    uint16_t word = VOL_UPDATE | VOL_ZEROCROSS | hp;
    W(WM8960_R_LOUT1, word);
    W(WM8960_R_ROUT1, word);
    return ESP_OK;
}

esp_err_t audio_codec_mute(bool mute)
{
    return wm8960_update(WM8960_R_DACCTL1, DACCTL1_DACMU, mute ? DACCTL1_DACMU : 0);
}

esp_err_t audio_codec_set_ambient_gain(uint8_t pga)
{
    if (pga > 63) pga = 63;
    W(WM8960_R_LINVOL, VOL_UPDATE | pga);     /* LINMUTE = 0 */
    W(WM8960_R_RINVOL, VOL_UPDATE | pga);
    return ESP_OK;
}

esp_err_t audio_codec_apply_filters(const audio_filters_t *f)
{
    /* 3D (R16): [4:1] profundidade, [0] enable */
    W(WM8960_R_3D, ((f->enhance3d_depth & 0xF) << 1) | (f->enhance3d ? 1 : 0));

    /* R5: de-enfase [2:1] e HPF do ADC [0] (o bit DACMU e preservado) */
    ESP_RETURN_ON_ERROR(wm8960_update(WM8960_R_DACCTL1, DACCTL1_DEEMPH | DACCTL1_ADCHPD,
                        ((uint16_t)f->deemphasis << 1) | (f->adc_hpf ? 0 : DACCTL1_ADCHPD)),
                        TAG, "DACCTL1");

    /* ALC (R17-R19). R18 mantem o bit 8 no valor de reset (0x100). */
    uint16_t alcsel = f->alc ? 3 : 0;         /* 3 = estereo */
    W(WM8960_R_ALC1, (alcsel << 7) | ((f->alc_max_gain & 7) << 4) | (f->alc_target & 0xF));
    W(WM8960_R_ALC2, 0x100 | ((f->alc_min_gain & 7) << 4) | (f->alc_hold & 0xF));
    W(WM8960_R_ALC3, ((f->alc_limiter ? 1 : 0) << 8) | ((f->alc_decay & 0xF) << 4) | (f->alc_attack & 0xF));

    /* Noise gate (R20): [7:3] limiar, [0] enable */
    W(WM8960_R_NOISEGATE, ((f->noise_gate_threshold & 0x1F) << 3) | (f->noise_gate ? 1 : 0));
    return ESP_OK;
}

/* ------------------------------------------------------------------ */

esp_err_t audio_codec_init(void)
{
    /* O barramento I2C e compartilhado com o APDS-9930 (board_i2c). */
    ESP_RETURN_ON_ERROR(board_i2c_init(), TAG, "i2c bus");
    i2c_master_bus_handle_t bus = board_i2c_get_bus();
    ESP_RETURN_ON_ERROR(wm8960_attach(bus), TAG, "attach");
    ESP_RETURN_ON_ERROR(wm8960_reset(), TAG, "reset");

    /* Referencias (enableVREF / enableVMID) e rampa do VMID, para evitar "pop" */
    W(WM8960_R_PWR1, PWR1_VMID_2X50K | PWR1_VREF);
    vTaskDelay(pdMS_TO_TICKS(100));

    /* Entrada: INPUT1 single-ended -> PGA -> boost mixer (boost 0 dB, nao-inversora em VMID) */
    W(WM8960_R_PWR1, PWR1_VMID_2X50K | PWR1_VREF | PWR1_AINL | PWR1_AINR | PWR1_ADCL | PWR1_ADCR);
    W(WM8960_R_PWR3, PWR3_LMIC | PWR3_RMIC | PWR3_LOMIX | PWR3_ROMIX);
    W(WM8960_R_LINPATH, PATH_MN1 | PATH_MIC2B);
    W(WM8960_R_RINPATH, PATH_MN1 | PATH_MIC2B);
    ESP_RETURN_ON_ERROR(audio_codec_set_ambient_gain(23), TAG, "gain");     /* 0 dB */

    /* Mixers de saida: Apenas DAC para fones de ouvido (sem retorno de mic analogico para fones) */
    W(WM8960_R_LOUTMIX, MIX_DAC2OUT);
    W(WM8960_R_ROUTMIX, MIX_DAC2OUT);
    W(WM8960_R_BYPASS1, 0);                   /* Desliga bypass analogico de mic para fone L */
    W(WM8960_R_BYPASS2, 0);                   /* Desliga bypass analogico de mic para fone R */

    /* Interface: I2S, 16 bits, codec = peripheral (o ESP32 gera BCLK/LRCLK) */
    W(WM8960_R_IFACE1, 0x002);
    W(WM8960_R_CLOCK2, 0x1C4);                /* DCLKDIV /16, BCLKDIV 4 (como no sketch) */

    /* DAC, headphone e OUT3 (PLL e ligado em set_sample_rate) */
    W(WM8960_R_DACCTL1, DACCTL1_DACMU);       /* comeca mudo; audio_io tira o mudo */
    W(WM8960_R_PWR2, PWR2_DACL | PWR2_DACR | PWR2_LOUT1 | PWR2_ROUT1 | PWR2_OUT3);
    s_powered = true;

    ESP_RETURN_ON_ERROR(audio_codec_set_sample_rate(44100), TAG, "clock");
    ESP_RETURN_ON_ERROR(audio_codec_set_volume(CODEC_DEFAULT_VOLUME), TAG, "volume");
    ESP_RETURN_ON_ERROR(audio_codec_apply_filters(&AUDIO_FILTERS_MUSIC), TAG, "filtros");

    /* Configurado; fica desligado ate o audio_io iniciar musica ou chamada */
    ESP_RETURN_ON_ERROR(audio_codec_power_down(), TAG, "power down");
    ESP_LOGI(TAG, "WM8960 configurado (44.1 kHz, headphone + mic ambiente); aguardando audio em standby");
    return ESP_OK;
}

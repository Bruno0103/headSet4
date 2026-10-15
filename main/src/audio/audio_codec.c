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
#define PWR1_MICB         (1u << 1)   /* MICB = bit 1: alimentacao de polarizacao MICBIAS */

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
#define PATH_MICBOOST_20DB (2u << 4)  /* Boost de +20 dB no mixer de entrada (ideal para eletreto) */
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

#define DACCTL2_DACSMM    (1u << 3)   /* 1 = DAC Soft Mute Mode (rampa gradual suave anti-pop em hardware) */
#define DACCTL2_DACMR     (1u << 2)   /* 0 = rampa lenta (1024 amostras ~23ms); 1 = rampa rapida */
#define DACCTL2_DACSLOPE  (1u << 1)   /* 0 = filtro FIR padrao */

#define APOP1_POBCTRL     (1u << 7)   /* 1 = Controle de bias anti-pop ativo */
#define APOP1_SOFT_ST     (1u << 2)   /* 1 = Soft start de VMID ativo */
#define APOP1_BUFDCOPEN   (1u << 4)   /* 1 = Buffers DC de saída suaves */

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
static uint8_t  s_current_vol_avrcp = CODEC_DEFAULT_VOLUME;
static bool     s_analog_muted = true;

/* ---- presets de filtros (tratamentos desativados para audio puro do microfone) ---- */
const audio_filters_t AUDIO_FILTERS_MUSIC = {
    .adc_hpf = false,                     /* ADC HPF desligado */
    .alc     = false,                     /* ALC desligado */
    .noise_gate = false,                  /* Noise Gate desligado */
};

const audio_filters_t AUDIO_FILTERS_CALL = {
    .adc_hpf    = false,                  /* Sem passa-altas: captura resposta plana (flat) */
    .alc        = false,                  /* Sem ALC: sem compressao nem ganho variavel */
    .noise_gate = false,                  /* Sem Noise Gate: sem cortes dinamicos */
};


#define W(reg, val)  ESP_RETURN_ON_ERROR(wm8960_write((reg), (val)), TAG, #reg)

/* ------------------------------------------------------------------ */

/* Liga os blocos de saida na ordem do init: referencias -> mixers -> DAC/headphone.
 * Os blocos de captura/microfone (AINL/R, ADCL/R, LMIC/RMIC e MICBIAS) permanecem
 * desligados por padrao para economia de energia e privacidade, sendo ligados sob demanda
 * por audio_codec_set_mic_power() somente durante chamadas ou gravacao de voz. */
static esp_err_t power_blocks_on(void)
{
    W(WM8960_R_PWR1, PWR1_VMID_2X50K | PWR1_VREF);
    W(WM8960_R_PWR3, PWR3_LOMIX | PWR3_ROMIX);
    /* No modo Capless (OUT3 como pseudo-terra de 1.65V), os buffers LOUT1, ROUT1 e OUT3
     * permanecem energizados para manter a DDP em exatamente 0.000 V */
    W(WM8960_R_PWR2, PWR2_DACL | PWR2_DACR | PWR2_LOUT1 | PWR2_ROUT1 | PWR2_OUT3);
    return ESP_OK;
}


esp_err_t audio_codec_set_analog_mute(bool mute)
{
    s_analog_muted = mute;
    uint16_t hp;
    if (mute || s_current_vol_avrcp == 0) {
        /* No hardware WM8960, volumes de 0x00 a 0x2F aplicam mute analógico real nas saídas LOUT1/ROUT1 */
        hp = 0;
    } else {
        hp = (uint16_t)(HP_VOL_0DB - ((127 - s_current_vol_avrcp) * HP_RANGE_DB) / 127);
    }
    uint16_t word = VOL_UPDATE | VOL_ZEROCROSS | hp;
    W(WM8960_R_LOUT1, word);
    W(WM8960_R_ROUT1, word);
    return ESP_OK;
}

esp_err_t audio_codec_power_up(void)
{
    if (s_powered) return ESP_OK;
    /* Soft mute digital ativado para que a saída inicie sem transitórios de clock */
    ESP_RETURN_ON_ERROR(audio_codec_mute(true), TAG, "mute");
    ESP_RETURN_ON_ERROR(power_blocks_on(), TAG, "power up");
    vTaskDelay(pdMS_TO_TICKS(15));            /* bias analógico estabiliza com o DAC mutado suavemente */
    s_powered = true;
    ESP_LOGI(TAG, "codec ligado");
    return ESP_OK;
}

/* Modo "standby" seguro e anti-pop (Capless Mode):
 * Mantém OUT3 (pseudo-terra do fone a 1.65V), LOUT1/ROUT1 (saídas de fone) e os mixers
 * estavelmente polarizados a 1.65V com VMID 2x50k. Desliga apenas DACs e PLL.
 * As saídas LOUT1/ROUT1 são colocadas em mute analógico real (0x00) com zero-cross,
 * garantindo DDP diferencial rigorosamente nula (0.000 V) sem estalos. */
esp_err_t audio_codec_power_down(void)
{
    if (!s_powered) return ESP_OK;
    /* 1. Mute analógico suave nas saídas de fone */
    audio_codec_set_analog_mute(true);
    /* 2. Soft Mute digital no DAC com rampa gradual */
    audio_codec_mute(true);
    vTaskDelay(pdMS_TO_TICKS(10));
    /* Desliga DACs e PLL, mas PRESERVA LOUT1, ROUT1, OUT3 e mixers energizados (Capless Anti-Pop) */
    W(WM8960_R_PWR2, PWR2_LOUT1 | PWR2_ROUT1 | PWR2_OUT3);
    W(WM8960_R_PWR1, PWR1_VMID_2X50K | PWR1_VREF);   /* VMID e VREF mantidos estáveis */
    s_pll_n = 0;                                      /* o PLL caiu: set_sample_rate precisa reprogramar */
    s_pll_k = 0;
    s_powered = false;
    ESP_LOGI(TAG, "codec em standby suave (anti-pop capless ativo)");
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
        /* Mover CLKSEL para MCLK temporariamente */
        ESP_RETURN_ON_ERROR(wm8960_update(WM8960_R_CLOCK1, CLK1_CLKSEL_PLL, 0), TAG, "CLKSEL = MCLK");
        ESP_RETURN_ON_ERROR(wm8960_update(WM8960_R_PWR2, PWR2_PLL_EN, 0), TAG, "PLL off");
        W(WM8960_R_PLL1, PLL1_SDM_FRAC | PLL1_PRESCALE_DIV2 | r->pll_n);
        W(WM8960_R_PLL2, (r->pll_k >> 16) & 0xFF);
        W(WM8960_R_PLL3, (r->pll_k >> 8) & 0xFF);
        W(WM8960_R_PLL4, r->pll_k & 0xFF);
        ESP_RETURN_ON_ERROR(wm8960_update(WM8960_R_PWR2, PWR2_PLL_EN, PWR2_PLL_EN), TAG, "PLL on");
        vTaskDelay(pdMS_TO_TICKS(10));        /* tempo de lock do PLL (F-10 req) */
        s_pll_n = r->pll_n;
        s_pll_k = r->pll_k;
    }
    W(WM8960_R_CLOCK1, (r->div << 6) | (r->div << 3) | CLK1_SYSCLKDIV2 | CLK1_CLKSEL_PLL);
    return ESP_OK;
}

esp_err_t audio_codec_set_volume(uint8_t v)
{
    if (v > 127) v = 127;
    s_current_vol_avrcp = v;
    if (!s_analog_muted) {
        return audio_codec_set_analog_mute(false);
    }
    return ESP_OK;
}

esp_err_t audio_codec_mute(bool mute)
{
    /* DACMU em R5 controla o mute digital sigma-delta.
     * Como DACSMM=1 é mantido permanentemente em R6, o hardware interpola rampa gradual suave */
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

esp_err_t audio_codec_set_mic_power(bool enable)
{
    if (!s_powered) return ESP_OK;

    if (enable) {
        /* Liga alimentacao MICBIAS (bit 1), amplificadores de entrada (AINL/AINR) e ADCs (ADCL/ADCR) */
        ESP_RETURN_ON_ERROR(wm8960_update(WM8960_R_PWR1,
                            PWR1_MICB | PWR1_AINL | PWR1_AINR | PWR1_ADCL | PWR1_ADCR,
                            PWR1_MICB | PWR1_AINL | PWR1_AINR | PWR1_ADCL | PWR1_ADCR),
                            TAG, "pwr1 mic on");

        /* Liga os PGAs de entrada de microfone (LMIC / RMIC) */
        ESP_RETURN_ON_ERROR(wm8960_update(WM8960_R_PWR3,
                            PWR3_LMIC | PWR3_RMIC,
                            PWR3_LMIC | PWR3_RMIC),
                            TAG, "pwr3 mic on");

        /* Restaura o ganho de 0 dB nas entradas do microfone */
        audio_codec_set_ambient_gain(23);
        ESP_LOGI(TAG, "Microfone ATIVADO sob demanda (MICBIAS, entradas analogicas e ADC ligados)");
    } else {
        /* Desliga MICBIAS, entradas analogicas e ADCs para economizar energia e garantir privacidade */
        ESP_RETURN_ON_ERROR(wm8960_update(WM8960_R_PWR1,
                            PWR1_MICB | PWR1_AINL | PWR1_AINR | PWR1_ADCL | PWR1_ADCR,
                            0),
                            TAG, "pwr1 mic off");

        /* Desliga os PGAs de entrada de microfone */
        ESP_RETURN_ON_ERROR(wm8960_update(WM8960_R_PWR3,
                            PWR3_LMIC | PWR3_RMIC,
                            0),
                            TAG, "pwr3 mic off");

        ESP_LOGI(TAG, "Microfone DESATIVADO (MICBIAS e ADC desligados para economia e privacidade)");
    }
    return ESP_OK;
}

esp_err_t audio_codec_set_sidetone(bool enable)
{
    ESP_RETURN_ON_ERROR(wm8960_update(WM8960_R_BYPASS1, BYPASS_B2O, enable ? BYPASS_B2O : 0), TAG, "BYPASS1");
    ESP_RETURN_ON_ERROR(wm8960_update(WM8960_R_BYPASS2, BYPASS_B2O, enable ? BYPASS_B2O : 0), TAG, "BYPASS2");
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

    /* Registradores que sofrem update() isolado precisam de uma escrita inteira primeiro */
    W(WM8960_R_CLOCK1, 0x000);

    /* Habilita R6 (DACCTL2) com DACSMM = 1 (Soft Mute Mode: rampa gradual suave anti-pop em hardware) */
    W(WM8960_R_DACCTL2, DACCTL2_DACSMM);

    /* Anti-pop 1 (R28): Ativa rampa de soft start VMID e controle de bias anti-pop */
    W(WM8960_R_APOP1, APOP1_POBCTRL | APOP1_SOFT_ST | APOP1_BUFDCOPEN);
    /* Anti-pop 2 (R29): Sem descarga forçada de capacitores (modo Capless OUT3) */
    W(WM8960_R_APOP2, 0x000);

    /* Referencias (enableVREF / enableVMID) e rampa do VMID, para evitar "pop" */
    W(WM8960_R_PWR1, PWR1_VMID_2X50K | PWR1_VREF);
    vTaskDelay(pdMS_TO_TICKS(100));

    /* Entrada: Configura roteamento (INPUT1 single-ended -> PGA -> boost mixer, boost +20 dB, VMID).
     * Nota: Os blocos de potencia do microfone (MICB, AIN, ADC, LMIC) permanecem DESLIGADOS
     * aqui e serao ativados somente durante chamadas HFP / gravacao de voz. */
    W(WM8960_R_PWR3, PWR3_LOMIX | PWR3_ROMIX);
    W(WM8960_R_LINPATH, PATH_MN1 | PATH_MICBOOST_20DB | PATH_MIC2B);
    W(WM8960_R_RINPATH, PATH_MN1 | PATH_MICBOOST_20DB | PATH_MIC2B);
    W(WM8960_R_ADDCTL1, (1u << 2));           /* DATSEL = 01: replica ADC L (LINPUT1) para L e R no barramento I2S */
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
    W(WM8960_R_DACCTL1, DACCTL1_DACMU);       /* comeca mudo digitalmente */
    W(WM8960_R_PWR2, PWR2_DACL | PWR2_DACR | PWR2_LOUT1 | PWR2_ROUT1 | PWR2_OUT3);
    s_powered = true;

    ESP_RETURN_ON_ERROR(audio_codec_set_sample_rate(44100), TAG, "clock");
    ESP_RETURN_ON_ERROR(audio_codec_set_volume(CODEC_DEFAULT_VOLUME), TAG, "volume");
    /* Garante fones em mute analógico inicialmente para partida sem cliques */
    ESP_RETURN_ON_ERROR(audio_codec_set_analog_mute(true), TAG, "analog mute");
    ESP_RETURN_ON_ERROR(audio_codec_apply_filters(&AUDIO_FILTERS_MUSIC), TAG, "filtros");

    /* Configurado; fica desligado ate o audio_io iniciar musica ou chamada */
    ESP_RETURN_ON_ERROR(audio_codec_power_down(), TAG, "power down");
    ESP_LOGI(TAG, "WM8960 configurado (44.1 kHz, headphone + mic ambiente); aguardando audio em standby");
    return ESP_OK;
}

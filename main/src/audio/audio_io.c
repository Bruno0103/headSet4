#include "audio_io.h"

#include <math.h>
#include <string.h>

#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "audio_codec.h"
#include "board_config.h"
#include "eq.h"
#include "voice_nr.h"

#define MUSIC_RB_SIZE   (32 * 1024)    /* ~185 ms a 44.1 kHz; fica em PSRAM quando disponivel */
/* Limiares relativos ao tamanho real do buffer (pode cair para metade sem PSRAM) */
#define MUSIC_PREFILL   (s_music_size * 3 / 8)   /* acumula isto antes de comecar a tocar */
#define DRIFT_HIGH      (s_music_size * 3 / 4)
#define DRIFT_LOW       (s_music_size / 8)
#define MUSIC_CHUNK     1024
#define CALL_RB_SIZE    (4 * 1024)
#define CALL_FRAMES     128            /* 16 ms a 8 kHz, 8 ms a 16 kHz */

static const char *TAG = "audio_io";

static i2s_chan_handle_t   s_tx, s_rx;
static SemaphoreHandle_t   s_lock;              /* protege I2S + troca de modo */
static RingbufHandle_t     s_music_rb, s_dl_rb, s_ul_rb;
static volatile audio_io_mode_t s_mode = AUDIO_IO_IDLE;
static volatile bool       s_reconfig_req;
static size_t              s_music_size;
static bool                s_prebuffering;
static int16_t             s_chunk[MUSIC_CHUNK / 2];
static uint32_t            s_call_limit_bytes;
static uint32_t            s_rate;

#define SFX_IDLE_RATE   44100
#define SFX_AMPLITUDE   9000.0f
#define SFX_RAMP_MS     6              /* sobe/desce suave para nao estalar */

static const audio_tone_t *s_tone_seq;
static size_t              s_tone_len, s_tone_idx;
static uint32_t            s_tone_pos;     /* amostras ja geradas na nota atual */
static float               s_tone_phase;
static volatile bool       s_tone_active;
static bool                s_tone_owns;    /* o efeito ligou o audio sozinho: ele mesmo desliga ao terminar */

/* APLL so em 44.1/48 kHz (precisao); taxas de voz usam o clock padrao */
static i2s_std_clk_config_t clk_for(uint32_t rate)
{
    i2s_std_clk_config_t c = I2S_STD_CLK_DEFAULT_CONFIG(rate);
    c.clk_src = (rate >= 44100) ? I2S_CLK_SRC_APLL : I2S_CLK_SRC_DEFAULT;
    return c;
}

static void drain(RingbufHandle_t rb)
{
    size_t n;
    void *p;
    while ((p = xRingbufferReceiveUpTo(rb, &n, 0, 4096)) != NULL) vRingbufferReturnItem(rb, p);
}

static void trim_rb_reader_side(RingbufHandle_t rb)
{
    if (!s_call_limit_bytes) return;
    while (true) {
        size_t free = xRingbufferGetCurFreeSize(rb);
        size_t used = (CALL_RB_SIZE > free) ? (CALL_RB_SIZE - free) : 0;
        if (used <= s_call_limit_bytes) break;
        size_t to_discard = used - s_call_limit_bytes;
        size_t n;
        void *p = xRingbufferReceiveUpTo(rb, &n, 0, to_discard);
        if (!p) break;
        vRingbufferReturnItem(rb, p);
    }
}

/* ---------------- efeitos sonoros (tons) ---------------- */

static void tone_render(int16_t *dst, size_t frames)
{
    for (size_t i = 0; i < frames; i++) {
        int16_t v = 0;
        while (s_tone_idx < s_tone_len) {
            const audio_tone_t *t = &s_tone_seq[s_tone_idx];
            uint32_t total = (uint32_t)t->ms * s_rate / 1000;
            if (s_tone_pos >= total) {
                s_tone_idx++;
                s_tone_pos = 0;
                s_tone_phase = 0;
                continue;
            }
            if (t->hz) {
                uint32_t ramp = s_rate * SFX_RAMP_MS / 1000;
                float env = 1.0f;
                if (s_tone_pos < ramp)                env = (float)s_tone_pos / ramp;
                else if (total - s_tone_pos < ramp)   env = (float)(total - s_tone_pos) / ramp;
                v = (int16_t)(sinf(s_tone_phase) * env * SFX_AMPLITUDE);
                s_tone_phase += 2.0f * (float)M_PI * t->hz / s_rate;
                if (s_tone_phase > 2.0f * (float)M_PI) s_tone_phase -= 2.0f * (float)M_PI;
            }
            s_tone_pos++;
            break;
        }
        dst[i] = v;
    }
}

static inline int16_t sat16(int32_t x)
{
    return (int16_t)(x > 32767 ? 32767 : (x < -32768 ? -32768 : x));
}

/* Soma o efeito (mono) em um bloco estereo intercalado */
static void tone_mix_stereo(int16_t *st, size_t frames)
{
    int16_t tone[MUSIC_CHUNK / 4];
    while (frames) {
        size_t n = frames < MUSIC_CHUNK / 4 ? frames : MUSIC_CHUNK / 4;
        tone_render(tone, n);
        for (size_t i = 0; i < n; i++) {
            st[2 * i]     = sat16((int32_t)st[2 * i] + tone[i]);
            st[2 * i + 1] = sat16((int32_t)st[2 * i + 1] + tone[i]);
        }
        st += 2 * n;
        frames -= n;
    }
}

/* Sem musica no buffer: o efeito toca sozinho */
static void pump_tone_only(void)
{
    int16_t st[CALL_FRAMES * 2] = { 0 };
    size_t w = 0;
    tone_mix_stereo(st, CALL_FRAMES);
    i2s_channel_write(s_tx, st, sizeof st, &w, 100);
}

/* ---------------- bombeamento (rodam dentro da task, com s_lock) ---------------- */

static void pump_music(void)
{
    if (s_prebuffering) {
        size_t fill = s_music_size - xRingbufferGetCurFreeSize(s_music_rb);
        if (fill < MUSIC_PREFILL) {
            if (s_tone_active) { pump_tone_only(); return; }
            vTaskDelay(pdMS_TO_TICKS(5));
            return;
        }
        s_prebuffering = false;
    }

    size_t n = 0, w = 0;
    void *p = xRingbufferReceiveUpTo(s_music_rb, &n, pdMS_TO_TICKS(10), MUSIC_CHUNK);
    if (p) {
        size_t fill = s_music_size - xRingbufferGetCurFreeSize(s_music_rb);

        /* Copia para um buffer local (o EQ altera no lugar) e libera o ringbuffer logo */
        n &= ~3u;
        memcpy(s_chunk, p, n);
        vRingbufferReturnItem(s_music_rb, p);
        eq_process(s_chunk, n / 4);
        if (s_tone_active) tone_mix_stereo(s_chunk, n / 4);
        uint8_t *d = (uint8_t *)s_chunk;

        /* Compensação de deriva de clock (drift) I2S vs Celular:
         * Manter o nível do buffer saudável (alvo ~ metade).
         * Se estiver muito cheio (overrun iminente), descarta 1 frame (4 bytes).
         * Se estiver muito vazio (underrun iminente), duplica 1 frame (4 bytes).
         */
        if (n >= 4) {
            if (fill > DRIFT_HIGH) {
                i2s_channel_write(s_tx, d + 4, n - 4, &w, 100);
            } else if (fill < DRIFT_LOW) {
                i2s_channel_write(s_tx, d, n, &w, 100);
                i2s_channel_write(s_tx, d + n - 4, 4, &w, 100);
            } else {
                i2s_channel_write(s_tx, d, n, &w, 100);
            }
        }
    } else {                                    /* underrun: toca silencio e reenche o colchao */
        s_prebuffering = true;
        if (s_tone_active) { pump_tone_only(); return; }
        static const int16_t silence[256];
        i2s_channel_write(s_tx, silence, sizeof silence, &w, 100);
    }
}

static void pump_call(void)
{
    int16_t in[CALL_FRAMES * 2], out[CALL_FRAMES * 2], mono[CALL_FRAMES];
    size_t rd = 0, wr = 0;

    /* uplink: microfones (L+R)/2 -> limpeza -> buffer do HFP */
    if (i2s_channel_read(s_rx, in, sizeof in, &rd, 100) != ESP_OK) return;
    size_t frames = rd / 4;
    for (size_t i = 0; i < frames; i++) mono[i] = (int16_t)((in[2 * i] + in[2 * i + 1]) / 2);
    voice_nr_process(mono, frames);
    xRingbufferSend(s_ul_rb, mono, frames * 2, 0);

    /* downlink: voz do interlocutor (mono) -> L e R. Falta de dados = silencio. */
    memset(out, 0, sizeof out);
    trim_rb_reader_side(s_dl_rb);
    size_t n = 0;
    int16_t *dl = xRingbufferReceiveUpTo(s_dl_rb, &n, 0, frames * 2);
    if (dl) {
        for (size_t i = 0; i < n / 2; i++) out[2 * i] = out[2 * i + 1] = dl[i];
        vRingbufferReturnItem(s_dl_rb, dl);
    }
    if (s_tone_active) tone_mix_stereo(out, frames);
    i2s_channel_write(s_tx, out, frames * 4, &wr, 100);
}

static void stop_locked(bool power_down);

static void audio_task(void *arg)
{
    for (;;) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        audio_io_mode_t m = s_mode;
        if (m == AUDIO_IO_MUSIC)     pump_music();
        else if (m == AUDIO_IO_CALL) pump_call();
        if (s_tone_active && s_tone_idx >= s_tone_len) {   /* efeito terminou */
            s_tone_active = false;
            if (s_tone_owns) stop_locked(true);
        }
        xSemaphoreGive(s_lock);

        if (m == AUDIO_IO_IDLE || s_reconfig_req) {
            vTaskDelay(pdMS_TO_TICKS(m == AUDIO_IO_IDLE ? 20 : 2));
        } else {
            /* Em streaming ativo (MUSIC ou CALL), a sincronização e temporização do loop
             * são governadas nativamente pelo bloqueio do DMA (i2s_channel_write com timeout)
             * e pelo recebimento do Ringbuffer (xRingbufferReceiveUpTo). Inserir vTaskDelay(1)
             * incondicional a cada iteração causava jitter e subamostragem audível. */
            taskYIELD();
        }
    }
}

/* ---------------- controle de modo ---------------- */

/* power_down=false quando o chamador vai religar o audio em seguida (troca de modo) */
static void stop_locked(bool power_down)
{
    s_tone_active = false;
    s_tone_owns = false;
    if (s_mode == AUDIO_IO_IDLE) return;
    audio_codec_mute(true);
    i2s_channel_disable(s_tx);
    if (s_mode == AUDIO_IO_CALL) i2s_channel_disable(s_rx);
    s_mode = AUDIO_IO_IDLE;
    if (power_down) audio_codec_power_down();
}

static esp_err_t start_mode(audio_io_mode_t mode, uint32_t rate, bool by_tone)
{
    esp_err_t ret = ESP_OK;
    s_reconfig_req = true;
    xSemaphoreTake(s_lock, portMAX_DELAY);

    if (mode == AUDIO_IO_MUSIC && s_mode == AUDIO_IO_CALL) {   /* chamada tem prioridade */
        ret = ESP_ERR_INVALID_STATE;
        goto out;
    }
    stop_locked(false);
    ESP_GOTO_ON_ERROR(audio_codec_power_up(), out, TAG, "codec power up");
    audio_codec_mute(true);

    ESP_GOTO_ON_ERROR(audio_codec_set_sample_rate(rate), out, TAG, "codec rate");
    if (mode == AUDIO_IO_MUSIC) eq_set_sample_rate(rate);
    ESP_GOTO_ON_ERROR(audio_codec_apply_filters(mode == AUDIO_IO_CALL ? &AUDIO_FILTERS_CALL
                                                                      : &AUDIO_FILTERS_MUSIC),
                      out, TAG, "filtros");

    i2s_std_clk_config_t clk = clk_for(rate);
    ESP_GOTO_ON_ERROR(i2s_channel_reconfig_std_clock(s_tx, &clk), out, TAG, "tx clock");
    ESP_GOTO_ON_ERROR(i2s_channel_reconfig_std_clock(s_rx, &clk), out, TAG, "rx clock");

    drain(s_music_rb); drain(s_dl_rb); drain(s_ul_rb);
    if (mode == AUDIO_IO_CALL) {
        voice_nr_init(rate);
        s_call_limit_bytes = rate * 2 * 35 / 1000;
    } else {
        s_call_limit_bytes = 0;
    }

    ESP_GOTO_ON_ERROR(i2s_channel_enable(s_tx), out, TAG, "tx enable");
    if (mode == AUDIO_IO_CALL) ESP_GOTO_ON_ERROR(i2s_channel_enable(s_rx), out, TAG, "rx enable");

    s_prebuffering = (mode == AUDIO_IO_MUSIC);
    s_mode = mode;
    s_rate = rate;
    s_tone_owns = by_tone;
    ESP_LOGI(TAG, "modo %s @ %lu Hz", mode == AUDIO_IO_CALL ? "CHAMADA" : "MUSICA", (unsigned long)rate);

out:
    if (ret != ESP_OK && s_mode == AUDIO_IO_IDLE) audio_codec_power_down();   /* falhou sem nada tocando */
    xSemaphoreGive(s_lock);
    s_reconfig_req = false;
    if (ret == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(30));          /* deixa o clock estabilizar antes de tirar o mudo */
        audio_codec_mute(false);
    }
    return ret;
}

esp_err_t audio_io_start(audio_io_mode_t mode, uint32_t rate)
{
    return start_mode(mode, rate, false);
}

esp_err_t audio_io_play_tones(const audio_tone_t *seq, size_t count)
{
    if (!seq || count == 0) return ESP_ERR_INVALID_ARG;
    if (s_mode == AUDIO_IO_IDLE) {
        ESP_RETURN_ON_ERROR(start_mode(AUDIO_IO_MUSIC, SFX_IDLE_RATE, true), TAG, "audio para efeito");
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t ret = ESP_ERR_INVALID_STATE;       /* o audio foi parado entre a partida e agora */
    if (s_mode != AUDIO_IO_IDLE) {
        s_tone_seq = seq;
        s_tone_len = count;
        s_tone_idx = 0;
        s_tone_pos = 0;
        s_tone_phase = 0;
        s_tone_active = true;
        ret = ESP_OK;
    }
    xSemaphoreGive(s_lock);
    return ret;
}

bool audio_io_tones_busy(void) { return s_tone_active; }

void audio_io_stop_mode(audio_io_mode_t mode)
{
    s_reconfig_req = true;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_mode == mode) stop_locked(true);
    xSemaphoreGive(s_lock);
    s_reconfig_req = false;
}

audio_io_mode_t audio_io_get_mode(void) { return s_mode; }

/* ---------------- Entradas de dados (Plano de Dados Puro - áudio contínuo, não bloqueia) ---------------- */

void audio_data_music_push(const uint8_t *d, uint32_t len)
{
    len &= ~3u;                                  /* frames de 4 bytes (L16+R16) */
    if (s_mode != AUDIO_IO_MUSIC || len == 0) return;
    xRingbufferSend(s_music_rb, d, len, 0);      /* cheio: descarta sem travar a pilha BT */
}

void audio_data_call_downlink_push(const uint8_t *d, uint32_t len)
{
    len &= ~1u;
    if (s_mode != AUDIO_IO_CALL || len == 0) return;
    xRingbufferSend(s_dl_rb, d, len, 0);
}

uint32_t audio_data_call_uplink_pull(uint8_t *buf, uint32_t size)
{
    uint32_t got = 0;
    if (s_mode == AUDIO_IO_CALL) {
        trim_rb_reader_side(s_ul_rb);
        size_t n = 0;
        void *p = xRingbufferReceiveUpTo(s_ul_rb, &n, 0, size);
        if (p) { memcpy(buf, p, n); vRingbufferReturnItem(s_ul_rb, p); got = n; }
    }
    if (got < size) memset(buf + got, 0, size - got);   /* falta de dados = silencio */
    return size;
}


/* ---------------- init ---------------- */

esp_err_t audio_io_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    /* Buffer grande de musica em PSRAM (acesso so da task de audio e do callback A2DP); sem PSRAM cai na RAM interna */
    s_music_size = MUSIC_RB_SIZE;
    s_music_rb = xRingbufferCreateWithCaps(s_music_size, RINGBUF_TYPE_BYTEBUF, MALLOC_CAP_SPIRAM);
    if (!s_music_rb) {
        s_music_size = MUSIC_RB_SIZE / 2;
        ESP_LOGW(TAG, "PSRAM indisponivel para o buffer de musica; usando RAM interna");
        s_music_rb = xRingbufferCreateWithCaps(s_music_size, RINGBUF_TYPE_BYTEBUF, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    s_dl_rb    = xRingbufferCreate(CALL_RB_SIZE, RINGBUF_TYPE_BYTEBUF);
    s_ul_rb    = xRingbufferCreate(CALL_RB_SIZE, RINGBUF_TYPE_BYTEBUF);
    ESP_RETURN_ON_FALSE(s_lock && s_music_rb && s_dl_rb && s_ul_rb, ESP_ERR_NO_MEM, TAG, "sem memoria");

    /* Full-duplex na mesma porta: TX = fones, RX = microfone. ESP32 e o mestre do clock. */
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    /* Ativa o auto_clear (limpeza automática). Em caso de underrun do DMA, isso evita que
       o último bloco seja repetido, o que produziria um "buzz" audível. */
    cc.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&cc, &s_tx, &s_rx), TAG, "new channel");

    i2s_std_config_t std = {
        .clk_cfg  = clk_for(44100),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,             /* o breakout gera o proprio MCLK (confirme no esquematico) */
            .bclk = BOARD_I2S_BCLK,
            .ws   = BOARD_I2S_WS,
            .dout = BOARD_I2S_DOUT,
            .din  = BOARD_I2S_DIN,
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx, &std), TAG, "tx init");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_rx, &std), TAG, "rx init");

    /* Core 1: o controlador Bluetooth roda no core 0 */
    BaseType_t ok = xTaskCreatePinnedToCore(audio_task, "audio_io", 4096, NULL, 10, NULL, 1);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "task");
    return ESP_OK;
}

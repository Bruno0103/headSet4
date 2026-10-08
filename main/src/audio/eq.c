#include "eq.h"

#include <math.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "settings.h"

static const char *TAG = "eq";

#define SAVE_DELAY_MS  3000      /* NVS so depois que o usuario parar de mexer (nao competir com o streaming) */

typedef enum { BAND_LOWSHELF, BAND_PEAK, BAND_HIGHSHELF } band_type_t;

static const struct { band_type_t type; float freq; float q; } k_bands[EQ_NUM_BANDS] = {
    { BAND_LOWSHELF,  100.0f,   0.707f },
    { BAND_PEAK,      400.0f,   1.0f   },
    { BAND_PEAK,      1500.0f,  1.0f   },
    { BAND_PEAK,      4500.0f,  1.0f   },
    { BAND_HIGHSHELF, 10000.0f, 0.707f },
};

static const int8_t k_presets[EQ_PRESET_COUNT][EQ_NUM_BANDS] = {
    [EQ_PRESET_FLAT]   = {  0,  0,  0,  0,  0 },
    [EQ_PRESET_BASS]   = { 10,  4,  0,  0,  0 },
    [EQ_PRESET_VOCAL]  = { -2, -2,  4,  4,  0 },
    [EQ_PRESET_TREBLE] = {  0,  0,  0,  4,  8 },
};

typedef struct { float b0, b1, b2, a1, a2; } coef_t;

typedef struct {
    coef_t coef[EQ_NUM_BANDS];
    float  preamp;
    bool   bypass;
} filter_t;

typedef struct {
    uint8_t preset;
    int8_t  gains[EQ_NUM_BANDS];
} nv_t;

static SemaphoreHandle_t  s_mtx;
static esp_timer_handle_t s_save_tmr;
static nv_t               s_cfg;
static uint32_t           s_rate = 44100;

static filter_t s_pending;               /* escrito sob s_mtx pelos setters */
static volatile bool s_dirty;
static filter_t s_active;                /* usado so pela task de audio */
static float    s_z[EQ_NUM_BANDS][2][2]; /* [banda][canal][z1,z2] (transposta DF2) */

/* RBJ Audio EQ Cookbook */
static coef_t design(band_type_t type, float f0, float q, float gain_db, float fs)
{
    float A = powf(10.0f, gain_db / 40.0f);
    float w0 = 2.0f * (float)M_PI * f0 / fs;
    float cw = cosf(w0), sw = sinf(w0);
    float alpha = sw / (2.0f * q);
    float b0, b1, b2, a0, a1, a2;

    if (type == BAND_PEAK) {
        b0 = 1 + alpha * A;  b1 = -2 * cw;  b2 = 1 - alpha * A;
        a0 = 1 + alpha / A;  a1 = -2 * cw;  a2 = 1 - alpha / A;
    } else {
        float sq = 2.0f * sqrtf(A) * alpha;
        if (type == BAND_LOWSHELF) {
            b0 = A * ((A + 1) - (A - 1) * cw + sq);
            b1 = 2 * A * ((A - 1) - (A + 1) * cw);
            b2 = A * ((A + 1) - (A - 1) * cw - sq);
            a0 = (A + 1) + (A - 1) * cw + sq;
            a1 = -2 * ((A - 1) + (A + 1) * cw);
            a2 = (A + 1) + (A - 1) * cw - sq;
        } else {
            b0 = A * ((A + 1) + (A - 1) * cw + sq);
            b1 = -2 * A * ((A - 1) + (A + 1) * cw);
            b2 = A * ((A + 1) + (A - 1) * cw - sq);
            a0 = (A + 1) - (A - 1) * cw + sq;
            a1 = 2 * ((A - 1) - (A + 1) * cw);
            a2 = (A + 1) - (A - 1) * cw - sq;
        }
    }
    return (coef_t){ b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
}

/* chamar com s_mtx */
static void rebuild_locked(void)
{
    filter_t f = { 0 };
    int8_t max_gain = 0;
    bool flat = true;
    for (int i = 0; i < EQ_NUM_BANDS; i++) {
        int8_t g = s_cfg.gains[i];
        if (g != 0) flat = false;
        if (g > max_gain) max_gain = g;
        f.coef[i] = design(k_bands[i].type, k_bands[i].freq, k_bands[i].q, g * EQ_GAIN_STEP_DB, (float)s_rate);
    }
    f.bypass = flat;
    f.preamp = powf(10.0f, -(max_gain * EQ_GAIN_STEP_DB) / 20.0f);
    s_pending = f;
    s_dirty = true;
}

static void schedule_save(void)
{
    esp_timer_stop(s_save_tmr);
    esp_timer_start_once(s_save_tmr, (uint64_t)SAVE_DELAY_MS * 1000);
}

static void save_cb(void *arg)
{
    nv_t snap;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    snap = s_cfg;
    xSemaphoreGive(s_mtx);

    /* Gravação delegada ao actor settings (dono único do NVS) */
    esp_err_t err = settings_set_blob(SETTINGS_KEY_EQ_PRESET, &snap, sizeof(snap));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao solicitar gravacao de EQ para o actor settings: %s", esp_err_to_name(err));
    }
}

esp_err_t eq_init(void)
{
    s_mtx = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_mtx, ESP_ERR_NO_MEM, TAG, "mutex");
    const esp_timer_create_args_t ta = { .callback = save_cb, .name = "eq_save" };
    ESP_RETURN_ON_ERROR(esp_timer_create(&ta, &s_save_tmr), TAG, "timer");

    memset(&s_cfg, 0, sizeof s_cfg);

    /* Leitura delegada ao actor settings (dono único do NVS) */
    nv_t tmp;
    if (settings_get_blob(SETTINGS_KEY_EQ_PRESET, &tmp, sizeof(tmp)) == ESP_OK && tmp.preset < EQ_PRESET_COUNT) {
        bool ok = true;
        for (int i = 0; i < EQ_NUM_BANDS; i++) {
            if (tmp.gains[i] < -EQ_GAIN_MAX || tmp.gains[i] > EQ_GAIN_MAX) ok = false;
        }
        if (ok) s_cfg = tmp;
    }

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    rebuild_locked();
    xSemaphoreGive(s_mtx);
    ESP_LOGI(TAG, "EQ pronto (preset %d)", s_cfg.preset);
    return ESP_OK;
}

void eq_set_sample_rate(uint32_t hz)
{
    if (!s_mtx || hz == 0) return;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_rate = hz;
    rebuild_locked();
    xSemaphoreGive(s_mtx);
}

void eq_process(int16_t *x, size_t frames)
{
    if (s_dirty && xSemaphoreTake(s_mtx, 0) == pdTRUE) {      /* nunca bloqueia a task de audio */
        s_active = s_pending;
        s_dirty = false;
        xSemaphoreGive(s_mtx);
        memset(s_z, 0, sizeof s_z);
    }
    if (s_active.bypass) return;

    const float pre = s_active.preamp;
    for (size_t n = 0; n < frames; n++) {
        for (int ch = 0; ch < 2; ch++) {
            float v = (float)x[2 * n + ch] * pre;
            for (int b = 0; b < EQ_NUM_BANDS; b++) {
                const coef_t *c = &s_active.coef[b];
                float *z = s_z[b][ch];
                float y = c->b0 * v + z[0];
                z[0] = c->b1 * v - c->a1 * y + z[1];
                z[1] = c->b2 * v - c->a2 * y;
                v = y;
            }
            if (v > 32767.0f) v = 32767.0f;
            else if (v < -32768.0f) v = -32768.0f;
            x[2 * n + ch] = (int16_t)lrintf(v);
        }
    }
}

esp_err_t eq_set_preset(eq_preset_t preset)
{
    ESP_RETURN_ON_FALSE(s_mtx && preset < EQ_PRESET_CUSTOM, ESP_ERR_INVALID_ARG, TAG, "preset invalido");
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_cfg.preset = preset;
    memcpy(s_cfg.gains, k_presets[preset], EQ_NUM_BANDS);
    rebuild_locked();
    xSemaphoreGive(s_mtx);
    schedule_save();
    ESP_LOGI(TAG, "Preset %d aplicado", preset);
    return ESP_OK;
}

esp_err_t eq_set_gains(const int8_t gains[EQ_NUM_BANDS])
{
    ESP_RETURN_ON_FALSE(s_mtx && gains, ESP_ERR_INVALID_ARG, TAG, "argumento");
    for (int i = 0; i < EQ_NUM_BANDS; i++) {
        ESP_RETURN_ON_FALSE(gains[i] >= -EQ_GAIN_MAX && gains[i] <= EQ_GAIN_MAX, ESP_ERR_INVALID_ARG, TAG,
                            "ganho da banda %d fora de +-12 dB", i);
    }
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_cfg.preset = EQ_PRESET_CUSTOM;
    memcpy(s_cfg.gains, gains, EQ_NUM_BANDS);
    rebuild_locked();
    xSemaphoreGive(s_mtx);
    schedule_save();
    return ESP_OK;
}

eq_preset_t eq_get_preset(void)
{
    return (eq_preset_t)s_cfg.preset;
}

void eq_get_gains(int8_t gains[EQ_NUM_BANDS])
{
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    memcpy(gains, s_cfg.gains, EQ_NUM_BANDS);
    xSemaphoreGive(s_mtx);
}

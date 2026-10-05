/**
 * @file eq.h
 * @brief Equalizador parametrico de musica (biquads RBJ em float) com presets e persistencia em NVS.
 *
 * 5 bandas: low shelf 100 Hz, peaking 400 Hz / 1.5 kHz / 4.5 kHz, high shelf 10 kHz.
 * Ganhos em passos de 0.5 dB (int8, -24..+24 = +-12 dB). Um pre-ganho negativo igual ao maior
 * ganho positivo evita clipping. Tudo flat = bypass (custo zero).
 * eq_process() roda na task de audio; os setters podem ser chamados de qualquer task.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EQ_NUM_BANDS      5
#define EQ_GAIN_STEP_DB   0.5f
#define EQ_GAIN_MAX       24      /* +-12 dB */

typedef enum {
    EQ_PRESET_FLAT = 0,
    EQ_PRESET_BASS,
    EQ_PRESET_VOCAL,
    EQ_PRESET_TREBLE,
    EQ_PRESET_CUSTOM,
    EQ_PRESET_COUNT,
} eq_preset_t;

/** Carrega o ultimo estado do NVS (ou flat). Chamar uma vez, antes de eq_process(). */
esp_err_t eq_init(void);

/** Recalcula os coeficientes para a taxa de amostragem da musica (32/44.1/48 kHz). */
void eq_set_sample_rate(uint32_t hz);

/** Processa PCM estereo 16 bits intercalado, no lugar. */
void eq_process(int16_t *stereo, size_t frames);

esp_err_t   eq_set_preset(eq_preset_t preset);
esp_err_t   eq_set_gains(const int8_t gains[EQ_NUM_BANDS]);     /* vira preset CUSTOM */
eq_preset_t eq_get_preset(void);
void        eq_get_gains(int8_t gains[EQ_NUM_BANDS]);

#ifdef __cplusplus
}
#endif

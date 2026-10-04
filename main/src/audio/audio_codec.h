#pragma once
/* Camada de funcoes do WM8960:
 *   1) configuracao
 *   2) volume (escala AVRCP)
 *   3) filtros que rodam dentro do codec */
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>


typedef enum {
  AUDIO_DEEMPH_OFF = 0,
  AUDIO_DEEMPH_32K = 1,
  AUDIO_DEEMPH_44K1 = 2,
  AUDIO_DEEMPH_48K = 3,
} audio_deemph_t;

typedef struct {
  /* --- reproducao (DAC) --- */
  bool enhance3d;            /* realce estereo 3D */
  uint8_t enhance3d_depth;   /* 0..15 */
  audio_deemph_t deemphasis; /* so p/ fontes pre-enfatizadas; normalmente OFF */

  /* --- captura (microfone / ADC) --- */
  bool adc_hpf;         /* passa-altas do ADC (tira graves/vento) */
  bool alc;             /* controle automatico de nivel */
  bool alc_limiter;     /* true = modo limiter, false = modo ALC */
  uint8_t alc_target;   /* 0..15: -22.5 dBFS .. -1.5 dBFS (1.5 dB/passo) */
  uint8_t alc_max_gain; /* 0..7: -12 dB .. +30 dB (6 dB/passo) */
  uint8_t alc_min_gain; /* 0..7: -17.25 dB .. +24.75 dB (6 dB/passo) */
  uint8_t alc_hold;     /* 0..15 */
  uint8_t alc_decay;    /* 0..10 */
  uint8_t alc_attack;   /* 0..10 */
  bool noise_gate;      /* so atua com o ALC ligado */
  uint8_t
      noise_gate_threshold; /* 0..31: -76.5 dBFS .. -30 dBFS (1.5 dB/passo) */
} audio_filters_t;

extern const audio_filters_t
    AUDIO_FILTERS_MUSIC; /* ouvir musica + mic ambiente */
extern const audio_filters_t AUDIO_FILTERS_CALL; /* chamada de voz */

/** Cria o I2C, detecta o WM8960 e aplica a configuracao do sketch (44.1 kHz,
 * headphone). */
esp_err_t audio_codec_init(void);

/** Reprograma PLL/divisores. Suporta 44100, 48000, 32000, 16000 e 8000 Hz. */
esp_err_t audio_codec_set_sample_rate(uint32_t hz);

/** Volume do headphone na escala do AVRCP: 0 = mudo, 127 = maximo (0 dB). */
esp_err_t audio_codec_set_volume(uint8_t avrcp_volume);

/** Soft-mute do DAC. */
esp_err_t audio_codec_mute(bool mute);

/** Ganho do PGA do microfone ambiente, 0..63 (23 = 0 dB). Ignorado se o ALC
 * estiver ligado. */
esp_err_t audio_codec_set_ambient_gain(uint8_t pga);

/** Aplica um conjunto de filtros. */
esp_err_t audio_codec_apply_filters(const audio_filters_t *f);

#pragma once
/* Caminho de dados de audio (I2S + buffers), usado pelos modulos Bluetooth.
 *   MUSICA : A2DP -> buffer -> I2S TX            (estereo, 44.1/48/32 kHz)
 *   CHAMADA: HFP  -> buffer -> I2S TX            (voz do interlocutor, mono)
 *            I2S RX -> limpeza de ruido -> HFP   (seu microfone, mono, 8/16 kHz) */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    AUDIO_IO_IDLE = 0,
    AUDIO_IO_MUSIC,
    AUDIO_IO_CALL,
} audio_io_mode_t;

typedef struct {
    uint16_t hz;   /* 0 = pausa */
    uint16_t ms;
} audio_tone_t;

esp_err_t audio_io_init(void);

/** Inicia um modo na taxa dada (reprograma codec + I2S + filtros). Chamada tem prioridade sobre musica. */
esp_err_t audio_io_start(audio_io_mode_t mode, uint32_t sample_rate_hz);

/** Para o I2S, mas somente se o modo atual for 'mode'. */
void audio_io_stop_mode(audio_io_mode_t mode);

audio_io_mode_t audio_io_get_mode(void);

/** Toca uma sequencia de tons (o array precisa ser estatico). Mistura sobre musica/chamada; com o audio
 *  parado, liga o codec so pelo tempo do efeito. Termine a sequencia com uma pausa para nao cortar o fim. */
esp_err_t audio_io_play_tones(const audio_tone_t *seq, size_t count);

/** true enquanto uma sequencia de tons ainda esta tocando. */
bool audio_io_tones_busy(void);

#include "audio_data.h"

/* --- Usados nos callbacks de dados do Bluetooth (plano de dados rápido, não bloqueia) ---
 * Os aliases abaixo mantêm compatibilidade retroativa enquanto delegam para audio_data_*. */
static inline void audio_io_music_push(const uint8_t *pcm_stereo16, uint32_t len) {
    audio_data_music_push(pcm_stereo16, len);
}
static inline void audio_io_call_downlink_push(const uint8_t *pcm_mono16, uint32_t len) {
    audio_data_call_downlink_push(pcm_mono16, len);
}
static inline uint32_t audio_io_call_uplink_pull(uint8_t *pcm_mono16, uint32_t size) {
    return audio_data_call_uplink_pull(pcm_mono16, size);
}


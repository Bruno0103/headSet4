#pragma once
/* Caminho de dados de audio (I2S + buffers), usado pelos modulos Bluetooth.
 *   MUSICA : A2DP -> buffer -> I2S TX            (estereo, 44.1/48/32 kHz)
 *   CHAMADA: HFP  -> buffer -> I2S TX            (voz do interlocutor, mono)
 *            I2S RX -> limpeza de ruido -> HFP   (seu microfone, mono, 8/16 kHz) */
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    AUDIO_IO_IDLE = 0,
    AUDIO_IO_MUSIC,
    AUDIO_IO_CALL,
} audio_io_mode_t;

esp_err_t audio_io_init(void);

/** Inicia um modo na taxa dada (reprograma codec + I2S + filtros). Chamada tem prioridade sobre musica. */
esp_err_t audio_io_start(audio_io_mode_t mode, uint32_t sample_rate_hz);

/** Para o I2S, mas somente se o modo atual for 'mode'. */
void audio_io_stop_mode(audio_io_mode_t mode);

audio_io_mode_t audio_io_get_mode(void);

/* --- usados nos callbacks de dados do Bluetooth (nao bloqueiam) --- */
void     audio_io_music_push(const uint8_t *pcm_stereo16, uint32_t len);          /* A2DP */
void     audio_io_call_downlink_push(const uint8_t *pcm_mono16, uint32_t len);    /* HFP incoming */
uint32_t audio_io_call_uplink_pull(uint8_t *pcm_mono16, uint32_t size);           /* HFP outgoing */

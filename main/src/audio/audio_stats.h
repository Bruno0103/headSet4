#pragma once

#include <stdint.h>

/**
 * @brief Estatísticas agregadas do processamento de áudio (I2S/Codec).
 */
typedef struct {
    uint32_t underruns;         ///< Vezes que o DMA não teve dados e repetiu o último frame.
    uint32_t overruns;          ///< O produtor BT foi mais rápido e perdeu buffers.
    uint32_t plc_count;         ///< Quantos samples sintetizados por Packet Loss Concealment.
    uint32_t drift_drops;       ///< Samples ignorados p/ compensar relógio rápido.
    uint32_t drift_dups;        ///< Samples duplicados p/ compensar relógio lento.
    uint32_t jitter_fill_min;   ///< Mínimo histórico do Jitter Buffer (bytes).
    uint32_t jitter_fill_max;   ///< Máximo histórico do Jitter Buffer (bytes).
    uint32_t latency_ms_est;    ///< Estimativa de latência atual do buffer de DMA.
} audio_stats_t;

/**
 * @brief Retorna snapshot das estatísticas atuais.
 * @note Thread-safe.
 */
audio_stats_t audio_stats_get(void);

/**
 * @brief Zera as estatísticas.
 * @note Thread-safe.
 */
void audio_stats_reset(void);

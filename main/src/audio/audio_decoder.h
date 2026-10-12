#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Inicializa o decodificador para o codec selecionado (e.g., SBC, mSBC).
 */
bool audio_decoder_init(uint32_t sample_rate);

/**
 * @brief Decodifica 1 frame/pacote.
 * @note Deve rodar rápido e em contexto de task.
 * @param in_data Pacote BT recebido.
 * @param in_len Tamanho do pacote.
 * @param out_pcm Buffer de saída (PCM 16-bit).
 * @param out_len[out] Bytes de PCM gerados.
 * @return true se sucesso.
 */
bool audio_decoder_decode(const uint8_t *in_data, size_t in_len, int16_t *out_pcm, size_t *out_len);

/**
 * @brief Libera o decodificador atual.
 */
void audio_decoder_deinit(void);

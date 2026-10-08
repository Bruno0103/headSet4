/**
 * @file audio_data.h
 * @brief Plano de Dados de Áudio (PCM de alta velocidade / baixa latência).
 *
 * Agente A3 - Audio Engine / Isolamento Arquitetural (AGENTS.md §4.1):
 * - Este cabeçalho expõe EXCLUSIVAMENTE o caminho de streaming de áudio contínuo.
 * - Callbacks da pilha Bluetooth (A2DP, HFP) usam estas funções para push/pull direto nos ringbuffers.
 * - NENHUM comando de controle ou esp_event trafega por aqui.
 * - Funções não bloqueiam e operam com semântica de descarte rápido em caso de overflow.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Envia amostras estéreo PCM 16-bit (A2DP sink) para o ringbuffer de música.
 *
 * Não bloqueia. Amostras excedentes são descartadas se o ringbuffer estiver cheio.
 *
 * @param pcm_stereo16 Ponteiro para os dados PCM intercalados (L16, R16).
 * @param len Tamanho em bytes dos dados (truncado internamente para múltiplos de 4).
 */
void audio_data_music_push(const uint8_t *pcm_stereo16, uint32_t len);

/**
 * @brief Envia amostras mono PCM 16-bit de voz recebida (HFP downlink) para o ringbuffer de chamada.
 *
 * Não bloqueia. Amostras excedentes são descartadas se o buffer estiver saturado.
 *
 * @param pcm_mono16 Ponteiro para dados PCM mono de voz.
 * @param len Tamanho em bytes dos dados.
 */
void audio_data_call_downlink_push(const uint8_t *pcm_mono16, uint32_t len);

/**
 * @brief Retira amostras mono PCM 16-bit capturadas pelo microfone com Voice NR (HFP uplink).
 *
 * Não bloqueia. Caso o buffer esteja vazio ou com dados insuficientes, preenche com silêncio.
 *
 * @param pcm_mono16 Buffer de destino para escrita das amostras.
 * @param size Quantidade de bytes solicitada pelo stack HFP.
 * @return uint32_t Quantidade real de bytes entregues (sempre igual a 'size').
 */
uint32_t audio_data_call_uplink_pull(uint8_t *pcm_mono16, uint32_t size);

#ifdef __cplusplus
}
#endif

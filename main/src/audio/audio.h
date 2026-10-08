/**
 * @file audio.h
 * @brief Interface do Motor de Áudio e Actor Audio (Agente A3).
 *
 * Arquitetura Event-Driven & Actor Model (AGENTS.md §4.2, WP 3.1):
 * - O Actor Audio é o ÚNICO dono dos periféricos e pipelines de áudio:
 *   - Codec WM8960 (I2C)
 *   - I2S DMA (TX reprodução, RX gravação microfone)
 *   - Equalizador de 5 bandas (eq.h)
 *   - Voice NR (supressão de ruído mono para uplink HFP)
 *   - Síntese de tons e alertas sonoros (sfx.h)
 *
 * Características da Task do Actor:
 * - Core: 1 (isolado do controlador BT no Core 0)
 * - Prioridade: 10 (tempo-real de controle de áudio)
 * - Stack: 4096 bytes
 * - Tamanho da Fila: 16 mensagens
 *
 * Comandos suportados via hs_actor_send / hs_actor_request (definidos em hs_cmds.h):
 * - AUDIO_CMD_START_MUSIC : Inicializa / reprograma pipeline para reprodução estéreo A2DP.
 * - AUDIO_CMD_START_CALL  : Inicializa / reprograma pipeline mono bidirecional HFP com Voice NR.
 * - AUDIO_CMD_STOP        : Para I2S e coloca codec em modo standby de baixo consumo.
 * - AUDIO_CMD_PLAY_TONE   : Dispara reprodução de tom sintetizado / SFX.
 * - AUDIO_CMD_SET_VOLUME  : Ajusta ganho/volume do hardware codec (0 a 100%).
 * - AUDIO_CMD_SET_EQ      : Aplica preset de EQ ou ganhos de bandas (-12 dB a +12 dB).
 * - AUDIO_CMD_SET_MUTE    : Muta ou desmuta saída master do hardware.
 *
 * Eventos publicados no barramento esp_event (AUDIO_EVT, definidos em hs_events.h):
 * - AUDIO_EVT_MODE_CHANGED   : Mudança entre IDLE, MUSIC_A2DP, CALL_HFP ou TONE_SFX.
 * - AUDIO_EVT_VOLUME_CHANGED : Atualização do nível de volume master ou mute.
 * - AUDIO_EVT_EQ_CHANGED     : Alteração de equalizador.
 * - AUDIO_EVT_TONE_DONE      : Fim de tom / SFX.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "hs_actor.h"
#include "hs_cmds.h"
#include "hs_events.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Inicializa todo o subsistema de áudio (Codec WM8960, I2S DMA, EQ, SFX) e sobe o Actor Audio.
 *
 * Deve ser chamado antes da inicialização do Bluetooth durante o boot do sistema.
 * Cria o ator de áudio fixo no Core 1 com prioridade 10.
 *
 * @return ESP_OK se inicializado com sucesso, ou código de erro em caso de falha.
 */
esp_err_t audio_init(void);

/**
 * @brief Encerra e limpa o ator de áudio e desliga periféricos com segurança.
 *
 * @return ESP_OK em caso de sucesso.
 */
esp_err_t audio_deinit(void);

/**
 * @brief Obtém o handle do Actor Audio.
 *
 * Permite que outros módulos enviem comandos diretamente via hs_actor_send()
 * ou realizem chamadas de request/reply síncronas via hs_actor_request().
 *
 * @return hs_actor_t* Ponteiro para o ator de áudio, ou NULL se ainda não inicializado.
 */
hs_actor_t *audio_actor_get(void);

/* ============================================================================
 * HELPERS CONVENIENTES PARA ENVIO DE COMANDOS (Encapsulam hs_actor_send)
 * ============================================================================ */

/**
 * @brief Helper para solicitar início de reprodução de música (A2DP).
 *
 * Envia assincronamente AUDIO_CMD_START_MUSIC para o ator de áudio.
 *
 * @param sample_rate Taxa de amostragem em Hz (ex: 44100, 48000, 32000).
 * @return esp_err_t ESP_OK se o comando foi enfileirado com sucesso.
 */
esp_err_t audio_cmd_start_music_send(uint32_t sample_rate);

/**
 * @brief Helper para solicitar início de modo de chamada de voz (HFP).
 *
 * Envia assincronamente AUDIO_CMD_START_CALL para o ator de áudio.
 *
 * @param sample_rate Taxa de amostragem em Hz (8000 para NB, 16000 para WB).
 * @param enable_nr Se o algoritmo Voice NR de redução de ruído deve ser ativado.
 * @return esp_err_t ESP_OK se o comando foi enfileirado com sucesso.
 */
esp_err_t audio_cmd_start_call_send(uint32_t sample_rate, bool enable_nr);

/**
 * @brief Helper para solicitar a parada do modo ativo de áudio.
 *
 * Envia assincronamente AUDIO_CMD_STOP para o ator de áudio.
 *
 * @param mode_mask Máscara de modos a parar (0xFF para parar qualquer modo ativo).
 * @return esp_err_t ESP_OK se o comando foi enfileirado com sucesso.
 */
esp_err_t audio_cmd_stop_send(uint8_t mode_mask);

/**
 * @brief Helper para ajustar o volume master via comando.
 *
 * Envia assincronamente AUDIO_CMD_SET_VOLUME para o ator de áudio.
 *
 * @param volume_percent Volume de 0 a 100%.
 * @return esp_err_t ESP_OK se o comando foi enfileirado com sucesso.
 */
esp_err_t audio_cmd_set_volume_send(uint8_t volume_percent);

/**
 * @brief Helper para tocar um tom ou SFX sonoro.
 *
 * Envia assincronamente AUDIO_CMD_PLAY_TONE para o ator de áudio.
 *
 * @param tone_id Identificador do tom / efeito (tabela sfx).
 * @param interrupt_current Se true, interrompe tom que esteja tocando agora.
 * @return esp_err_t ESP_OK se o comando foi enfileirado com sucesso.
 */
esp_err_t audio_cmd_play_tone_send(uint16_t tone_id, bool interrupt_current);

/**
 * @brief Helper para mutar ou desmutar a saída de áudio.
 *
 * @param mute true para mutar, false para desmutar.
 * @return esp_err_t ESP_OK se o comando foi enfileirado com sucesso.
 */
esp_err_t audio_cmd_set_mute_send(bool mute);

/**
 * @brief Helper para aplicar preset ou ganhos de equalizador.
 *
 * @param preset_id ID do preset (0=Flat, 1=Bass, 2=Vocal, 3=Treble, 0xFF=Custom).
 * @param gains_db Array de 5 ganhos em dB (-12 a +12) se customizado (ou NULL).
 * @return esp_err_t ESP_OK se o comando foi enfileirado com sucesso.
 */
esp_err_t audio_cmd_set_eq_send(uint8_t preset_id, const int8_t gains_db[5]);

#ifdef __cplusplus
}
#endif

/**
 * @file phone_ctl.h
 * @brief Actor phone_ctl sob demanda (Agente A7 - App do Celular).
 *
 * Arquitetura Event-Driven & Actor Model (AGENTS.md WP 7.2 & 7.3):
 * - Criado sob demanda no momento em que um cliente BLE conecta ou envia comandos.
 * - Encerra automaticamente por inatividade (idle_ms = 30000) apos desconexao,
 *   economizando memoria de stack (3072 bytes) quando nenhum smartphone estiver conectado.
 * - Processa mensagens de entrada recebidas via GATT Write (cJSON), desmontando pacotes
 *   e despachando ordens imperativas (comandos) diretamente para os respectivos atores
 *   donos dos recursos (audio, bt_link, settings, actuators).
 * - Monitora eventos do barramento central (esp_event: SENSOR_EVT, BT_EVT, AUDIO_EVT, CFG_EVT)
 *   e gera notificacoes BLE com mecanismo de throttling/debounce temporal para nao
 *   sobrecarregar o enlace de radio BLE.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "hs_actor.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Inicializa a infraestrutura do actor phone_ctl e inscreve nos eventos centrais do sistema.
 *
 * @return esp_err_t ESP_OK em caso de sucesso.
 */
esp_err_t phone_ctl_init(void);

/**
 * @brief Obtém o handle do ator phone_ctl (para hs_actor_send ou testes).
 *
 * @return hs_actor_t* Ponteiro para o ator ou NULL se nao inicializado.
 */
hs_actor_t *phone_ctl_get_actor(void);

/**
 * @brief Despacha um chunk ou mensagem completa recebida via GATT Write para a fila do actor phone_ctl.
 *
 * Garante que a task do ator sob demanda seja acordada/criada se necessario.
 *
 * @param conn_id ID da conexao BLE do cliente.
 * @param data Dados recebidos no atributo GATT.
 * @param len Quantidade de bytes recebidos.
 * @return esp_err_t ESP_OK se postado com sucesso.
 */
esp_err_t phone_ctl_post_incoming_data(uint16_t conn_id, const uint8_t *data, size_t len);

/**
 * @brief Notifica o encerramento da conexao do cliente BLE para iniciar a contagem de timeout de idle.
 *
 * @param conn_id ID da conexao desconectada.
 */
void phone_ctl_notify_disconnect(uint16_t conn_id);

#ifdef __cplusplus
}
#endif

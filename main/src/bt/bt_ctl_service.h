/**
 * @file bt_ctl_service.h
 * @brief Servico GATT proprietario de Controle e Telemetria BLE para Smartphone.
 *
 * Arquitetura Event-Driven & Actor Model (AGENTS.md WP 7.1):
 * - Servico UUID: 5E0A1C00-4D2F-4A7B-9C31-0E5F2A6B7C8D (GATT Control Service)
 *   - Caracteristica Comando (WRITE, 5E0A1C01-...): Recebe comandos em JSON do app.
 *     Requer link criptografado (PERM_WRITE_ENCRYPTED / PERM_WRITE_ENC_MITM se pareado).
 *   - Caracteristica Resposta / Telemetria (NOTIFY, 5E0A1C02-... + CCCD):
 *     Envia respostas de comandos e notificacoes de eventos com throttling para o app.
 * - Registrado no despachante BLE (bt_ble.c) atraves de bt_ble_gatts_register().
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Identificador de aplicacao GATT para o servico de controle.
 */
#define BT_CTL_APP_ID 0x47

/**
 * @brief Inicializa a tabela de atributos GATT e registra o servico no bt_ble.
 * Deve ser chamado durante a subida da pilha BLE (bt_core.c).
 *
 * @return esp_err_t ESP_OK em caso de sucesso.
 */
esp_err_t bt_ctl_service_init(void);

/**
 * @brief Envia uma resposta ou notificacao ao smartphone conectado via GATT Notify.
 *
 * @param conn_id ID da conexao BLE do cliente.
 * @param data Ponteiro para os dados a enviar (geralmente JSON terminado em null ou binario).
 * @param len Tamanho dos dados em bytes.
 * @return esp_err_t ESP_OK se transmitido ou enfileirado com sucesso.
 */
esp_err_t bt_ctl_service_send_notify(uint16_t conn_id, const uint8_t *data, size_t len);

/**
 * @brief Informa se as notificacoes (CCCD) estao ativas para o cliente conectado.
 *
 * @return true se o cliente habilitou notificacoes no CCCD.
 */
bool bt_ctl_service_is_notify_enabled(void);

/**
 * @brief Retorna o conn_id atualmente conectado ao servico de controle (ou 0xFFFF se nenhum).
 *
 * @return uint16_t conn_id ativo ou 0xFFFF.
 */
uint16_t bt_ctl_service_get_conn_id(void);

#ifdef __cplusplus
}
#endif

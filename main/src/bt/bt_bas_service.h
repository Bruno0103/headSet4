/**
 * @file bt_bas_service.h
 * @brief Servico padrao Bluetooth SIG Battery Service (BAS, UUID 0x180F).
 *
 * Fornece compatibilidade universal para dispositivos Android (e Windows/iOS/Linux)
 * informarem a porcentagem de bateria (0..100%) atraves de BLE GATT.
 * 
 * Arquitetura Event-Driven & Actor Model:
 * - Ouve eventos SENSOR_EVT_BATTERY no barramento hs_events.
 * - Registrado no despachante BLE (bt_ble.c) via bt_ble_gatts_register().
 * - Notifica clientes BLE inscritos via GATT Notify (UUID 0x2A19).
 */

#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Identificador de aplicacao GATT para o Servico de Bateria padrao (BAS).
 */
#define BT_BAS_APP_ID 0x48

/**
 * @brief Inicializa e registra o servico BAS no despachante BLE (bt_ble).
 * Deve ser chamado durante stack_up_hdl() em bt_core.c.
 *
 * @return esp_err_t ESP_OK em caso de sucesso.
 */
esp_err_t bt_bas_service_init(void);

#ifdef __cplusplus
}
#endif

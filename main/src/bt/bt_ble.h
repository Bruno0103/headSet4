#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Inicializa o servidor GATT (BLE) com a tabela de serviços.
 * Inclui: Battery Service, Device Name e um serviço customizado de EQ.
 *
 * @return esp_err_t ESP_OK em caso de sucesso
 */
esp_err_t bt_ble_init(void);

/**
 * @brief Configura o Advertising BLE para modo RÁPIDO.
 * Ideal para quando o fone é colocado na cabeça, permitindo reconexão imediata.
 */
void bt_ble_set_adv_fast(void);

/**
 * @brief Configura o Advertising BLE para modo LENTO.
 * Ideal para quando o fone está em repouso (mesa), economizando energia.
 */
void bt_ble_set_adv_slow(void);

#ifdef __cplusplus
}
#endif

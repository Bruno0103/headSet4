/**
 * @file bt_eq_service.h
 * @brief Servico GATT proprietario para controlar o equalizador por BLE.
 *
 * Servico 5E0A1B00-4D2F-4A7B-9C31-0E5F2A6B7C8D
 *   5E0A1B01-...  Preset      (read/write, 1 byte: eq_preset_t 0..3)
 *   5E0A1B02-...  Band gains  (read/write, 5 x int8 em passos de 0.5 dB, -24..+24; vira preset CUSTOM)
 * Sem criptografia (alteracoes so de EQ); trocar por PERM_*_ENCRYPTED se isso virar requisito.
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Registra o servico no bt_ble. Chamar depois de bt_ble_init(). */
esp_err_t bt_eq_service_init(void);

#ifdef __cplusplus
}
#endif

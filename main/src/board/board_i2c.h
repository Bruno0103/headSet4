/**
 * @file board_i2c.h
 * @brief Barramento I2C unico da placa (WM8960 0x1A + APDS-9930 0x39).
 *
 * O driver i2c_master do ESP-IDF ja serializa as transacoes por barramento,
 * entao varios modulos podem usar o mesmo handle em tasks diferentes.
 */
#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Cria o barramento (idempotente). */
esp_err_t board_i2c_init(void);

/** Handle do barramento; NULL se board_i2c_init() ainda nao foi chamado. */
i2c_master_bus_handle_t board_i2c_get_bus(void);

/** Adiciona um dispositivo de 7 bits ao barramento. */
esp_err_t board_i2c_add_device(uint8_t addr, uint32_t scl_hz, i2c_master_dev_handle_t *out);

#ifdef __cplusplus
}
#endif

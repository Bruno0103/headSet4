/**
 * @file board_button.h
 * @brief Botao da placa: GPIO + ISR -> fila -> debounce -> eventos SENSOR_EVT.
 *
 *   clique curto (< 1,5 s)  -> SENSOR_EVT_BUTTON_SHORT
 *   pressao longa (>= 3 s)  -> SENSOR_EVT_BUTTON_LONG (disparado ao atingir 3 s)
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Requer hs_events_init() previo. */
esp_err_t board_button_init(void);

#ifdef __cplusplus
}
#endif

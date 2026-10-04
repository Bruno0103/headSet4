/**
 * @file board_button.h
 * @brief Botao da placa: GPIO + ISR -> fila -> debounce -> eventos HEADSET_EVENT.
 *
 *   clique curto (< 1,5 s)  -> HEADSET_EVT_BUTTON_SWITCH
 *   pressao longa (>= 3 s)  -> HEADSET_EVT_BUTTON_PAIRING (disparado ao atingir 3 s)
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Requer headset_events_init() previo. */
esp_err_t board_button_init(void);

#ifdef __cplusplus
}
#endif

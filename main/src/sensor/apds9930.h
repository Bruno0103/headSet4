/**
 * @file apds9930.h
 * @brief Sensor de proximidade APDS-9930 (uso/retirada do fone).
 *
 * O driver NUNCA chama o Bluetooth: apenas publica HEADSET_EVT_WORN /
 * HEADSET_EVT_REMOVED no barramento de eventos. Histerese + debounce evitam
 * transicoes falsas (ajuste fino em menuconfig -> Headset).
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Inicializa o sensor no barramento compartilhado e inicia a task de leitura.
 * Requer board_i2c_init() e hs_events_init(). Se o sensor nao responder,
 * publica WORN (assume fone em uso, para o BT funcionar sem o sensor) e
 * retorna o erro.
 */
esp_err_t apds9930_start(void);

/** Estado filtrado atual (true = fone na cabeca). */
bool apds9930_is_worn(void);

#ifdef __cplusplus
}
#endif

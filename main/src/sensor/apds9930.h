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

/**
 * @brief Ajusta dinamicamente o limiar de sensibilidade de proximidade (on_threshold).
 *
 * Atualiza os parâmetros de histerese em tempo de execução sem reiniciar a task:
 * - on_delta = on_threshold
 * - off_delta = (on_threshold * 6) / 10 (histerese dinâmica conforme AGENTS.md §0.1).
 *
 * @param on_threshold Novo limiar de proximidade (ex: 10 a 200 contagens).
 * @return ESP_OK se o valor foi aceito.
 */
esp_err_t apds9930_set_threshold(uint16_t on_threshold);

/**
 * @brief Habilita ou desabilita o funcionamento do sensor de proximidade.
 *
 * Quando desabilitado (enabled = false):
 * - Assume imediatamente que o fone está na cabeça (s_worn = true) para não interromper a mídia.
 * - Suspende a emissão de eventos WORN/REMOVED.
 * - Coloca o sensor em modo de baixo consumo (power down do I2C).
 *
 * @param enabled true para ativar a detecção óptica, false para desativar.
 * @return ESP_OK se o estado foi aplicado com sucesso.
 */
esp_err_t apds9930_set_enabled(bool enabled);

/**
 * @brief Retorna se o sensor de proximidade está atualmente ativo.
 *
 * @return true se o sensor estiver operando ativamente.
 */
bool apds9930_is_enabled(void);

#ifdef __cplusplus
}
#endif

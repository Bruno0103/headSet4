/**
 * @file actuators.h
 * @brief Definição da interface e ciclo de vida do Actor Actuators (Vibracall e PWM).
 *
 * Em conformidade com AGENTS.md (WP 4.2 / WP 5.2 / §4.2 / §4.3):
 * - Actor sob demanda (Core 0, Prioridade 3, idle_ms = 5000).
 * - Dono exclusivo do hardware LEDC PWM para o motor de vibração (Vibracall).
 * - Monitora eventos do barramento central (SENSOR_EVT, BT_EVT) para acionar
 *   o vibracall automaticamente quando houver notificação, chamada ou conexão no dispositivo ativo.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "hs_actor.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Inicializa a infraestrutura do Actor Actuators e registra os handlers de eventos.
 *
 * Cria o ator no modo sob demanda (idle_ms = 5000), de modo que a task só é alocada
 * quando houver necessidade de vibração ou ajuste de atuador, liberando memória após repouso.
 *
 * @return ESP_OK em caso de sucesso.
 */
esp_err_t actuators_init(void);

/**
 * @brief Retorna o handle do ator actuators (hs_actor_t).
 */
hs_actor_t *actuators_get_actor(void);

/**
 * @brief Dispara uma vibração háptica no motor Vibracall.
 *
 * Envia o comando ACTUATOR_CMD_VIBRATE para o ator de forma não-bloqueante.
 * Caso o ator esteja inativo, a task é instanciada sob demanda automaticamente.
 *
 * @param duration_ms Duração da vibração em milissegundos.
 * @param intensity_percent Intensidade PWM (0 a 100%). Se 0, utiliza a intensidade configurada no NVS.
 * @return ESP_OK se o comando foi enfileirado com sucesso.
 */
esp_err_t actuators_vibrate(uint16_t duration_ms, uint8_t intensity_percent);

#ifdef __cplusplus
}
#endif

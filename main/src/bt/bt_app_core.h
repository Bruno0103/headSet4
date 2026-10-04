#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Callback function type for dispatched events
 */
typedef void (* bt_app_cb_t) (uint16_t event, void *param);

/**
 * @brief Callback function type to copy parameters
 */
typedef void (* bt_app_copy_cb_t) (void *p_dest, void *p_src, int p_len);

/**
 * @brief Callback function type to free parameters
 */
typedef void (* bt_app_free_cb_t) (void *p_params);

/**
 * @brief Inicializa a task e a fila de mensagens do app Bluetooth
 */
void bt_app_task_start_up(void);

/**
 * @brief Desliga a task e a fila de mensagens do app Bluetooth
 */
void bt_app_task_shut_down(void);

/**
 * @brief Despacha um evento (trabalho) para ser executado na task do app Bluetooth
 *
 * Esta função utiliza internamente as filas do FreeRTOS para transferir o evento
 * da task nativa do Bluedroid para a task da aplicação, evitando travamentos na stack.
 *
 * @param  p_cback       Callback a ser executado na task do app
 * @param  event         ID do evento a ser repassado para o callback
 * @param  p_params      Ponteiro para os parâmetros do evento (serão copiados)
 * @param  param_len     Tamanho em bytes dos parâmetros
 * @param  p_copy_cback  (Opcional) Callback customizado para cópia profunda
 * @param  p_free_cback  (Opcional) Callback customizado para limpeza dos parâmetros copiados
 *
 * @return true se enfileirado com sucesso, false caso contrário
 */
bool bt_app_work_dispatch(bt_app_cb_t p_cback, uint16_t event, void *p_params, int param_len, bt_app_copy_cb_t p_copy_cback, bt_app_free_cb_t p_free_cback);

#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Máquina de estados principal de conexão BT.
 */
typedef enum {
    BT_CONN_ST_INIT,
    BT_CONN_ST_PAIRING,
    BT_CONN_ST_RECONNECTING,
    BT_CONN_ST_CONNECTED
} bt_conn_state_t;

/**
 * @brief Eventos alimentados à FSM a partir da BT_APP.
 * @note Não bloqueantes.
 */
void bt_conn_on_gap_evt(uint32_t evt, void *param);
void bt_conn_on_a2dp_evt(uint32_t evt, void *param);
void bt_conn_on_hfp_evt(uint32_t evt, void *param);

/**
 * @brief Comandos públicos.
 */
void bt_conn_enter_pair_mode(void);
void bt_conn_clear_bonds(void);
bt_conn_state_t bt_conn_get_state(void);

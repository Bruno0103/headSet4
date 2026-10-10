/**
 * @file bt_link_mgr.h
 * @brief Gerenciador de dois slots de dispositivo com UM unico link A2DP ativo.
 *
 * Cada slot tem estado SLEEP/ACTIVE. Somente o slot selecionado pode estar ACTIVE, e so
 * com o fone na cabeca. Fone retirado -> Classic desligado + BLE lento (SLEEP); recolocado
 * -> Classic volta e reconecta com backoff. Decide a politica de SSP (so aceita em pareamento).
 * Tudo e orientado a eventos do HEADSET_EVENT; nenhum driver chama o BT diretamente.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BT_LINK_NUM_SLOTS 2

typedef enum {
    BT_LINK_SLEEP = 0,
    BT_LINK_ACTIVE,
} bt_link_state_t;

/** Carrega os slots do NVS, assina os eventos e aplica o estado inicial. Chamar na BtAppTask apos o stack subir. */
esp_err_t bt_link_mgr_start(void);

bool            bt_link_mgr_pairing_active(void);
int             bt_link_mgr_selected(void);
bt_link_state_t bt_link_mgr_state(int slot);

/** Referencia (1-based) da chave de conta Fast Pair associada ao slot selecionado; 0 = nenhuma. */
void bt_link_mgr_set_account_ref(uint8_t ref);

/** Volume (0..127) do slot selecionado; a gravacao no NVS e adiada para nao competir com o streaming. */
uint8_t bt_link_mgr_get_volume(void);
void    bt_link_mgr_set_volume(uint8_t volume);

/**
 * @brief Retorna a instância do Actor bt_link (Core 0, prioridade 6).
 */
struct hs_actor *bt_link_actor_get(void);

#ifdef __cplusplus
}
#endif


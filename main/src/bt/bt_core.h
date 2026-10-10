/**
 * @file bt_core.h
 * @brief Sobe o controlador (BTDM) + Bluedroid e inicia os modulos BT na ordem certa.
 *
 *   bt_avrcp / bt_a2dp / bt_hfp   perfis Classic
 *   bt_gap                        politica Classic (SSP, visibilidade, bonds)
 *   bt_fastpair + bt_ble          advertising/GATT BLE (Google Fast Pair)
 *   bt_link_mgr                   2 slots, SLEEP/ACTIVE, botao e sensor
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Requer hs_events_init() e audio_init() previos. Retorna apos subir a pilha;
 *  os modulos sao iniciados de forma assincrona na task BtAppTask. */
esp_err_t bt_core_init(void);

#ifdef __cplusplus
}
#endif

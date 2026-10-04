/**
 * @file bt_ble.h
 * @brief BLE: advertising por perfis (runtime), registro de servicos GATT e privacidade (RPA).
 *
 * Perfis:
 *  - PAIRING : descobrivel, ~100 ms, endereco publico fixo (Fast Pair exige que nao rotacione)
 *  - ACTIVE  : conectavel, intervalo moderado (mais lento durante streaming A2DP)
 *  - SLEEP   : 1.28 s, usado com o Classic desligado enquanto o fone esta fora da cabeca
 *  Em ACTIVE/SLEEP o endereco e um RPA (privacidade) e o payload vem do provider (Fast Pair).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_gatts_api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BT_BLE_ADV_OFF = 0,
    BT_BLE_ADV_PAIRING,
    BT_BLE_ADV_ACTIVE,
    BT_BLE_ADV_SLEEP,
} bt_ble_profile_t;

/** Potencia de TX do advertising (dBm), tambem anunciada no campo TX Power do Fast Pair. */
#define BT_BLE_ADV_TX_POWER_DBM 3

/**
 * Fornecedor de AD structures extras (ex.: service data 0xFE2C do Fast Pair).
 * Escreve ate `max` bytes em `out` e retorna quantos escreveu (0 = nada).
 * Chamado na task do Bluedroid a cada (re)configuracao do advertising.
 */
typedef size_t (*bt_ble_adv_provider_t)(bt_ble_profile_t profile, uint8_t *out, size_t max);

/** Registra callbacks GAP/GATTS, liga a privacidade local e ajusta a potencia de TX. Nao anuncia ainda. */
esp_err_t bt_ble_init(const char *device_name);

void bt_ble_set_adv_provider(bt_ble_adv_provider_t provider);

/** Troca o perfil de advertising (para/reconfigura/reinicia sozinho). Seguro de qualquer task. */
esp_err_t bt_ble_set_profile(bt_ble_profile_t profile);
bt_ble_profile_t bt_ble_get_profile(void);

/** Durante streaming A2DP o advertising ACTIVE fica mais lento para nao disputar o radio. */
void bt_ble_set_streaming(bool streaming);

/** Forca nova montagem do payload (ex.: chaves de conta mudaram, filtro de Bloom precisa refazer). */
void bt_ble_refresh_adv(void);

/** Registra um servico GATT (app_id unico). O cb recebe todos os eventos do seu gatts_if. */
esp_err_t bt_ble_gatts_register(uint16_t app_id, esp_gatts_cb_t cb);

#ifdef __cplusplus
}
#endif

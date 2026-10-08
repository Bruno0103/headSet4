/**
 * @file headset_events.h
 * @brief Shim de compatibilidade com o barramento legado do Headset (HEADSET_EVENT).
 *
 * NOTA DE MIGRAÇÃO (WP 1.1 - Strangler Fig pattern):
 * Este arquivo atua como camada de adaptação (shim/aliases) para o novo barramento
 * hs_events.h, garantindo que todo o código legado existente continue compilando
 * e funcionando sem quebras até que a Fase 9 conclua a migração completa.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_bt_defs.h"
#include "esp_err.h"
#include "esp_event.h"

/* Inclui o novo contrato oficial */
#include "hs_events.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Base de eventos legada mantida para compatibilidade retroativa.
 */
ESP_EVENT_DECLARE_BASE(HEADSET_EVENT);

/**
 * @brief Enumeração de IDs de eventos legados mapeados 1-para-1.
 */
typedef enum {
    HEADSET_EVT_WORN = 0,        /**< Sensor: fone colocado na cabeca (sem payload)          */
    HEADSET_EVT_REMOVED,         /**< Sensor: fone retirado da cabeca (sem payload)          */
    HEADSET_EVT_BUTTON_SWITCH,   /**< Botao: clique curto -> alterna o slot ativo (sem payload) */
    HEADSET_EVT_BUTTON_PAIRING,  /**< Botao: pressao longa -> pedido de modo de pareamento   */
    HEADSET_EVT_LINK_UP,         /**< BT: perfil conectado   (payload headset_link_evt_t)    */
    HEADSET_EVT_LINK_DOWN,       /**< BT: perfil desconectado (payload headset_link_evt_t)   */
    HEADSET_EVT_PAIRING_MODE,    /**< link_mgr: janela de pareamento (payload headset_pairing_evt_t) */
    HEADSET_EVT_STREAMING,       /**< A2DP: inicio/fim de stream (payload headset_streaming_evt_t) */
    HEADSET_EVT_BATTERY,         /**< battery: nivel mudou (payload headset_battery_evt_t)   */
} headset_event_id_t;

/* Aliases de perfis legados */
typedef enum {
    HEADSET_PROFILE_A2DP = BT_PROFILE_A2DP,
    HEADSET_PROFILE_HFP  = BT_PROFILE_HFP,
} headset_profile_t;

/* Estruturas legadas mapeadas para manter compatibilidade binária exata */
typedef struct {
    esp_bd_addr_t bda;
    uint8_t       profile;   /**< headset_profile_t */
} headset_link_evt_t;

typedef struct {
    bool active;
} headset_pairing_evt_t;

typedef struct {
    bool streaming;
} headset_streaming_evt_t;

typedef struct {
    uint8_t  percent;       /**< 0..100 */
    uint16_t millivolts;    /**< tensao real da bateria */
} headset_battery_evt_t;

/** Cria o loop de eventos centralizado através de hs_events_init(). Idempotente. */
esp_err_t headset_events_init(void);

/** Publica um evento no laço central (também traduz para a nova base). */
esp_err_t headset_event_post(headset_event_id_t id, const void *data, size_t size);

/** Ultimo estado WORN/REMOVED publicado. Retorna false se o sensor ainda nao reportou nada. */
bool headset_events_get_worn(bool *worn);

/** Assina um evento especifico no loop dedicado. */
esp_err_t headset_event_register(headset_event_id_t id, esp_event_handler_t handler, void *arg);

#ifdef __cplusplus
}
#endif

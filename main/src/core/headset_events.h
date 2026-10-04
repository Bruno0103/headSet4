/**
 * @file headset_events.h
 * @brief Barramento de eventos do headset (esp_event dedicado).
 *
 * Regra de projeto: sensores/botao/BT apenas PUBLICAM eventos aqui; quem toma
 * decisoes (bt_link_mgr) apenas ASSINA. Assim o driver do APDS-9930 nunca chama
 * a pilha Bluetooth diretamente.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_bt_defs.h"
#include "esp_err.h"
#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

ESP_EVENT_DECLARE_BASE(HEADSET_EVENT);

typedef enum {
    HEADSET_EVT_WORN = 0,        /**< Sensor: fone colocado na cabeca (sem payload)          */
    HEADSET_EVT_REMOVED,         /**< Sensor: fone retirado da cabeca (sem payload)          */
    HEADSET_EVT_BUTTON_SWITCH,   /**< Botao: clique curto -> alterna o slot ativo (sem payload) */
    HEADSET_EVT_BUTTON_PAIRING,  /**< Botao: pressao longa -> pedido de modo de pareamento   */
    HEADSET_EVT_LINK_UP,         /**< BT: perfil conectado   (payload headset_link_evt_t)    */
    HEADSET_EVT_LINK_DOWN,       /**< BT: perfil desconectado (payload headset_link_evt_t)   */
    HEADSET_EVT_PAIRING_MODE,    /**< link_mgr: janela de pareamento (payload headset_pairing_evt_t) */
    HEADSET_EVT_STREAMING,       /**< A2DP: inicio/fim de stream (payload headset_streaming_evt_t) */
} headset_event_id_t;

typedef enum {
    HEADSET_PROFILE_A2DP = 1 << 0,
    HEADSET_PROFILE_HFP  = 1 << 1,
} headset_profile_t;

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

/** Cria o loop de eventos dedicado (task propria no core 0). Idempotente. */
esp_err_t headset_events_init(void);

/** Publica um evento (copia o payload). Nao bloqueia; seguro fora de ISR. */
esp_err_t headset_event_post(headset_event_id_t id, const void *data, size_t size);

/** Ultimo estado WORN/REMOVED publicado. Retorna false se o sensor ainda nao reportou nada. */
bool headset_events_get_worn(bool *worn);

/** Assina um evento especifico do loop dedicado. */
esp_err_t headset_event_register(headset_event_id_t id, esp_event_handler_t handler, void *arg);

#ifdef __cplusplus
}
#endif

#pragma once
/**
 * @file sfx.h
 * @brief Efeitos sonoros curtos e de alta fidelidade (síntese acústica orgânica) para eventos do Headset4.
 *
 * Catálogo de efeitos sonoros rápidos, elegantes e cristalinos (UX Sound Design)
 * sintetizados em tempo real pelo subsistema de áudio (audio_io).
 */

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SFX_WORN = 0,       /**< Fone colocado na cabeça (sensor de proximidade APDS). */
    SFX_REMOVED,        /**< Fone retirado da cabeça. */
    SFX_CONNECTED,      /**< Smartphone conectado via Bluetooth. */
    SFX_DISCONNECTED,   /**< Smartphone desconectado via Bluetooth. */
    SFX_PAIRING,        /**< Modo de pareamento Bluetooth aberto / em busca. */
    SFX_BATTERY_LOW,    /**< Alerta discreto de bateria fraca (<= 15%). */
    SFX_VOLUME_LIMIT,   /**< Limite de volume (máximo ou mínimo atingido). */
    SFX_MAX
} sfx_id_t;

/**
 * @brief Inicializa a task de fila de efeitos sonoros e registra os handlers no barramento central hs_events.
 *
 * @return esp_err_t ESP_OK em caso de sucesso ou código de erro FreeRTOS.
 */
esp_err_t sfx_init(void);

/**
 * @brief Enfileira um efeito sonoro para reprodução assíncrona imediata.
 *
 * Não bloqueia a task chamadora (descarta se a fila estiver cheia).
 *
 * @param id Identificador do efeito sonoro desejado.
 */
void sfx_play(sfx_id_t id);

#ifdef __cplusplus
}
#endif

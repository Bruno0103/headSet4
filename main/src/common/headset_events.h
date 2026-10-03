#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Tópicos (categorias) de eventos trafegados no barramento principal.
 */
typedef enum {
    HEADSET_EVT_TOPIC_BT_CONN,    ///< Mudança de estado da conexão BT
    HEADSET_EVT_TOPIC_AUDIO,      ///< Eventos de áudio (codec/volume/estado)
    HEADSET_EVT_TOPIC_SYS         ///< Bateria, watchdog, botões
} headset_evt_topic_t;

/**
 * @brief Struct universal do barramento de eventos.
 */
typedef struct {
    headset_evt_topic_t topic;
    uint32_t event_id;
    union {
        struct {
            uint8_t bd_addr[6];
            bool is_connected;
        } bt_conn;
        struct {
            uint8_t volume;
            bool is_muted;
        } audio;
        uint32_t val;
    } data;
} headset_evt_t;

/**
 * @brief Envia um evento para o barramento principal.
 * @note Thread-safe, não bloqueante se houver timeout = 0. Chamável a partir de tasks (não de ISR).
 * @param evt Evento a ser enviado.
 * @return true se enviado com sucesso.
 */
bool headset_evt_post(const headset_evt_t *evt);

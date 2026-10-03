#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Estrutura que espelha as configurações guardadas em NVS.
 */
typedef struct {
    uint8_t last_peer_bd_addr[6];
    bool has_last_peer;
    uint8_t volume_music;
    uint8_t volume_call;
    uint32_t flags;
} headset_settings_t;

/**
 * @brief Carrega settings do NVS (ou inicializa default).
 */
void settings_init(void);

/**
 * @brief Lê a configuração atual (memória).
 */
void settings_get(headset_settings_t *out_settings);

/**
 * @brief Salva uma nova configuração de forma persistente (NVS commit).
 * @note Operação bloqueante, lenta (escrita em flash). Não usar na audio_task.
 */
void settings_save(const headset_settings_t *new_settings);

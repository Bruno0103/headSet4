#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    AUDIO_MODE_IDLE,
    AUDIO_MODE_MUSIC,
    AUDIO_MODE_CALL
} audio_mode_t;

typedef enum {
    AUDIO_CMD_START,
    AUDIO_CMD_STOP,
    AUDIO_CMD_MUTE,
    AUDIO_CMD_VOLUME,
    AUDIO_CMD_SIDETONE
} audio_cmd_type_t;

/**
 * @brief Comando a ser enfileirado para a `audio_io_task`.
 */
typedef struct {
    audio_cmd_type_t type;
    audio_mode_t mode;      // Usado em START/STOP
    uint32_t sample_rate;   // Usado em START
    uint8_t volume;         // Usado em VOLUME
    bool sidetone_on;       // Usado em SIDETONE
} audio_cmd_t;

/**
 * @brief Envia comando para a task de áudio.
 * @note Thread-safe, não bloqueante. Pode ser chamado pelos callbacks do BT_APP.
 * @return true se inserido na fila.
 */
bool audio_io_post_cmd(const audio_cmd_t *cmd);

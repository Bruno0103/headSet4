#pragma once
/* Efeitos sonoros curtos (tons sintetizados) para eventos do fone. */
#include "esp_err.h"

typedef enum {
    SFX_WORN = 0,       /* fone colocado   */
    SFX_REMOVED,        /* fone retirado   */
    SFX_CONNECTED,      /* celular conectado */
    SFX_DISCONNECTED,   /* celular desconectado */
} sfx_id_t;

esp_err_t sfx_init(void);

/** Enfileira um efeito; nao bloqueia (descarta se a fila estiver cheia). */
void sfx_play(sfx_id_t id);

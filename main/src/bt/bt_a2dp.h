#pragma once
/* A2DP sink (musica, SBC) -> audio_io */
#include <stdbool.h>
#include "esp_bt_defs.h"
#include "esp_err.h"

/** Registra os callbacks e inicializa o sink. Chamar no contexto da task BT_APP, com a pilha ja ativa. */
esp_err_t bt_a2dp_start(void);
esp_err_t bt_a2dp_connect(esp_bd_addr_t remote);
esp_err_t bt_a2dp_disconnect(esp_bd_addr_t remote);

/** true se o celular esta com o stream A2DP em andamento. */
bool bt_a2dp_is_streaming(void);

/** Religa o audio de musica (usado ao fim de uma chamada). */
void bt_a2dp_resume_audio(void);

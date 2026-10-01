#pragma once
/* HFP Client: chamadas de voz (passos 4 e 5). Exige CONFIG_BT_HFP_CLIENT_ENABLE
 * e CONFIG_BT_HFP_AUDIO_DATA_PATH_HCI; sem isso o modulo vira "no-op". */
#include "esp_bt_defs.h"
#include "esp_err.h"

esp_err_t bt_hfp_start(void);

/** Pede conexao HFP ao aparelho (ignora erro se ele ja conectou por conta propria). */
void bt_hfp_connect(esp_bd_addr_t remote);

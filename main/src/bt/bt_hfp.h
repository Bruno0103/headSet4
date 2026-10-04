#pragma once
/* HFP Client: chamadas de voz. */
#include "esp_bt_defs.h"
#include "esp_err.h"

esp_err_t bt_hfp_start(void);

void bt_hfp_connect(esp_bd_addr_t remote);
void bt_hfp_disconnect(esp_bd_addr_t remote);

esp_err_t bt_hfp_answer_call(void);
esp_err_t bt_hfp_reject_call(void);
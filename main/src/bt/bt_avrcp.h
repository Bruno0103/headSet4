#pragma once
/* AVRCP Target: recebe o volume absoluto do celular -> volume do WM8960 (passo 2) */
#include <stdint.h>
#include "esp_err.h"

esp_err_t bt_avrcp_start(void);

/** Volume iniciado localmente (botoes/UI): aplica no codec e avisa o celular. 0..127 */
void bt_avrcp_set_volume(uint8_t volume);

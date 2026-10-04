#pragma once
/* AVRCP Target: recebe o volume absoluto do celular -> volume do WM8960 */
#include "esp_err.h"
#include <stdint.h>


esp_err_t bt_avrcp_start(void);

/** Volume iniciado localmente (botoes/UI): aplica no codec e avisa o celular.
 * 0..127 */
void bt_avrcp_set_volume(uint8_t volume);

esp_err_t bt_avrcp_send_play(void);
esp_err_t bt_avrcp_send_pause(void);
esp_err_t bt_avrcp_send_next(void);
esp_err_t bt_avrcp_send_prev(void);

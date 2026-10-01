#include "bt_avrcp.h"

#include <stdbool.h>

#include "esp_avrc_api.h"
#include "esp_check.h"
#include "esp_log.h"

#include "bt_app_core_utils.h"

#include "audio_codec.h"
#include "board_config.h"

static const char *TAG = "bt_avrcp";

static uint8_t s_volume = CODEC_DEFAULT_VOLUME;
static bool    s_notify_registered;      /* o celular pediu para ser avisado de mudancas */

static void send_volume_rsp(esp_avrc_rn_rsp_t rsp)
{
    esp_avrc_rn_param_t rn = { .volume = s_volume };
    esp_avrc_tg_send_rn_rsp(ESP_AVRC_RN_VOLUME_CHANGE, rsp, &rn);
}

/* Roda na task BT_APP */
static void avrcp_evt_hdl(uint16_t event, void *p)
{
    esp_avrc_tg_cb_param_t *rc = p;

    switch (event) {
    case ESP_AVRC_TG_SET_ABSOLUTE_VOLUME_CMD_EVT:            /* celular mudou o volume */
        s_volume = rc->set_abs_vol.volume;
        audio_codec_set_volume(s_volume);
        break;

    case ESP_AVRC_TG_REGISTER_NOTIFICATION_EVT:
        if (rc->reg_ntf.event_id == ESP_AVRC_RN_VOLUME_CHANGE) {
            s_notify_registered = true;
            send_volume_rsp(ESP_AVRC_RN_RSP_INTERIM);        /* informa o volume atual */
        }
        break;

    default:
        break;
    }
}

static void avrcp_tg_cb(esp_avrc_tg_cb_event_t event, esp_avrc_tg_cb_param_t *param)
{
    bt_app_work_dispatch(avrcp_evt_hdl, event, param, sizeof(esp_avrc_tg_cb_param_t), NULL, NULL);
}

void bt_avrcp_set_volume(uint8_t volume)
{
    s_volume = volume > 127 ? 127 : volume;
    audio_codec_set_volume(s_volume);
    if (s_notify_registered) {                               /* a resposta CHANGED consome o registro */
        s_notify_registered = false;
        send_volume_rsp(ESP_AVRC_RN_RSP_CHANGED);
    }
}

esp_err_t bt_avrcp_start(void)
{
    ESP_RETURN_ON_ERROR(esp_avrc_tg_init(), TAG, "tg init");
    ESP_RETURN_ON_ERROR(esp_avrc_tg_register_callback(avrcp_tg_cb), TAG, "tg cb");

    esp_avrc_rn_evt_cap_mask_t caps = {0};
    esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &caps, ESP_AVRC_RN_VOLUME_CHANGE);
    ESP_RETURN_ON_ERROR(esp_avrc_tg_set_rn_evt_cap(&caps), TAG, "rn cap");
    return ESP_OK;
}

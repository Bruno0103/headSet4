#include "bt_avrcp.h"

#include <stdbool.h>

#include "esp_avrc_api.h"
#include "esp_check.h"
#include "esp_log.h"

#include "bt_app_core.h"

#include "audio_codec.h"
#include "board_config.h"

static const char *TAG = "bt_avrcp";

static uint8_t s_volume = CODEC_DEFAULT_VOLUME;
static bool    s_notify_registered;      /* o celular pediu para ser avisado de mudancas */
static bool    s_ct_connected;
static bool    s_peer_supports_abs_vol;

static esp_err_t send_volume_rsp(esp_avrc_rn_rsp_t rsp)
{
    esp_avrc_rn_param_t rn = { .volume = s_volume };
    esp_err_t err = esp_avrc_tg_send_rn_rsp(ESP_AVRC_RN_VOLUME_CHANGE, rsp, &rn);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "falha ao enviar notificacao de volume: %s", esp_err_to_name(err));
    }
    return err;
}

static esp_err_t set_volume(uint8_t requested_volume, const char *source)
{
    uint8_t volume = requested_volume > 127 ? 127 : requested_volume;
    if (volume != requested_volume) {
        ESP_LOGW(TAG, "%s enviou volume fora da faixa (%u); limitando a 127",
                 source, (unsigned)requested_volume);
    }

    esp_err_t err = audio_codec_set_volume(volume);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "falha ao aplicar volume %u recebido de %s: %s",
                 (unsigned)volume, source, esp_err_to_name(err));
        return err;
    }

    s_volume = volume;
    ESP_LOGI(TAG, "volume aplicado (%s): %u/127", source, (unsigned)s_volume);
    return ESP_OK;
}

/* Roda na task BT_APP */
static void avrcp_evt_hdl(uint16_t event, void *p)
{
    esp_avrc_tg_cb_param_t *rc = p;

    switch (event) {
    case ESP_AVRC_TG_CONNECTION_STATE_EVT:
        if (!rc->conn_stat.connected) {
            s_notify_registered = false;
            ESP_LOGI(TAG, "conexao AVRCP encerrada; notificacao pendente removida");
        }
        break;

    case ESP_AVRC_TG_SET_ABSOLUTE_VOLUME_CMD_EVT:            /* celular mudou o volume */
        if (s_peer_supports_abs_vol) {
            set_volume(rc->set_abs_vol.volume, "celular");
        } else {
            ESP_LOGW(TAG, "Ignorando set absolute volume: remote features nao indicam suporte");
        }
        break;

    case ESP_AVRC_TG_REGISTER_NOTIFICATION_EVT:
        if (rc->reg_ntf.event_id == ESP_AVRC_RN_VOLUME_CHANGE) {
            s_notify_registered = true;
            if (send_volume_rsp(ESP_AVRC_RN_RSP_INTERIM) != ESP_OK) {
                s_notify_registered = false;
            }                                                  /* informa o volume atual */
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

/* Roda na task BT_APP */
static void avrcp_ct_evt_hdl(uint16_t event, void *p)
{
    esp_avrc_ct_cb_param_t *rc = p;

    switch (event) {
    case ESP_AVRC_CT_CONNECTION_STATE_EVT:
        s_ct_connected = rc->conn_stat.connected;
        if (s_ct_connected) {
            ESP_LOGI(TAG, "AVRCP CT Conectado, pedindo metadados...");
            esp_avrc_ct_send_metadata_cmd(0, ESP_AVRC_MD_ATTR_TITLE | ESP_AVRC_MD_ATTR_ARTIST | ESP_AVRC_MD_ATTR_ALBUM);
        }
        break;

    case ESP_AVRC_CT_METADATA_RSP_EVT:
        ESP_LOGI(TAG, "Metadata attr %d: %.*s", rc->meta_rsp.attr_id, rc->meta_rsp.attr_length, rc->meta_rsp.attr_text);
        break;

    case ESP_AVRC_CT_REMOTE_FEATURES_EVT:
        s_peer_supports_abs_vol = ((rc->rmt_feats.feat_mask & ESP_AVRC_FEAT_ADV_CTRL) != 0);
        ESP_LOGI(TAG, "CT Remote features: 0x%04lX, suporta vol: %d", (unsigned long)rc->rmt_feats.feat_mask, s_peer_supports_abs_vol);
        break;

    default:
        break;
    }
}

static void avrcp_ct_cb(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param)
{
    bt_app_work_dispatch(avrcp_ct_evt_hdl, event, param, sizeof(esp_avrc_ct_cb_param_t), NULL, NULL);
}

void bt_avrcp_set_volume(uint8_t volume)
{
    uint8_t previous_volume = s_volume;
    if (set_volume(volume, "local") != ESP_OK) {
        return;
    }
    if (s_notify_registered && s_volume != previous_volume) { /* CHANGED consome o registro */
        s_notify_registered = false;
        send_volume_rsp(ESP_AVRC_RN_RSP_CHANGED);
    }
}

esp_err_t bt_avrcp_start(void)
{
    ESP_RETURN_ON_ERROR(esp_avrc_tg_register_callback(avrcp_tg_cb), TAG, "tg cb");
    ESP_RETURN_ON_ERROR(esp_avrc_tg_init(), TAG, "tg init");

    ESP_RETURN_ON_ERROR(esp_avrc_ct_register_callback(avrcp_ct_cb), TAG, "ct cb");
    ESP_RETURN_ON_ERROR(esp_avrc_ct_init(), TAG, "ct init");

    esp_avrc_rn_evt_cap_mask_t caps = {0};
    esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &caps, ESP_AVRC_RN_VOLUME_CHANGE);
    ESP_RETURN_ON_ERROR(esp_avrc_tg_set_rn_evt_cap(&caps), TAG, "rn cap");
    return ESP_OK;
}

static esp_err_t send_pt_cmd(uint8_t key_code)
{
    if (!s_ct_connected) return ESP_ERR_INVALID_STATE;
    esp_avrc_ct_send_passthrough_cmd(0, key_code, 0); /* PRESSED */
    return esp_avrc_ct_send_passthrough_cmd(0, key_code, 1); /* RELEASED */
}

esp_err_t bt_avrcp_send_play(void) { return send_pt_cmd(ESP_AVRC_PT_CMD_PLAY); }
esp_err_t bt_avrcp_send_pause(void) { return send_pt_cmd(ESP_AVRC_PT_CMD_PAUSE); }
esp_err_t bt_avrcp_send_next(void) { return send_pt_cmd(ESP_AVRC_PT_CMD_FORWARD); }
esp_err_t bt_avrcp_send_prev(void) { return send_pt_cmd(ESP_AVRC_PT_CMD_BACKWARD); }

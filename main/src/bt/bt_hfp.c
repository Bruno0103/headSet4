#include "bt_hfp.h"

#include "sdkconfig.h"

#if CONFIG_BT_HFP_CLIENT_ENABLE

#include "esp_check.h"
#include "esp_hf_client_api.h"
#include "esp_log.h"

#include "bt_app_core_utils.h"

#include "audio_io.h"
#include "bt_a2dp.h"

static const char *TAG = "bt_hfp";

/* ---- dados de audio (contexto da pilha BT: nao bloquear) ---- */

/* voz do interlocutor, PCM 16 bits mono (8 kHz CVSD / 16 kHz mSBC) */
static void hf_incoming_cb(const uint8_t *buf, uint32_t len)
{
    audio_io_call_downlink_push(buf, len);
    esp_hf_client_outgoing_data_ready();           /* o envio do microfone acompanha o recebimento */
}

/* seu microfone: preenche 'sz' bytes */
static uint32_t hf_outgoing_cb(uint8_t *buf, uint32_t sz)
{
    return audio_io_call_uplink_pull(buf, sz);
}

/* ---- eventos (task BT_APP) ---- */

static void hf_evt_hdl(uint16_t event, void *p)
{
    esp_hf_client_cb_param_t *hf = p;

    if (event != ESP_HF_CLIENT_AUDIO_STATE_EVT) return;

    switch (hf->audio_stat.state) {
    case ESP_HF_CLIENT_AUDIO_STATE_CONNECTED:         /* CVSD: banda estreita */
        audio_io_start(AUDIO_IO_CALL, 8000);
        break;
    case ESP_HF_CLIENT_AUDIO_STATE_CONNECTED_MSBC:    /* mSBC: banda larga */
        audio_io_start(AUDIO_IO_CALL, 16000);
        break;
    case ESP_HF_CLIENT_AUDIO_STATE_DISCONNECTED:
        audio_io_stop_mode(AUDIO_IO_CALL);
        bt_a2dp_resume_audio();                       /* volta a musica, se estava tocando */
        break;
    default:
        break;
    }
}

static void hf_cb(esp_hf_client_cb_event_t event, esp_hf_client_cb_param_t *param)
{
    bt_app_work_dispatch(hf_evt_hdl, event, param, sizeof(esp_hf_client_cb_param_t), NULL, NULL);
}

void bt_hfp_connect(esp_bd_addr_t remote)
{
    esp_hf_client_connect(remote);
}

esp_err_t bt_hfp_start(void)
{
    ESP_RETURN_ON_ERROR(esp_hf_client_register_callback(hf_cb), TAG, "register cb");
    ESP_RETURN_ON_ERROR(esp_hf_client_init(), TAG, "init");
    esp_hf_client_register_data_callback(hf_incoming_cb, hf_outgoing_cb);
    return ESP_OK;
}

#else  /* HFP desligado no menuconfig */

esp_err_t bt_hfp_start(void)                { return ESP_OK; }
void      bt_hfp_connect(esp_bd_addr_t r)   { (void)r; }

#endif

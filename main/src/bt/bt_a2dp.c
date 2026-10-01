#include "bt_a2dp.h"

#include "esp_a2dp_api.h"
#include "esp_check.h"
#include "esp_log.h"

#include "a2dp_sink_common_utils.h"      /* bt_a2d_evt_def_hdl */
#include "bt_app_core_utils.h"           /* bt_app_work_dispatch */

#include "audio_io.h"
#include "bt_hfp.h"

static const char *TAG = "bt_a2dp";

static uint32_t s_rate = 44100;
static bool     s_streaming;

/* Contexto da pilha BT: so repassa o PCM (ja decodificado de SBC) para o buffer. */
static void a2dp_data_cb(const uint8_t *data, uint32_t len)
{
    audio_io_music_push(data, len);
}

static uint32_t rate_from_cfg(const esp_a2d_mcc_t *mcc)
{
    if (mcc->type != ESP_A2D_MCT_SBC) return 44100;
    uint8_t sf = mcc->cie.sbc_info.samp_freq;
    if (sf & ESP_A2D_SBC_CIE_SF_48K) return 48000;
    if (sf & ESP_A2D_SBC_CIE_SF_44K) return 44100;
    if (sf & ESP_A2D_SBC_CIE_SF_32K) return 32000;
    return 16000;
}

/* Roda na task BT_APP (pode demorar: mexe em I2C/I2S) */
static void a2dp_evt_hdl(uint16_t event, void *p)
{
    esp_a2d_cb_param_t *a2d = p;

    switch (event) {
    case ESP_A2D_CONNECTION_STATE_EVT:
        if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
            bt_hfp_connect(a2d->conn_stat.remote_bda);       /* chamadas pelo mesmo aparelho */
        } else if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
            s_streaming = false;
            audio_io_stop_mode(AUDIO_IO_MUSIC);
        }
        break;

    case ESP_A2D_AUDIO_CFG_EVT:                              /* chega antes do STARTED */
        s_rate = rate_from_cfg(&a2d->audio_cfg.mcc);
        ESP_LOGI(TAG, "SBC @ %lu Hz", (unsigned long)s_rate);
        break;

    case ESP_A2D_AUDIO_STATE_EVT:
        s_streaming = (a2d->audio_stat.state == ESP_A2D_AUDIO_STATE_STARTED);
        if (s_streaming) audio_io_start(AUDIO_IO_MUSIC, s_rate);
        else             audio_io_stop_mode(AUDIO_IO_MUSIC);
        break;

    default:
        bt_a2d_evt_def_hdl(event, p);                        /* PROF_STATE, delay, etc. */
        break;
    }
}

static void a2dp_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param)
{
    bt_app_work_dispatch(a2dp_evt_hdl, event, param, sizeof(esp_a2d_cb_param_t), NULL, NULL);
}

bool bt_a2dp_is_streaming(void) { return s_streaming; }

void bt_a2dp_resume_audio(void)
{
    if (s_streaming) audio_io_start(AUDIO_IO_MUSIC, s_rate);
}

esp_err_t bt_a2dp_start(void)
{
    ESP_RETURN_ON_ERROR(esp_a2d_register_callback(&a2dp_cb), TAG, "register cb");
    ESP_RETURN_ON_ERROR(esp_a2d_sink_init(), TAG, "sink init");
    ESP_RETURN_ON_ERROR(esp_a2d_sink_register_data_callback(a2dp_data_cb), TAG, "data cb");
    return ESP_OK;
}

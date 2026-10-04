#include "bt_a2dp.h"
#include <string.h>

#include "esp_a2dp_api.h"
#include "esp_check.h"
#include "esp_log.h"

#include "bt_app_core.h"      /* bt_app_work_dispatch */

#include "audio_io.h"
#include "bt_hfp.h"
#include "esp_gap_bt_api.h"
#include "bt_gap.h"

static esp_bd_addr_t s_remote;
static bool s_has_remote;

static const char *TAG = "bt_a2dp";

static uint32_t s_rate = 44100;
static bool s_streaming;

static const char *s_a2d_conn_state_str[] = {"Disconnected", "Connecting", "Connected", "Disconnecting"};
static const char *s_a2d_audio_state_str[] = {"Suspended", "Started"};
#define APP_DELAY_VALUE 50 // 5ms

#include "esp_timer.h"
#define INACTIVITY_TIMEOUT_MS (5 * 60 * 1000) // 5 minutos de inatividade para derrubar o link
static esp_timer_handle_t s_inactivity_timer;

static void inactivity_timer_cb(void *arg)
{
    ESP_LOGI(TAG, "Inatividade detectada (%d ms de silencio), derrubando link Classic (repouso TWS)", INACTIVITY_TIMEOUT_MS);
    bt_gap_disconnect_active();
}

/* Contexto da pilha BT: so repassa o PCM (ja decodificado de SBC) para o buffer. */
static void a2dp_data_cb(const uint8_t *data, uint32_t len)
{
    audio_io_music_push(data, len);
}

static uint32_t rate_from_cfg(const esp_a2d_mcc_t *mcc)
{
    if (mcc->type != ESP_A2D_MCT_SBC)
        return 44100;
    uint8_t sf = mcc->cie.sbc_info.samp_freq;
    if (sf & ESP_A2D_SBC_CIE_SF_48K)
        return 48000;
    if (sf & ESP_A2D_SBC_CIE_SF_44K)
        return 44100;
    if (sf & ESP_A2D_SBC_CIE_SF_32K)
        return 32000;
    return 16000;
}

/* Roda na task BT_APP (pode demorar: mexe em I2C/I2S) */
static void a2dp_evt_hdl(uint16_t event, void *p)
{
    esp_a2d_cb_param_t *a2d = p;

    switch (event)
    {
    case ESP_A2D_CONNECTION_STATE_EVT: {
        uint8_t *bda = a2d->conn_stat.remote_bda;
        ESP_LOGI(TAG, "A2DP connection state: %s, [%02x:%02x:%02x:%02x:%02x:%02x]",
                 s_a2d_conn_state_str[a2d->conn_stat.state], bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
        
        if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED)
        {
            esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
            memcpy(s_remote, a2d->conn_stat.remote_bda, sizeof s_remote);
            s_has_remote = true;
            bt_hfp_connect(a2d->conn_stat.remote_bda);
            
            // Inicia timer de inatividade ao conectar (caso não dê play)
            esp_timer_stop(s_inactivity_timer);
            esp_timer_start_once(s_inactivity_timer, INACTIVITY_TIMEOUT_MS * 1000ULL);
        }
        else if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED)
        {
            esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
            if (s_has_remote && memcmp(a2d->conn_stat.remote_bda, s_remote, sizeof s_remote) != 0)
            {
                break; /* aparelho antigo, ja substituido: nao mexe no audio do atual */
            }
            s_has_remote = false;
            s_streaming = false;
            audio_io_stop_mode(AUDIO_IO_MUSIC);
            
            esp_timer_stop(s_inactivity_timer);
        }
        break;
    }

    case ESP_A2D_AUDIO_CFG_EVT: /* chega antes do STARTED */
        s_rate = rate_from_cfg(&a2d->audio_cfg.mcc);
        ESP_LOGI(TAG, "SBC @ %lu Hz", (unsigned long)s_rate);
        break;

    case ESP_A2D_AUDIO_STATE_EVT:
        ESP_LOGI(TAG, "A2DP audio state: %s", s_a2d_audio_state_str[a2d->audio_stat.state]);
        s_streaming = (a2d->audio_stat.state == ESP_A2D_AUDIO_STATE_STARTED);
        if (s_streaming) {
            audio_io_start(AUDIO_IO_MUSIC, s_rate);
            esp_timer_stop(s_inactivity_timer);
        } else {
            audio_io_stop_mode(AUDIO_IO_MUSIC);
            if (s_has_remote) {
                esp_timer_start_once(s_inactivity_timer, INACTIVITY_TIMEOUT_MS * 1000ULL);
            }
        }
        break;

    case ESP_A2D_PROF_STATE_EVT:
        if (ESP_A2D_INIT_SUCCESS == a2d->a2d_prof_stat.init_state) {
            ESP_LOGI(TAG, "A2DP PROF STATE: Init Complete");
        } else {
            ESP_LOGI(TAG, "A2DP PROF STATE: Deinit Complete");
        }
        break;

    case ESP_A2D_SEP_REG_STATE_EVT:
        if (a2d->a2d_sep_reg_stat.reg_state == ESP_A2D_SEP_REG_SUCCESS) {
            ESP_LOGI(TAG, "A2DP register SEP success, seid: %d", a2d->a2d_sep_reg_stat.seid);
        } else {
            ESP_LOGI(TAG, "A2DP register SEP fail, seid: %d, state: %d", a2d->a2d_sep_reg_stat.seid, a2d->a2d_sep_reg_stat.reg_state);
        }
        break;

    case ESP_A2D_SNK_PSC_CFG_EVT:
        ESP_LOGI(TAG, "protocol service capabilities configured: 0x%x ", a2d->a2d_psc_cfg_stat.psc_mask);
        if (a2d->a2d_psc_cfg_stat.psc_mask & ESP_A2D_PSC_DELAY_RPT) {
            ESP_LOGI(TAG, "Peer device support delay reporting");
        } else {
            ESP_LOGI(TAG, "Peer device unsupported delay reporting");
        }
        break;

    case ESP_A2D_SNK_SET_DELAY_VALUE_EVT:
        if (ESP_A2D_SET_INVALID_PARAMS == a2d->a2d_set_delay_value_stat.set_state) {
            ESP_LOGI(TAG, "Set delay report value: fail");
        } else {
            ESP_LOGI(TAG, "Set delay report value: success, delay_value: %u * 1/10 ms", a2d->a2d_set_delay_value_stat.delay_value);
        }
        break;

    case ESP_A2D_SNK_GET_DELAY_VALUE_EVT:
        ESP_LOGI(TAG, "Get delay report value: delay_value: %u * 1/10 ms", a2d->a2d_get_delay_value_stat.delay_value);
        esp_a2d_sink_set_delay_value(a2d->a2d_get_delay_value_stat.delay_value + APP_DELAY_VALUE);
        break;

    default:
        ESP_LOGE(TAG, "Unhandled A2DP event: %d", event);
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
    if (s_streaming)
        audio_io_start(AUDIO_IO_MUSIC, s_rate);
}

esp_err_t bt_a2dp_start(void)
{
    const esp_timer_create_args_t timer_args = {
        .callback = inactivity_timer_cb,
        .name = "a2dp_inactivity"
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&timer_args, &s_inactivity_timer), TAG, "inactivity timer");

    ESP_RETURN_ON_ERROR(esp_a2d_register_callback(&a2dp_cb), TAG, "register cb");
    ESP_RETURN_ON_ERROR(esp_a2d_sink_init(), TAG, "sink init");
    ESP_RETURN_ON_ERROR(esp_a2d_sink_register_data_callback(a2dp_data_cb), TAG, "data cb");
    return ESP_OK;
}
esp_err_t bt_a2dp_connect(esp_bd_addr_t remote) { return esp_a2d_sink_connect(remote); }
esp_err_t bt_a2dp_disconnect(esp_bd_addr_t remote) { return esp_a2d_sink_disconnect(remote); }

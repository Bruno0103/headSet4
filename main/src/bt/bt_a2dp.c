#include "bt_a2dp.h"
#include <string.h>

#include "esp_a2dp_api.h"
#include "esp_check.h"
#include "esp_log.h"

#include "bt_app_core.h"      /* bt_app_work_dispatch */

#include "audio.h"
#include "audio_data.h"
#include "headset_events.h"

static esp_bd_addr_t s_remote;
static bool s_has_remote;

static const char *TAG = "bt_a2dp";

static uint32_t s_rate = 44100;
static bool s_streaming;

static const char *s_a2d_conn_state_str[] = {"Disconnected", "Connecting", "Connected", "Disconnecting"};
static const char *s_a2d_audio_state_str[] = {"Suspended", "Started"};
#define APP_DELAY_VALUE 50 // 5ms

/* Contexto da pilha BT: plano de dados de áudio contínuo para o ringbuffer de música */
static void a2dp_data_cb(const uint8_t *data, uint32_t len)
{
    audio_data_music_push(data, len);
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

/* Registra a configuracao SBC negociada: quem escolhe bitpool/modo e o celular (source). */
static void log_sbc_cfg(const esp_a2d_mcc_t *mcc)
{
    if (mcc->type != ESP_A2D_MCT_SBC) {
        ESP_LOGW(TAG, "Codec negociado nao e SBC (tipo %d)", mcc->type);
        return;
    }
    const esp_a2d_cie_sbc_t *c = &mcc->cie.sbc_info;
    const char *mode = c->ch_mode == ESP_A2D_SBC_CIE_CH_MODE_JOINT_STEREO ? "joint stereo" :
                       c->ch_mode == ESP_A2D_SBC_CIE_CH_MODE_STEREO       ? "stereo" :
                       c->ch_mode == ESP_A2D_SBC_CIE_CH_MODE_DUAL_CHANNEL ? "dual" : "mono";
    int blocks = c->block_len == ESP_A2D_SBC_CIE_BLOCK_LEN_16 ? 16 : c->block_len == ESP_A2D_SBC_CIE_BLOCK_LEN_12 ? 12 :
                 c->block_len == ESP_A2D_SBC_CIE_BLOCK_LEN_8 ? 8 : 4;
    int bands = c->num_subbands == ESP_A2D_SBC_CIE_NUM_SUBBANDS_8 ? 8 : 4;
    ESP_LOGI(TAG, "SBC: %lu Hz, %s, %d blocos, %d subbandas, %s, bitpool %u..%u",
             (unsigned long)s_rate, mode, blocks, bands,
             c->alloc_mthd == ESP_A2D_SBC_CIE_ALLOC_MTHD_LOUDNESS ? "loudness" : "SNR",
             c->min_bitpool, c->max_bitpool);
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

        /* Quem decide o que fazer com o link e o bt_link_mgr: aqui so se publica o fato. */
        headset_link_evt_t ev = { .profile = HEADSET_PROFILE_A2DP };
        memcpy(ev.bda, bda, sizeof ev.bda);

        if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED)
        {
            memcpy(s_remote, bda, sizeof s_remote);
            s_has_remote = true;
            headset_event_post(HEADSET_EVT_LINK_UP, &ev, sizeof ev);
        }
        else if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED)
        {
            ESP_LOGI(TAG, "Motivo da desconexao: %s",
                     a2d->conn_stat.disc_rsn == ESP_A2D_DISC_RSN_NORMAL ? "normal" : "anormal (perda de sinal)");
            if (s_has_remote && memcmp(bda, s_remote, sizeof s_remote) == 0) {
                s_has_remote = false;
                s_streaming = false;
                /* Envia comando assíncrono para o Actor Audio parar a reprodução */
                audio_cmd_stop_send(0xFF);
                headset_streaming_evt_t st = { .streaming = false };
                headset_event_post(HEADSET_EVT_STREAMING, &st, sizeof st);
            }
            headset_event_post(HEADSET_EVT_LINK_DOWN, &ev, sizeof ev);
        }
        break;
    }

    case ESP_A2D_AUDIO_CFG_EVT: /* chega antes do STARTED */
        s_rate = rate_from_cfg(&a2d->audio_cfg.mcc);
        log_sbc_cfg(&a2d->audio_cfg.mcc);
        break;

    case ESP_A2D_AUDIO_STATE_EVT: {
        ESP_LOGI(TAG, "A2DP audio state: %s", s_a2d_audio_state_str[a2d->audio_stat.state]);
        s_streaming = (a2d->audio_stat.state == ESP_A2D_AUDIO_STATE_STARTED);
        if (s_streaming) {
            /* Envia comando AUDIO_CMD_START_MUSIC para o Actor Audio (Core 1) */
            audio_cmd_start_music_send(s_rate);
        } else {
            /* Envia comando AUDIO_CMD_STOP para o Actor Audio */
            audio_cmd_stop_send(1);
        }
        headset_streaming_evt_t st = { .streaming = s_streaming };
        headset_event_post(HEADSET_EVT_STREAMING, &st, sizeof st);
        break;
    }

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
    if (s_streaming) {
        /* Envia comando assíncrono para o Actor Audio retomar a reprodução na taxa configurada */
        audio_cmd_start_music_send(s_rate);
    }
}

esp_err_t bt_a2dp_start(void)
{
    ESP_RETURN_ON_ERROR(esp_a2d_register_callback(&a2dp_cb), TAG, "register cb");
    ESP_RETURN_ON_ERROR(esp_a2d_sink_init(), TAG, "sink init");
    ESP_RETURN_ON_ERROR(esp_a2d_sink_register_data_callback(a2dp_data_cb), TAG, "data cb");
    return ESP_OK;
}
esp_err_t bt_a2dp_connect(esp_bd_addr_t remote) { return esp_a2d_sink_connect(remote); }
esp_err_t bt_a2dp_disconnect(esp_bd_addr_t remote) { return esp_a2d_sink_disconnect(remote); }

#include "bt_hfp.h"

#include "sdkconfig.h"

#if CONFIG_BT_HFP_CLIENT_ENABLE

#include <string.h>

#include "esp_check.h"
#include "esp_hf_client_api.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "bt_app_core.h"

#include "audio.h"
#include "audio_data.h"
#include "bt_a2dp.h"
#include "bt_link_mgr.h"
#include "hs_events.h"

static const char *TAG = "bt_hfp";

/* Extensao Apple (AT+XAPL / AT+IPHONEACCEV), aceita por iOS, Android e Windows para mostrar a bateria do acessorio.
 * Formato: "<VID hex>-<PID hex>-<versao>" (14 caracteres); VID 303A = Espressif. */
#define XAPL_INFO "303A-0001-0100"

static volatile bool s_slc_up;
static volatile bool s_xapl_sent;
static volatile int  s_last_level = -1;   /* ultimo nivel 0..9 enviado nesta conexao */

/* ---- dados de audio (plano de dados de streaming contínuo PCM: nao bloquear) ---- */

/* voz do interlocutor, PCM 16 bits mono (8 kHz CVSD / 16 kHz mSBC) */
static void hf_incoming_cb(const uint8_t *buf, uint32_t len)
{
    audio_data_call_downlink_push(buf, len);
    esp_hf_client_outgoing_data_ready(); /* o envio do microfone acompanha o recebimento */
}

/* seu microfone: preenche 'sz' bytes a partir do uplink com Voice NR */
static uint32_t hf_outgoing_cb(uint8_t *buf, uint32_t sz)
{
    return audio_data_call_uplink_pull(buf, sz);
}

/* ---- eventos (task BT_APP) ---- */

static void hf_evt_hdl(uint16_t event, void *p)
{
    esp_hf_client_cb_param_t *hf = p;

    switch (event)
    {
    case ESP_HF_CLIENT_CONNECTION_STATE_EVT:
        /* O bt_link_mgr decide se falta abrir o A2DP; aqui so se publica o fato. */
        if (hf->conn_stat.state == ESP_HF_CLIENT_CONNECTION_STATE_SLC_CONNECTED ||
            hf->conn_stat.state == ESP_HF_CLIENT_CONNECTION_STATE_DISCONNECTED) {
            const bool up = hf->conn_stat.state == ESP_HF_CLIENT_CONNECTION_STATE_SLC_CONNECTED;
            s_slc_up = up;
            s_xapl_sent = false;
            s_last_level = -1;
            bt_link_evt_t ev = {
                .slot = 0,
                .profile = BT_PROFILE_HFP,
            };
            memcpy(ev.bda, hf->conn_stat.remote_bda, sizeof(ev.bda));
            ESP_LOGI(TAG, "HFP %s", up ? "conectado (SLC)" : "desconectado");
            hs_event_post(BT_EVT, up ? BT_EVT_LINK_UP : BT_EVT_LINK_DOWN, &ev, sizeof(ev));
        }
        break;

    case ESP_HF_CLIENT_CIND_CALL_EVT:
        if (hf->call.status == ESP_HF_CALL_STATUS_CALL_IN_PROGRESS) {
            ESP_LOGI(TAG, "Chamada em andamento");
        } else {
            ESP_LOGI(TAG, "Sem chamada");
        }
        break;

    case ESP_HF_CLIENT_CIND_CALL_SETUP_EVT:
        if (hf->call_setup.status == ESP_HF_CALL_SETUP_STATUS_INCOMING) {
            ESP_LOGI(TAG, "Chamada recebendo");
            bt_call_state_evt_t ev = {
                .slot = (uint8_t)bt_link_mgr_selected(),
                .state = BT_CALL_INCOMING,
            };
            hs_event_post(BT_EVT, BT_EVT_CALL_STATE, &ev, sizeof(ev));
        } else if (hf->call_setup.status == ESP_HF_CALL_SETUP_STATUS_OUTGOING_DIALING || hf->call_setup.status == ESP_HF_CALL_SETUP_STATUS_OUTGOING_ALERTING) {
            ESP_LOGI(TAG, "Chamada efetuando");
            bt_call_state_evt_t ev = {
                .slot = (uint8_t)bt_link_mgr_selected(),
                .state = BT_CALL_OUTGOING,
            };
            hs_event_post(BT_EVT, BT_EVT_CALL_STATE, &ev, sizeof(ev));
        } else if (hf->call_setup.status == ESP_HF_CALL_SETUP_STATUS_IDLE) {
            bt_call_state_evt_t ev = {
                .slot = (uint8_t)bt_link_mgr_selected(),
                .state = BT_CALL_IDLE,
            };
            hs_event_post(BT_EVT, BT_EVT_CALL_STATE, &ev, sizeof(ev));
        }
        break;

    case ESP_HF_CLIENT_RING_IND_EVT:
        ESP_LOGI(TAG, "Tocando...");
        {
            bt_call_state_evt_t ev = {
                .slot = (uint8_t)bt_link_mgr_selected(),
                .state = BT_CALL_INCOMING,
            };
            hs_event_post(BT_EVT, BT_EVT_CALL_STATE, &ev, sizeof(ev));
        }
        break;

    case ESP_HF_CLIENT_CLIP_EVT:
        ESP_LOGI(TAG, "Numero: %s", hf->clip.number);
        break;

    case ESP_HF_CLIENT_VOLUME_CONTROL_EVT:
        /* Volume de chamada so vale para o alto-falante e NAO e gravado no slot (e do HFP, 0..15) */
        if (hf->volume_control.type == ESP_HF_VOLUME_CONTROL_TARGET_SPK) {
            ESP_LOGI(TAG, "Volume de chamada do celular: %d/15", hf->volume_control.volume);
            uint8_t vol_pct = (uint8_t)(hf->volume_control.volume * 100 / 15);
            audio_cmd_set_volume_send(vol_pct);
        }
        break;

    case ESP_HF_CLIENT_AUDIO_STATE_EVT:
        switch (hf->audio_stat.state)
        {
    case ESP_HF_CLIENT_AUDIO_STATE_CONNECTED: /* CVSD: banda estreita 8 kHz */
        audio_cmd_start_call_send(8000, true);
        break;
    case ESP_HF_CLIENT_AUDIO_STATE_CONNECTED_MSBC: /* mSBC: banda larga 16 kHz */
        audio_cmd_start_call_send(16000, true);
        break;
    case ESP_HF_CLIENT_AUDIO_STATE_DISCONNECTED: {
        /* Para o modo de chamada via comando */
        audio_cmd_stop_send(2);
        /* Devolve o volume da musica do slot */
        uint8_t v = bt_link_mgr_get_volume();
        uint8_t v_pct = (uint8_t)((uint32_t)v * 100 / 127);
        audio_cmd_set_volume_send(v_pct);
        bt_a2dp_resume_audio(); /* volta a musica, se estava tocando */
        break;
    }
        default:
            break;
        }
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

void bt_hfp_disconnect(esp_bd_addr_t remote) { esp_hf_client_disconnect(remote); }

esp_err_t bt_hfp_answer_call(void) { return esp_hf_client_answer_call(); }
esp_err_t bt_hfp_reject_call(void) { return esp_hf_client_reject_call(); }

void bt_hfp_report_battery(uint8_t percent)
{
    if (!s_slc_up) {
        /* Conexão de controle SLC ainda não estabelecida */
        return;
    }

    /* Mapeia 0..100% para a escala 0..9 exigida pelo padrão Apple AT+IPHONEACCEV */
    const int level = ((percent > 100 ? 100 : percent) * 9 + 50) / 100;   /* 0..9 */

    /* Envia o handshake de recursos Apple AT+XAPL caso ainda não tenha sido negociado nesta sessão SLC */
    if (!s_xapl_sent) {
        char info[] = XAPL_INFO;
        esp_err_t err = esp_hf_client_send_xapl(info, ESP_HF_CLIENT_XAPL_FEAT_BATTERY_REPORT);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "XAPL recusado pela pilha (%s) - aguardando proximo ciclo", esp_err_to_name(err));
            return;
        }
        s_xapl_sent = true;
        /* Em conformidade com AGENTS.md §3 (handlers e funções de despacho nunca bloqueiam com vTaskDelay),
         * enviamos a primeira indicação logo a seguir ou no próximo ciclo sem bloquear a task */
    }

    /* 
     * Heartbeat para Windows e hosts Bluetooth:
     * O Windows e alguns sistemas precisam de atualizações periódicas mesmo se o nível
     * permanecer no mesmo degrau (0..9), para confirmar que o dispositivo está ativo
     * e o valor de bateria continua fresco.
     */
    static uint32_t s_last_report_ms = 0;
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    /* Cada leitura (15 s) é repassada ao host; o debounce de 5 s só evita rajadas
     * (ex.: LINK_UP + evento periódico juntos). Mudança de nível sempre envia. */
    bool should_send = (level != s_last_level) || (s_last_report_ms == 0) ||
                       (now_ms - s_last_report_ms >= 5000);

    if (!should_send) {
        return;
    }

    /* Envia o valor atualizado de bateria */
    esp_err_t err_acc = esp_hf_client_send_iphoneaccev((uint32_t)level, false);
    if (err_acc == ESP_OK) {
        s_last_level = level;
        s_last_report_ms = now_ms;
        ESP_LOGI(TAG, "Bateria informada com sucesso ao host HFP: %u%% (nivel %d/9)", percent, level);
    } else {
        ESP_LOGW(TAG, "IPHONEACCEV recusado pela pilha (%s)", esp_err_to_name(err_acc));
    }
}

esp_err_t bt_hfp_start(void)
{
    ESP_RETURN_ON_ERROR(esp_hf_client_register_callback(hf_cb), TAG, "register cb");
    ESP_RETURN_ON_ERROR(esp_hf_client_init(), TAG, "init");
    esp_hf_client_register_data_callback(hf_incoming_cb, hf_outgoing_cb);
    return ESP_OK;
}

#else /* HFP desligado no menuconfig */

esp_err_t bt_hfp_start(void) { return ESP_OK; }
void bt_hfp_connect(esp_bd_addr_t r) { (void)r; }
void bt_hfp_disconnect(esp_bd_addr_t r) { (void)r; }
esp_err_t bt_hfp_answer_call(void) { return ESP_OK; }
esp_err_t bt_hfp_reject_call(void) { return ESP_OK; }
void bt_hfp_report_battery(uint8_t p) { (void)p; }

#endif

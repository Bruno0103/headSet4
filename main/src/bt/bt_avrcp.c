#include "bt_avrcp.h"

#include <stdbool.h>

#include "esp_avrc_api.h"
#include "esp_check.h"
#include "esp_log.h"

#include "bt_app_core.h"
#include "bt_link_mgr.h"

#include "audio.h"
#include "board_config.h"
#include "cover_art.h"
#include "ui_model.h"

static const char *TAG = "bt_avrcp";

static uint8_t s_volume = CODEC_DEFAULT_VOLUME;
static bool    s_notify_registered;      /* o celular pediu para ser avisado de mudancas */
static bool    s_ct_connected;
static bool    s_peer_supports_abs_vol;
static uint8_t s_tl = 0;                 /* Transaction Label sequencial (0..15) per AVCTP v1.4 */

/**
 * @brief Aloca o próximo Transaction Label cíclico de 4 bits para comandos AVRCP.
 */
static inline uint8_t avrcp_next_tl(void)
{
    return (s_tl++ & 0x0F);
}

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

    /* Converte de escala 0..127 para escala 0..100% para o comando AUDIO_CMD_SET_VOLUME */
    uint8_t vol_pct = (uint8_t)((uint32_t)volume * 100 / 127);
    esp_err_t err = audio_cmd_set_volume_send(vol_pct);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "falha ao enviar comando de volume %u recebido de %s: %s",
                 (unsigned)volume, source, esp_err_to_name(err));
        return err;
    }

    s_volume = volume;
    bt_link_mgr_set_volume(volume);          /* volume por slot, gravado no NVS com atraso */
    ESP_LOGI(TAG, "volume aplicado (%s): %u/127", source, (unsigned)s_volume);
    return ESP_OK;
}

/* Roda na task BT_APP */
static void avrcp_evt_hdl(uint16_t event, void *p)
{
    esp_avrc_tg_cb_param_t *rc = p;

    switch (event) {
    case ESP_AVRC_TG_CONNECTION_STATE_EVT:
        if (rc->conn_stat.connected) {
            uint8_t v = bt_link_mgr_get_volume();   /* volume salvo do slot; o celular o adota via interim */
            uint8_t v_pct = (uint8_t)((uint32_t)v * 100 / 127);
            if (audio_cmd_set_volume_send(v_pct) == ESP_OK) {
                s_volume = v;
                ESP_LOGI(TAG, "volume do slot restaurado via comando: %u/127", (unsigned)v);
            }
        } else {
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

/* Buffers de metadados de texto para repassar à interface */
static char s_track_title[64] = {0};
static char s_track_artist[64] = {0};
static char s_track_album[64] = {0};

/* Roda na task BT_APP */
static void avrcp_ct_evt_hdl(uint16_t event, void *p)
{
    esp_avrc_ct_cb_param_t *rc = p;

    switch (event) {
    case ESP_AVRC_CT_CONNECTION_STATE_EVT:
        s_ct_connected = rc->conn_stat.connected;
        if (s_ct_connected) {
            ESP_LOGI(TAG, "AVRCP CT Conectado, abrindo servico Cover Art e pedindo metadados...");
            cover_art_srv_open();

            /* Consulta capacidades de notificacao do peer e registra para mudanca de faixa */
            esp_avrc_ct_send_get_rn_capabilities_cmd(avrcp_next_tl());
            esp_avrc_ct_send_register_notification_cmd(avrcp_next_tl(), ESP_AVRC_RN_TRACK_CHANGE, 0);

            /* Requisita metadados textuais e identificador de capa da musica em reproducao */
            esp_avrc_ct_send_metadata_cmd(avrcp_next_tl(), ESP_AVRC_MD_ATTR_TITLE | 
                                                           ESP_AVRC_MD_ATTR_ARTIST | 
                                                           ESP_AVRC_MD_ATTR_ALBUM | 
                                                           ESP_AVRC_MD_ATTR_COVER_ART);
        } else {
            ESP_LOGI(TAG, "AVRCP CT Desconectado, resetando Cover Art");
            cover_art_srv_close();
            cover_art_reset_to_default();
        }
        break;

    case ESP_AVRC_CT_METADATA_RSP_EVT:
        if (rc->meta_rsp.attr_text) {
            ESP_LOGI(TAG, "Metadata attr 0x%02x (len=%d): %.*s", 
                     rc->meta_rsp.attr_id, rc->meta_rsp.attr_length, 
                     rc->meta_rsp.attr_length, rc->meta_rsp.attr_text);

            if (rc->meta_rsp.attr_id == ESP_AVRC_MD_ATTR_COVER_ART) {
                /* Atualiza o Image Handle recebido e solicita o linked thumbnail */
                cover_art_srv_ct_metadata_update(rc->meta_rsp.attr_text, rc->meta_rsp.attr_length);
            } else {
                bool meta_updated = false;
                if (rc->meta_rsp.attr_id == ESP_AVRC_MD_ATTR_TITLE) {
                    size_t cpy = (rc->meta_rsp.attr_length < sizeof(s_track_title) - 1) ? 
                                 rc->meta_rsp.attr_length : (sizeof(s_track_title) - 1);
                    memcpy(s_track_title, rc->meta_rsp.attr_text, cpy);
                    s_track_title[cpy] = '\0';
                    meta_updated = true;
                } else if (rc->meta_rsp.attr_id == ESP_AVRC_MD_ATTR_ARTIST) {
                    size_t cpy = (rc->meta_rsp.attr_length < sizeof(s_track_artist) - 1) ? 
                                 rc->meta_rsp.attr_length : (sizeof(s_track_artist) - 1);
                    memcpy(s_track_artist, rc->meta_rsp.attr_text, cpy);
                    s_track_artist[cpy] = '\0';
                    meta_updated = true;
                } else if (rc->meta_rsp.attr_id == ESP_AVRC_MD_ATTR_ALBUM) {
                    size_t cpy = (rc->meta_rsp.attr_length < sizeof(s_track_album) - 1) ? 
                                 rc->meta_rsp.attr_length : (sizeof(s_track_album) - 1);
                    memcpy(s_track_album, rc->meta_rsp.attr_text, cpy);
                    s_track_album[cpy] = '\0';
                    meta_updated = true;
                }
                if (meta_updated) {
                    ui_model_post_track_metadata(s_track_title, s_track_artist, s_track_album);
                }
            }
        }
        break;

    case ESP_AVRC_CT_REMOTE_FEATURES_EVT:
        s_peer_supports_abs_vol = ((rc->rmt_feats.feat_mask & ESP_AVRC_FEAT_ADV_CTRL) != 0);
        ESP_LOGI(TAG, "CT Remote features: 0x%04lX, tg_feat_flag: 0x%x, suporta vol: %d", 
                 (unsigned long)rc->rmt_feats.feat_mask, rc->rmt_feats.tg_feat_flag, s_peer_supports_abs_vol);
        if (rc->rmt_feats.tg_feat_flag & ESP_AVRC_FEAT_FLAG_TG_COVER_ART) {
            ESP_LOGI(TAG, "Host remoto suporta Cover Art (BIP)! Conectando cliente OBEX...");
            cover_art_srv_connect(ESP_AVRC_CA_MTU_MAX);
        }
        break;

    case ESP_AVRC_CT_COVER_ART_STATE_EVT:
        if (rc->cover_art_state.state == ESP_AVRC_COVER_ART_CONNECTED) {
            ESP_LOGI(TAG, "Cliente Cover Art conectado com sucesso!");
            cover_art_srv_set_connected(true);
            cover_art_srv_ca_req();
        } else {
            ESP_LOGW(TAG, "Cliente Cover Art desconectado (motivo: %d)", rc->cover_art_state.reason);
            cover_art_srv_set_connected(false);
        }
        break;

    case ESP_AVRC_CT_COVER_ART_DATA_EVT:
        /* Processa finalizacao do pacote JPEG na tarefa BtAppTask */
        cover_art_set_image_final(rc->cover_art_data.final);
        break;

    case ESP_AVRC_CT_CHANGE_NOTIFY_EVT:
        if (rc->change_ntf.event_id == ESP_AVRC_RN_TRACK_CHANGE) {
            ESP_LOGI(TAG, "Mudança de faixa detectada (RN_TRACK_CHANGE). Requisitando novos metadados e capa...");
            /* Reseta o estado de download anterior para não bloquear a nova faixa */
            cover_art_reset_getting();

            /* CRÍTICO: Re-registra a notificação que é de disparo único (one-shot) no padrão AVRCP */
            esp_avrc_ct_send_register_notification_cmd(avrcp_next_tl(), ESP_AVRC_RN_TRACK_CHANGE, 0);

            esp_avrc_ct_send_metadata_cmd(avrcp_next_tl(), ESP_AVRC_MD_ATTR_TITLE | 
                                                           ESP_AVRC_MD_ATTR_ARTIST | 
                                                           ESP_AVRC_MD_ATTR_ALBUM | 
                                                           ESP_AVRC_MD_ATTR_COVER_ART);
            cover_art_srv_ca_req();
        }
        break;

    case ESP_AVRC_CT_GET_RN_CAPABILITIES_RSP_EVT:
        /* Re-assegura o registro de mudanca de faixa caso suportado */
        if (esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_TEST, &rc->get_rn_caps_rsp.evt_set, ESP_AVRC_RN_TRACK_CHANGE)) {
            esp_avrc_ct_send_register_notification_cmd(avrcp_next_tl(), ESP_AVRC_RN_TRACK_CHANGE, 0);
        }
        cover_art_srv_ca_req();
        break;

    default:
        break;
    }
}

/**
 * @brief Realiza Deep Copy seguro dos parâmetros de metadados recebidos da pilha Bluedroid.
 * BLINDAGEM CONTRA USE-AFTER-FREE (AGENTS.md §2 Princípio 5 e Skill bt-avrcp-media):
 * O ponteiro rc->meta_rsp.attr_text é volátil e liberado assim que o callback retorna.
 * Esta função clona a string no heap antes de despachar a mensagem para a BtAppTask.
 */
static void avrcp_copy_metadata(void *p_dest, void *p_src, int p_len)
{
    esp_avrc_ct_cb_param_t *dst = (esp_avrc_ct_cb_param_t *)p_dest;
    esp_avrc_ct_cb_param_t *src = (esp_avrc_ct_cb_param_t *)p_src;
    memcpy(dst, src, p_len);
    if (src->meta_rsp.attr_text && src->meta_rsp.attr_length > 0) {
        dst->meta_rsp.attr_text = (uint8_t *)malloc(src->meta_rsp.attr_length + 1);
        if (dst->meta_rsp.attr_text) {
            memcpy(dst->meta_rsp.attr_text, src->meta_rsp.attr_text, src->meta_rsp.attr_length);
            dst->meta_rsp.attr_text[src->meta_rsp.attr_length] = '\0';
        }
    } else {
        dst->meta_rsp.attr_text = NULL;
    }
}

static void avrcp_free_metadata(void *p_params)
{
    esp_avrc_ct_cb_param_t *param = (esp_avrc_ct_cb_param_t *)p_params;
    if (param && param->meta_rsp.attr_text) {
        free(param->meta_rsp.attr_text);
        param->meta_rsp.attr_text = NULL;
    }
}

static void avrcp_ct_cb(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param)
{
    /* Salva fragmento de imagem imediatamente no contexto de callback para evitar Use-After-Free */
    if (event == ESP_AVRC_CT_COVER_ART_DATA_EVT) {
        if (param->cover_art_data.status == ESP_BT_STATUS_SUCCESS && param->cover_art_data.p_data) {
            cover_art_save_chunk(param->cover_art_data.p_data, param->cover_art_data.data_len);
        } else if (param->cover_art_data.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGW(TAG, "Recepcao de chunk Cover Art falhou (status=%d), resetando estado de obtencao",
                     param->cover_art_data.status);
            cover_art_reset_getting();
        }
    }

    /* Utiliza deep copy para metadados (garante integridade do Image Handle e strings UTF-8) */
    if (event == ESP_AVRC_CT_METADATA_RSP_EVT) {
        bt_app_work_dispatch(avrcp_ct_evt_hdl, event, param, sizeof(esp_avrc_ct_cb_param_t),
                             avrcp_copy_metadata, avrcp_free_metadata);
    } else {
        bt_app_work_dispatch(avrcp_ct_evt_hdl, event, param, sizeof(esp_avrc_ct_cb_param_t), NULL, NULL);
    }
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
    /* Inicializa buffers de Cover Art em PSRAM */
    cover_art_init();

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
    esp_avrc_ct_send_passthrough_cmd(avrcp_next_tl(), key_code, 0); /* PRESSED */
    return esp_avrc_ct_send_passthrough_cmd(avrcp_next_tl(), key_code, 1); /* RELEASED */
}

esp_err_t bt_avrcp_send_play(void) { return send_pt_cmd(ESP_AVRC_PT_CMD_PLAY); }
esp_err_t bt_avrcp_send_pause(void) { return send_pt_cmd(ESP_AVRC_PT_CMD_PAUSE); }
esp_err_t bt_avrcp_send_next(void) { return send_pt_cmd(ESP_AVRC_PT_CMD_FORWARD); }
esp_err_t bt_avrcp_send_prev(void) { return send_pt_cmd(ESP_AVRC_PT_CMD_BACKWARD); }

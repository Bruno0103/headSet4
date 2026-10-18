/**
 * @file phone_ctl.c
 * @brief Implementacao do Actor phone_ctl sob demanda com parser cJSON e telemetria por eventos.
 *
 * Em conformidade com AGENTS.md (WP 7.2 e WP 7.3):
 * - Actor sob demanda com encerramento por inatividade (idle_ms = 30000).
 * - Parser JSON robusto com cJSON com retorno de status estruturado em JSON.
 * - Despacha ordens imperativas exclusivamente via hs_actor_send() para os atores donos
 *   (audio_actor_get, bt_link_actor_get, settings_get_actor, actuators).
 * - Assina eventos do barramento central (SENSOR_EVT, BT_EVT, AUDIO_EVT, CFG_EVT)
 *   e gera notificacoes BLE com throttling para evitar saturacao do canal de radio.
 */

#include "phone_ctl.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "actuators.h"
#include "audio.h"
#include "bt_avrcp.h"
#include "bt_ctl_service.h"
#include "bt_fastpair.h"
#include "bt_link_mgr.h"
#include "hs_actor.h"
#include "hs_cmds.h"
#include "hs_events.h"
#include "settings.h"
#include "ui_model.h"
#include "ui_bridge.h"
#include "esp_heap_caps.h"
#include "mbedtls/base64.h"
#include "media_storage.h"

static const char *TAG = "act_phone_ctl";

/* Estrutura de controle para recepção de chunks de arquivos de mídia (240x296) */
typedef struct {
    bool     active;
    bool     is_gif;
    uint8_t  slot;
    uint32_t expected_size;
    uint32_t expected_crc32;
    char     name[MEDIA_NAME_MAX_LEN];
    uint8_t *buffer;
    uint32_t received_bytes;
} media_upload_ctx_t;

static media_upload_ctx_t s_upload_ctx = {0};

#define PHONE_CTL_STACK_SIZE    3584
#define PHONE_CTL_QUEUE_LEN     32
#define PHONE_CTL_IDLE_MS       30000  /* 30 segundos sem atividade -> encerra a task */

/* Buffer de montagem de frames de entrada (suporta pacotes Base64 com folga) */
#define INCOMING_BUF_MAX        1024
static char                     s_incoming_buf[INCOMING_BUF_MAX];
static size_t                   s_incoming_len = 0;
static SemaphoreHandle_t        s_ctl_mtx = NULL;

static hs_actor_t              *s_phone_actor = NULL;

/* Throttling de telemetria BLE: minimo de 500 ms entre notificacoes continuas */
#define NOTIFY_THROTTLE_MS      500
static int64_t                  s_last_notify_us = 0;

/* Declarações adiante */
static void phone_ctl_on_msg(hs_actor_t *self, const hs_msg_t *msg);
static void phone_ctl_on_start(void *ctx);
static void phone_ctl_on_stop(void *ctx);
static void handle_json_command(uint16_t conn_id, const char *json_str);
static void send_event_telemetry(const char *category, const char *event_name, const char *payload_json);

/* ============================================================================
 * TRATAMENTO DE EVENTOS CENTRAIS (esp_event) -> Telemetria com Throttling
 * ============================================================================
 */

static void on_sensor_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    char buf[64];
    switch (id) {
    case SENSOR_EVT_WORN:
        send_event_telemetry("sensor", "worn", "{\"worn\":true}");
        break;
    case SENSOR_EVT_REMOVED:
        send_event_telemetry("sensor", "removed", "{\"worn\":false}");
        break;
    case SENSOR_EVT_BATTERY:
        if (data) {
            const sensor_battery_evt_t *b = (const sensor_battery_evt_t *)data;
            snprintf(buf, sizeof(buf), "{\"pct\":%u,\"mv\":%u,\"chg\":%s}",
                     b->percent, b->millivolts, b->is_charging ? "true" : "false");
            send_event_telemetry("sensor", "battery", buf);
        }
        break;
    default:
        break;
    }
}

static void on_bt_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    char buf[80];
    switch (id) {
    case BT_EVT_LINK_UP:
        if (data) {
            const bt_link_evt_t *l = (const bt_link_evt_t *)data;
            snprintf(buf, sizeof(buf), "{\"slot\":%u,\"status\":\"connected\"}", l->slot);
            send_event_telemetry("bt", "link_up", buf);
        }
        break;
    case BT_EVT_LINK_DOWN:
        if (data) {
            const bt_link_evt_t *l = (const bt_link_evt_t *)data;
            snprintf(buf, sizeof(buf), "{\"slot\":%u,\"status\":\"disconnected\"}", l->slot);
            send_event_telemetry("bt", "link_down", buf);
        }
        break;
    case BT_EVT_SLOT_CHANGED:
        if (data) {
            const bt_slot_evt_t *s = (const bt_slot_evt_t *)data;
            snprintf(buf, sizeof(buf), "{\"active_slot\":%u}", s->active_slot);
            send_event_telemetry("bt", "slot_changed", buf);
        }
        break;
    case BT_EVT_PEER_NAME:
        if (data) {
            const bt_peer_name_evt_t *n = (const bt_peer_name_evt_t *)data;
            snprintf(buf, sizeof(buf), "{\"slot\":%u,\"name\":\"%s\"}", n->slot, n->name);
            send_event_telemetry("bt", "peer_name", buf);
        }
        break;
    default:
        break;
    }
}

static void on_audio_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    char buf[64];
    switch (id) {
    case AUDIO_EVT_VOLUME_CHANGED:
        if (data) {
            const audio_volume_evt_t *v = (const audio_volume_evt_t *)data;
            snprintf(buf, sizeof(buf), "{\"vol\":%u,\"mute\":%s}", v->volume_percent, v->muted ? "true" : "false");
            send_event_telemetry("audio", "volume", buf);
        }
        break;
    case AUDIO_EVT_MODE_CHANGED:
        if (data) {
            const audio_mode_evt_t *m = (const audio_mode_evt_t *)data;
            snprintf(buf, sizeof(buf), "{\"mode\":%u,\"rate\":%" PRIu32 "}", (unsigned int)m->mode, m->sample_rate);
            send_event_telemetry("audio", "mode", buf);
        }
        break;
    default:
        break;
    }
}

static void on_cfg_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == CFG_EVT_SETTING_CHANGED && data) {
        const cfg_changed_evt_t *c = (const cfg_changed_evt_t *)data;
        char buf[64];
        snprintf(buf, sizeof(buf), "{\"key\":\"%s\",\"val\":%u}", c->key, c->value_u8);
        send_event_telemetry("cfg", "changed", buf);
    }
}

static void send_event_telemetry(const char *category, const char *event_name, const char *payload_json)
{
    if (!bt_ctl_service_is_notify_enabled()) {
        return;
    }

    /* Throttling com esp_timer */
    int64_t now = esp_timer_get_time();
    if ((now - s_last_notify_us) < (NOTIFY_THROTTLE_MS * 1000LL)) {
        return; /* Descarta silenciosamente atualizacoes continuas em rajada */
    }
    s_last_notify_us = now;

    uint16_t conn_id = bt_ctl_service_get_conn_id();
    if (conn_id == 0xFFFF) {
        return;
    }

    char out[192];
    snprintf(out, sizeof(out), "{\"evt\":\"%s.%s\",\"data\":%s}", category, event_name, payload_json);
    bt_ctl_service_send_notify(conn_id, (const uint8_t *)out, strlen(out));
}

/* ============================================================================
 * PROCESSAMENTO DE COMANDOS JSON (cJSON) -> Validação e Respostas Estruturadas
 * ============================================================================
 */

/**
 * @brief Formata o cabeçalho base da resposta JSON contendo "status", "cmd" e o "id" de correlação (se fornecido).
 *
 * Permite ao app Android correlacionar respostas a requisições assíncronas feitas via
 * Coroutines/Flow/Promises, eliminando condições de corrida entre múltiplos comandos simultâneos.
 *
 * @param buf Buffer de saída.
 * @param max_len Capacidade máxima do buffer.
 * @param status "ok" ou "error".
 * @param cmd Nome da operação (ex: "set_volume").
 * @param id_item Objeto cJSON correspondente ao "id" enviado pelo app (pode ser número, string ou NULL).
 * @return int Quantidade de caracteres gravados no buffer (sem o caractere terminador ou '}' final).
 */
static int phone_ctl_format_resp_header(char *buf, size_t max_len, const char *status, const char *cmd, const cJSON *id_item)
{
    if (!id_item) {
        return snprintf(buf, max_len, "{\"status\":\"%s\",\"cmd\":\"%s\"", status, cmd ? cmd : "unknown");
    }
    if (cJSON_IsNumber(id_item)) {
        return snprintf(buf, max_len, "{\"status\":\"%s\",\"cmd\":\"%s\",\"id\":%d", status, cmd ? cmd : "unknown", id_item->valueint);
    }
    if (cJSON_IsString(id_item)) {
        return snprintf(buf, max_len, "{\"status\":\"%s\",\"cmd\":\"%s\",\"id\":\"%s\"", status, cmd ? cmd : "unknown", id_item->valuestring);
    }
    return snprintf(buf, max_len, "{\"status\":\"%s\",\"cmd\":\"%s\"", status, cmd ? cmd : "unknown");
}

/**
 * @brief Formata resposta padronizada de erro com o motivo de falha ("reason") e detalhe opcional ("detail").
 *
 * @param buf Buffer de saída onde o JSON será escrito.
 * @param max_len Tamanho máximo do buffer.
 * @param cmd Nome do comando que gerou a rejeição.
 * @param id_item Objeto cJSON com o identificador de correlação (se houver).
 * @param reason Código de erro de máquina (ex: "missing_arg", "out_of_range", "invalid_type").
 * @param detail Mensagem legível explicativa detalhando a causa da rejeição.
 */
static void phone_ctl_format_err(char *buf, size_t max_len, const char *cmd, const cJSON *id_item, const char *reason, const char *detail)
{
    int len = phone_ctl_format_resp_header(buf, max_len, "error", cmd, id_item);
    if (detail && strlen(detail) > 0) {
        snprintf(buf + len, max_len - len, ",\"reason\":\"%s\",\"detail\":\"%s\"}", reason, detail);
    } else {
        snprintf(buf + len, max_len - len, ",\"reason\":\"%s\"}", reason);
    }
    ESP_LOGW(TAG, "Comando BLE rejeitado [%s]: reason=%s, detail=%s", cmd ? cmd : "unknown", reason, detail ? detail : "");
}

static void handle_json_command(uint16_t conn_id, const char *json_str)
{
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        ESP_LOGW(TAG, "JSON invalido recebido: %s", json_str);
        const char *err_rsp = "{\"status\":\"error\",\"reason\":\"invalid_json\",\"detail\":\"malformed_json_syntax\"}";
        bt_ctl_service_send_notify(conn_id, (const uint8_t *)err_rsp, strlen(err_rsp));
        return;
    }

    cJSON *cmd_item = cJSON_GetObjectItem(root, "cmd");
    if (!cmd_item || !cJSON_IsString(cmd_item)) {
        cJSON_Delete(root);
        const char *err_rsp = "{\"status\":\"error\",\"reason\":\"missing_cmd\",\"detail\":\"cmd_string_is_required\"}";
        bt_ctl_service_send_notify(conn_id, (const uint8_t *)err_rsp, strlen(err_rsp));
        return;
    }

    const char *cmd = cmd_item->valuestring;
    cJSON *id_item = cJSON_GetObjectItem(root, "id");
    char resp_buf[512] = {0};

    /* ========================================================================
     * 1. HANDSHAKE FORMAL DE CONEXÃO COM COMPANION APP
     * ======================================================================== */
    if (strcmp(cmd, "app_connect") == 0) {
        cJSON *app_ver = cJSON_GetObjectItem(root, "app_version");
        cJSON *client  = cJSON_GetObjectItem(root, "client");
        ESP_LOGI(TAG, "Handshake formal recebido do companion app (cliente: %s, versao: %s)",
                 (client && cJSON_IsString(client)) ? client->valuestring : "desconhecido",
                 (app_ver && cJSON_IsString(app_ver)) ? app_ver->valuestring : "1.0.0");

        int len = phone_ctl_format_resp_header(resp_buf, sizeof(resp_buf), "ok", cmd, id_item);
        snprintf(resp_buf + len, sizeof(resp_buf) - len,
                 ",\"connected\":true,\"headset\":\"HeadSet4\",\"fw_version\":\"1.0.0\",\"api_version\":1,"
                 "\"features\":[\"eq5\",\"media_upload\",\"dual_slot\",\"haptics\",\"proximity\"]}");
    }

    /* ========================================================================
     * 2. CONSULTA GLOBAL DE ESTADO (SNAPSHOT DO HEADSET)
     * ======================================================================== */
    else if (strcmp(cmd, "get_all_status") == 0) {
        ui_model_data_t snap;
        ui_model_get_snapshot(&snap);

        int len = phone_ctl_format_resp_header(resp_buf, sizeof(resp_buf), "ok", cmd, id_item);
        snprintf(resp_buf + len, sizeof(resp_buf) - len,
                 ",\"bt1\":\"%s\",\"bt2\":\"%s\",\"st1\":\"%s\",\"st2\":\"%s\","
                 "\"alt\":%s,\"prox\":%s,\"bat\":%u,\"disp\":%s,\"brilho\":%u,\"vol\":%u,"
                 "\"img\":%d,\"gif\":%d}",
                 snap.bt_name_slot0, snap.bt_name_slot1,
                 snap.bt_status_slot0, snap.bt_status_slot1,
                 snap.bt_auto_switch ? "true" : "false",
                 snap.prox_worn ? "\"worn\"" : "\"removed\"",
                 (unsigned int)snap.battery_percent,
                 snap.display_on ? "true" : "false",
                 (unsigned int)snap.display_brightness,
                 (unsigned int)snap.audio_volume,
                 snap.display_selected_img_idx,
                 snap.display_selected_gif_idx);
    }

    /* ========================================================================
     * 3. CONTROLE DE VOLUME E ÁUDIO
     * ======================================================================== */
    else if (strcmp(cmd, "set_volume") == 0) {
        cJSON *val = cJSON_GetObjectItem(root, "val");
        if (!val) {
            phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "missing_arg", "val is required");
        } else if (!cJSON_IsNumber(val)) {
            phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "invalid_type", "val must be a number");
        } else {
            int vol = val->valueint;
            if (vol < 0 || vol > 100) {
                phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "out_of_range", "val must be between 0 and 100");
            } else {
                audio_cmd_set_volume_send((uint8_t)vol);
                settings_set_u8(SETTINGS_KEY_VOL_SLOT0, (uint8_t)vol);
                int len = phone_ctl_format_resp_header(resp_buf, sizeof(resp_buf), "ok", cmd, id_item);
                snprintf(resp_buf + len, sizeof(resp_buf) - len, ",\"val\":%d}", vol);
            }
        }
    }
    else if (strcmp(cmd, "set_mute") == 0) {
        cJSON *m = cJSON_GetObjectItem(root, "mute");
        if (!m) {
            phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "missing_arg", "mute is required");
        } else if (!cJSON_IsBool(m)) {
            phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "invalid_type", "mute must be boolean");
        } else {
            bool muted = cJSON_IsTrue(m);
            audio_cmd_set_mute_send(muted);
            int len = phone_ctl_format_resp_header(resp_buf, sizeof(resp_buf), "ok", cmd, id_item);
            snprintf(resp_buf + len, sizeof(resp_buf) - len, ",\"mute\":%s}", muted ? "true" : "false");
        }
    }

    /* ========================================================================
     * 4. CONTROLE DE BLUETOOTH E MULTIPONTO
     * ======================================================================== */
    else if (strcmp(cmd, "select_slot") == 0) {
        cJSON *slot = cJSON_GetObjectItem(root, "slot");
        if (!slot) {
            phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "missing_arg", "slot is required");
        } else if (!cJSON_IsNumber(slot)) {
            phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "invalid_type", "slot must be a number");
        } else {
            int s = slot->valueint;
            if (s < 0 || s > 1) {
                phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "out_of_range", "slot must be 0 or 1");
            } else {
                hs_actor_t *bt_act = bt_link_actor_get();
                if (!bt_act) {
                    phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "actor_unavailable", "bt_link actor not ready");
                } else {
                    bt_cmd_select_slot_t scmd = { .slot = (uint8_t)s };
                    hs_actor_send(bt_act, BT_CMD_SELECT_SLOT, &scmd, sizeof(scmd), pdMS_TO_TICKS(50));
                    int len = phone_ctl_format_resp_header(resp_buf, sizeof(resp_buf), "ok", cmd, id_item);
                    snprintf(resp_buf + len, sizeof(resp_buf) - len, ",\"slot\":%d}", s);
                }
            }
        }
    }
    else if (strcmp(cmd, "disconnect_slot") == 0) {
        cJSON *slot = cJSON_GetObjectItem(root, "slot");
        if (!slot) {
            phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "missing_arg", "slot is required");
        } else if (!cJSON_IsNumber(slot)) {
            phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "invalid_type", "slot must be a number");
        } else {
            int s = slot->valueint;
            if (s < 0 || s > 1) {
                phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "out_of_range", "slot must be 0 or 1");
            } else {
                hs_actor_t *bt_act = bt_link_actor_get();
                if (!bt_act) {
                    phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "actor_unavailable", "bt_link actor not ready");
                } else {
                    bt_cmd_disconnect_t dcmd = { .slot = (uint8_t)s };
                    hs_actor_send(bt_act, BT_CMD_DISCONNECT, &dcmd, sizeof(dcmd), pdMS_TO_TICKS(50));
                    int len = phone_ctl_format_resp_header(resp_buf, sizeof(resp_buf), "ok", cmd, id_item);
                    snprintf(resp_buf + len, sizeof(resp_buf) - len, ",\"slot\":%d,\"disconnected\":true}", s);
                }
            }
        }
    }
    else if (strcmp(cmd, "start_pairing") == 0) {
        hs_actor_t *bt_act = bt_link_actor_get();
        if (!bt_act) {
            phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "actor_unavailable", "bt_link actor not ready");
        } else {
            bt_cmd_start_pairing_t pcmd = { .duration_sec = 120 };
            hs_actor_send(bt_act, BT_CMD_START_PAIRING, &pcmd, sizeof(pcmd), pdMS_TO_TICKS(50));
            int len = phone_ctl_format_resp_header(resp_buf, sizeof(resp_buf), "ok", cmd, id_item);
            snprintf(resp_buf + len, sizeof(resp_buf) - len, ",\"duration_sec\":120,\"pairing\":true}");
        }
    }
    else if (strcmp(cmd, "set_auto_switch") == 0) {
        cJSON *en = cJSON_GetObjectItem(root, "enable");
        if (!en) {
            phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "missing_arg", "enable is required");
        } else if (!cJSON_IsBool(en)) {
            phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "invalid_type", "enable must be boolean");
        } else {
            bool asw = cJSON_IsTrue(en);
            settings_set_u8(SETTINGS_KEY_BT_AUTOSWITCH, asw ? 1 : 0);
            hs_actor_t *bt_act = bt_link_actor_get();
            if (bt_act) {
                bt_cmd_set_auto_switch_t scmd = { .enabled = asw };
                hs_actor_send(bt_act, BT_CMD_SET_AUTO_SWITCH, &scmd, sizeof(scmd), pdMS_TO_TICKS(50));
            }
            ui_model_update_msg_t umsg = {
                .type = UI_UPDATE_BT_AUTO_SWITCH,
                .payload.bt_auto_sw = { .auto_switch = asw }
            };
            ui_model_post_update(&umsg);
            int len = phone_ctl_format_resp_header(resp_buf, sizeof(resp_buf), "ok", cmd, id_item);
            snprintf(resp_buf + len, sizeof(resp_buf) - len, ",\"enable\":%s}", asw ? "true" : "false");
        }
    }

    /* ========================================================================
     * 5. CONTROLE DE DISPLAY & BRILHO
     * ======================================================================== */
    else if (strcmp(cmd, "set_brightness") == 0) {
        cJSON *val = cJSON_GetObjectItem(root, "val");
        if (!val) {
            phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "missing_arg", "val is required");
        } else if (!cJSON_IsNumber(val)) {
            phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "invalid_type", "val must be a number");
        } else {
            int b = val->valueint;
            if (b < 0 || b > 100) {
                phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "out_of_range", "val must be between 0 and 100");
            } else {
                ui_bridge_display_set_brilho(b);
                int len = phone_ctl_format_resp_header(resp_buf, sizeof(resp_buf), "ok", cmd, id_item);
                snprintf(resp_buf + len, sizeof(resp_buf) - len, ",\"val\":%d}", b);
            }
        }
    }
    else if (strcmp(cmd, "set_display_state") == 0) {
        cJSON *en = cJSON_GetObjectItem(root, "enable");
        if (!en) {
            phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "missing_arg", "enable is required");
        } else if (!cJSON_IsBool(en)) {
            phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "invalid_type", "enable must be boolean");
        } else {
            bool turn_on = cJSON_IsTrue(en);
            if (turn_on) {
                ui_bridge_display_set_ativar(true);
            } else {
                ui_bridge_display_set_desativar(true);
            }
            int len = phone_ctl_format_resp_header(resp_buf, sizeof(resp_buf), "ok", cmd, id_item);
            snprintf(resp_buf + len, sizeof(resp_buf) - len, ",\"enable\":%s}", turn_on ? "true" : "false");
        }
    }
    else if (strcmp(cmd, "set_display_timeout") == 0) {
        cJSON *val = cJSON_GetObjectItem(root, "val");
        if (!val) {
            phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "missing_arg", "val is required");
        } else if (!cJSON_IsNumber(val)) {
            phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "invalid_type", "val must be a number");
        } else {
            int t = val->valueint;
            if (t < 0) {
                phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "out_of_range", "val must be >= 0");
            } else {
                ui_bridge_display_set_tempo_tela(t);
                int len = phone_ctl_format_resp_header(resp_buf, sizeof(resp_buf), "ok", cmd, id_item);
                snprintf(resp_buf + len, sizeof(resp_buf) - len, ",\"val\":%d}", t);
            }
        }
    }

    /* ========================================================================
     * 6. ATUADORES (VIBRACALL E ORELHAS ANIMATRÔNICAS)
     * ======================================================================== */
    else if (strcmp(cmd, "set_vibracall") == 0) {
        cJSON *en = cJSON_GetObjectItem(root, "enable");
        cJSON *val = cJSON_GetObjectItem(root, "val");
        if (!en && !val) {
            phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "missing_arg", "enable or val is required");
        } else {
            bool has_error = false;
            bool en_val = false;
            bool has_en = false;
            int int_val = -1;

            if (en) {
                if (!cJSON_IsBool(en)) {
                    phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "invalid_type", "enable must be boolean");
                    has_error = true;
                } else {
                    en_val = cJSON_IsTrue(en);
                    has_en = true;
                    ui_bridge_vibracall_set_ativar(en_val);
                }
            }
            if (!has_error && val) {
                if (!cJSON_IsNumber(val)) {
                    phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "invalid_type", "val must be a number");
                    has_error = true;
                } else {
                    int_val = val->valueint;
                    if (int_val < 0 || int_val > 100) {
                        phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "out_of_range", "val must be between 0 and 100");
                        has_error = true;
                    } else {
                        ui_bridge_vibracall_set_intensidade(int_val);
                    }
                }
            }

            if (!has_error) {
                int len = phone_ctl_format_resp_header(resp_buf, sizeof(resp_buf), "ok", cmd, id_item);
                if (has_en && int_val >= 0) {
                    snprintf(resp_buf + len, sizeof(resp_buf) - len, ",\"enable\":%s,\"val\":%d}", en_val ? "true" : "false", int_val);
                } else if (has_en) {
                    snprintf(resp_buf + len, sizeof(resp_buf) - len, ",\"enable\":%s}", en_val ? "true" : "false");
                } else {
                    snprintf(resp_buf + len, sizeof(resp_buf) - len, ",\"val\":%d}", int_val);
                }
            }
        }
    }
    else if (strcmp(cmd, "trigger_vibrate") == 0 || strcmp(cmd, "notification") == 0) {
        uint16_t dur = 300;
        uint8_t intens = 0;
        cJSON *dur_item = cJSON_GetObjectItem(root, "duration_ms");
        if (dur_item && cJSON_IsNumber(dur_item)) {
            dur = (uint16_t)dur_item->valueint;
        }
        cJSON *int_item = cJSON_GetObjectItem(root, "intensity");
        if (int_item && cJSON_IsNumber(int_item)) {
            intens = (uint8_t)int_item->valueint;
        }
        ESP_LOGI(TAG, "Notificacao recebida do smartphone ativo! Acionando vibracall (%u ms, %u%%)", dur, intens);
        actuators_vibrate(dur, intens);
        int len = phone_ctl_format_resp_header(resp_buf, sizeof(resp_buf), "ok", cmd, id_item);
        snprintf(resp_buf + len, sizeof(resp_buf) - len, ",\"vibrated\":true,\"duration_ms\":%u,\"intensity\":%u}", dur, intens);
    }
    else if (strcmp(cmd, "set_ears_angle") == 0) {
        cJSON *val = cJSON_GetObjectItem(root, "val");
        if (!val) {
            phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "missing_arg", "val is required");
        } else if (!cJSON_IsNumber(val)) {
            phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "invalid_type", "val must be a number");
        } else {
            int a = val->valueint;
            if (a < 0 || a > 180) {
                phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "out_of_range", "val must be between 0 and 180");
            } else {
                ui_bridge_orelhas_set_angulo_maximo(a);
                int len = phone_ctl_format_resp_header(resp_buf, sizeof(resp_buf), "ok", cmd, id_item);
                snprintf(resp_buf + len, sizeof(resp_buf) - len, ",\"val\":%d}", a);
            }
        }
    }
    else if (strcmp(cmd, "set_ears_enable") == 0) {
        cJSON *en = cJSON_GetObjectItem(root, "enable");
        if (!en) {
            phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "missing_arg", "enable is required");
        } else if (!cJSON_IsBool(en)) {
            phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "invalid_type", "enable must be boolean");
        } else {
            bool e = cJSON_IsTrue(en);
            ui_bridge_orelhas_set_ativar(e);
            int len = phone_ctl_format_resp_header(resp_buf, sizeof(resp_buf), "ok", cmd, id_item);
            snprintf(resp_buf + len, sizeof(resp_buf) - len, ",\"enable\":%s}", e ? "true" : "false");
        }
    }

    /* ========================================================================
     * 7. SENSOR DE PROXIMIDADE (USO NA CABEÇA - APDS-9930)
     * ======================================================================== */
    else if (strcmp(cmd, "set_prox_enable") == 0) {
        cJSON *en = cJSON_GetObjectItem(root, "enable");
        if (!en) {
            phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "missing_arg", "enable is required");
        } else if (!cJSON_IsBool(en)) {
            phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "invalid_type", "enable must be boolean");
        } else {
            bool e = cJSON_IsTrue(en);
            ui_bridge_proximidade_set_ativar(e);
            int len = phone_ctl_format_resp_header(resp_buf, sizeof(resp_buf), "ok", cmd, id_item);
            snprintf(resp_buf + len, sizeof(resp_buf) - len, ",\"enable\":%s}", e ? "true" : "false");
        }
    }
    else if (strcmp(cmd, "set_prox_sensitivity") == 0) {
        cJSON *val = cJSON_GetObjectItem(root, "val");
        if (!val) {
            phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "missing_arg", "val is required");
        } else if (!cJSON_IsNumber(val)) {
            phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "invalid_type", "val must be a number");
        } else {
            int s = val->valueint;
            if (s < 0 || s > 1023) {
                phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "out_of_range", "val must be between 0 and 1023");
            } else {
                ui_bridge_proximidade_set_distancia_sensibilidade(s);
                int len = phone_ctl_format_resp_header(resp_buf, sizeof(resp_buf), "ok", cmd, id_item);
                snprintf(resp_buf + len, sizeof(resp_buf) - len, ",\"val\":%d}", s);
            }
        }
    }

    /* ========================================================================
     * 8. GALERIA, WALLPAPERS E ANIMAÇÕES GIF
     * ======================================================================== */
    else if (strcmp(cmd, "get_media_info") == 0) {
        media_catalog_t cat;
        if (media_storage_get_catalog(&cat) == ESP_OK) {
            char *big_resp = (char *)heap_caps_malloc(1024, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (big_resp) {
                int offset = phone_ctl_format_resp_header(big_resp, 1024, "ok", cmd, id_item);
                offset += snprintf(big_resp + offset, 1024 - offset,
                                   ",\"active_img\":%u,\"active_gif\":%u,\"images\":[",
                                   cat.active_img_idx, cat.active_gif_idx);
                for (int i = 0; i < MEDIA_STORAGE_MAX_IMAGES; i++) {
                    offset += snprintf(big_resp + offset, 1024 - offset,
                                       "{\"slot\":%d,\"has_data\":%s,\"name\":\"%s\",\"size\":%" PRIu32 "}%s",
                                       i, cat.images[i].has_data ? "true" : "false",
                                       cat.images[i].name, cat.images[i].size,
                                       (i < MEDIA_STORAGE_MAX_IMAGES - 1) ? "," : "");
                }
                offset += snprintf(big_resp + offset, 1024 - offset, "],\"gifs\":[");
                for (int i = 0; i < MEDIA_STORAGE_MAX_GIFS; i++) {
                    offset += snprintf(big_resp + offset, 1024 - offset,
                                       "{\"slot\":%d,\"has_data\":%s,\"name\":\"%s\",\"size\":%" PRIu32 "}%s",
                                       i, cat.gifs[i].has_data ? "true" : "false",
                                       cat.gifs[i].name, cat.gifs[i].size,
                                       (i < MEDIA_STORAGE_MAX_GIFS - 1) ? "," : "");
                }
                snprintf(big_resp + offset, 1024 - offset, "]}");
                bt_ctl_service_send_notify(conn_id, (const uint8_t *)big_resp, strlen(big_resp));
                heap_caps_free(big_resp);
                cJSON_Delete(root);
                return;
            } else {
                phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "no_mem", "psram_allocation_failed");
            }
        } else {
            phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "storage_error", "failed_to_read_catalog");
        }
    }
    else if (strcmp(cmd, "set_wallpaper_idx") == 0) {
        cJSON *val = cJSON_GetObjectItem(root, "val");
        if (!val) {
            phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "missing_arg", "val is required");
        } else if (!cJSON_IsNumber(val)) {
            phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "invalid_type", "val must be a number");
        } else {
            int idx = val->valueint;
            if (idx < 0 || idx >= MEDIA_STORAGE_MAX_IMAGES) {
                phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "out_of_range", "val must be between 0 and 5");
            } else {
                ui_bridge_display_set_imagem_principal(idx);
                int len = phone_ctl_format_resp_header(resp_buf, sizeof(resp_buf), "ok", cmd, id_item);
                snprintf(resp_buf + len, sizeof(resp_buf) - len, ",\"active_img\":%d}", idx);
            }
        }
    }
    else if (strcmp(cmd, "set_gif_idx") == 0) {
        cJSON *val = cJSON_GetObjectItem(root, "val");
        if (!val) {
            phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "missing_arg", "val is required");
        } else if (!cJSON_IsNumber(val)) {
            phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "invalid_type", "val must be a number");
        } else {
            int idx = val->valueint;
            if (idx < 0 || idx >= MEDIA_STORAGE_MAX_GIFS) {
                phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "out_of_range", "val must be between 0 and 5");
            } else {
                ui_bridge_display_set_gif_principal(idx);
                int len = phone_ctl_format_resp_header(resp_buf, sizeof(resp_buf), "ok", cmd, id_item);
                snprintf(resp_buf + len, sizeof(resp_buf) - len, ",\"active_gif\":%d}", idx);
            }
        }
    }
    else if (strcmp(cmd, "media_upload_start") == 0) {
        cJSON *type_item = cJSON_GetObjectItem(root, "type");
        cJSON *slot_item = cJSON_GetObjectItem(root, "slot");
        cJSON *size_item = cJSON_GetObjectItem(root, "size");
        cJSON *crc_item  = cJSON_GetObjectItem(root, "crc32");
        cJSON *name_item = cJSON_GetObjectItem(root, "name");

        if (!type_item || !slot_item || !size_item || !cJSON_IsNumber(slot_item) || !cJSON_IsNumber(size_item)) {
            phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "missing_arg", "type, slot and size are required");
        } else {
            bool is_gif = (strcmp(type_item->valuestring, "gif") == 0);
            int slot = slot_item->valueint;
            uint32_t size = (uint32_t)size_item->valueint;

            if (slot < 0 || slot >= 6) {
                phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "out_of_range", "slot must be between 0 and 5");
            } else if (!is_gif && size != MEDIA_IMAGE_RGB565_SIZE) {
                phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "invalid_image_size", "expected 142080 bytes for RGB565");
            } else if (is_gif && (size == 0 || size > MEDIA_MAX_GIF_SIZE)) {
                phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "gif_too_large", "maximum GIF size is 400 KB");
            } else {
                if (s_upload_ctx.buffer) {
                    heap_caps_free(s_upload_ctx.buffer);
                    s_upload_ctx.buffer = NULL;
                }
                s_upload_ctx.buffer = (uint8_t *)heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                if (!s_upload_ctx.buffer) {
                    phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "no_mem", "psram_allocation_failed");
                } else {
                    s_upload_ctx.active = true;
                    s_upload_ctx.is_gif = is_gif;
                    s_upload_ctx.slot = (uint8_t)slot;
                    s_upload_ctx.expected_size = size;
                    s_upload_ctx.received_bytes = 0;
                    s_upload_ctx.expected_crc32 = 0;
                    if (crc_item && cJSON_IsString(crc_item)) {
                        s_upload_ctx.expected_crc32 = (uint32_t)strtoul(crc_item->valuestring, NULL, 16);
                    }
                    s_upload_ctx.name[0] = '\0';
                    if (name_item && cJSON_IsString(name_item)) {
                        strncpy(s_upload_ctx.name, name_item->valuestring, sizeof(s_upload_ctx.name) - 1);
                        s_upload_ctx.name[sizeof(s_upload_ctx.name) - 1] = '\0';
                    }
                    int len = phone_ctl_format_resp_header(resp_buf, sizeof(resp_buf), "ok", cmd, id_item);
                    snprintf(resp_buf + len, sizeof(resp_buf) - len,
                             ",\"type\":\"%s\",\"slot\":%d,\"chunk_size\":480}",
                             is_gif ? "gif" : "image", slot);
                }
            }
        }
    }
    else if (strcmp(cmd, "media_chunk") == 0) {
        if (!s_upload_ctx.active || !s_upload_ctx.buffer) {
            phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "no_session", "media_upload_start must be called first");
        } else {
            cJSON *seq_item = cJSON_GetObjectItem(root, "seq");
            cJSON *data_item = cJSON_GetObjectItem(root, "data");
            if (!seq_item || !data_item || !cJSON_IsString(data_item)) {
                phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "missing_arg", "seq and data string are required");
            } else {
                int seq = seq_item->valueint;
                const char *b64_str = data_item->valuestring;
                size_t b64_len = strlen(b64_str);
                size_t remain_buf = s_upload_ctx.expected_size - s_upload_ctx.received_bytes;
                size_t written = 0;

                int b64_err = mbedtls_base64_decode(s_upload_ctx.buffer + s_upload_ctx.received_bytes,
                                                    remain_buf, &written,
                                                    (const unsigned char *)b64_str, b64_len);
                if (b64_err != 0) {
                    phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "base64_error", "failed to decode chunk");
                } else {
                    s_upload_ctx.received_bytes += (uint32_t)written;
                    int len = phone_ctl_format_resp_header(resp_buf, sizeof(resp_buf), "ack", cmd, id_item);
                    snprintf(resp_buf + len, sizeof(resp_buf) - len,
                             ",\"seq\":%d,\"received\":%" PRIu32 ",\"total\":%" PRIu32 "}",
                             seq, s_upload_ctx.received_bytes, s_upload_ctx.expected_size);
                }
            }
        }
    }
    else if (strcmp(cmd, "media_upload_finish") == 0) {
        if (!s_upload_ctx.active || !s_upload_ctx.buffer) {
            phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "no_session", "no active media upload session");
        } else if (s_upload_ctx.received_bytes != s_upload_ctx.expected_size) {
            phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "incomplete_data", "received bytes do not match expected size");
        } else {
            esp_err_t s_err;
            if (s_upload_ctx.is_gif) {
                s_err = media_storage_save_gif(s_upload_ctx.slot, s_upload_ctx.name,
                                               s_upload_ctx.buffer, s_upload_ctx.received_bytes,
                                               s_upload_ctx.expected_crc32);
            } else {
                s_err = media_storage_save_image(s_upload_ctx.slot, s_upload_ctx.name,
                                                 s_upload_ctx.buffer, s_upload_ctx.received_bytes,
                                                 s_upload_ctx.expected_crc32);
            }

            if (s_err == ESP_OK) {
                int len = phone_ctl_format_resp_header(resp_buf, sizeof(resp_buf), "ok", cmd, id_item);
                snprintf(resp_buf + len, sizeof(resp_buf) - len,
                         ",\"type\":\"%s\",\"slot\":%u,\"verified\":true,\"name\":\"%s\"}",
                         s_upload_ctx.is_gif ? "gif" : "image", s_upload_ctx.slot, s_upload_ctx.name);
            } else if (s_err == ESP_ERR_INVALID_CRC) {
                phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "crc_mismatch", "data checksum does not match expected_crc32");
            } else {
                phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "save_nvs_failed", "flash commit operation failed");
            }

            heap_caps_free(s_upload_ctx.buffer);
            s_upload_ctx.buffer = NULL;
            s_upload_ctx.active = false;
        }
    }
    else if (strcmp(cmd, "delete_media_slot") == 0) {
        cJSON *type_item = cJSON_GetObjectItem(root, "type");
        cJSON *slot_item = cJSON_GetObjectItem(root, "slot");
        if (!type_item || !slot_item || !cJSON_IsNumber(slot_item)) {
            phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "missing_arg", "type and slot are required");
        } else {
            bool is_gif = (strcmp(type_item->valuestring, "gif") == 0);
            int slot = slot_item->valueint;
            if (slot < 0 || slot >= 6) {
                phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "out_of_range", "slot must be between 0 and 5");
            } else {
                media_storage_delete(is_gif, (uint8_t)slot);
                int len = phone_ctl_format_resp_header(resp_buf, sizeof(resp_buf), "ok", cmd, id_item);
                snprintf(resp_buf + len, sizeof(resp_buf) - len,
                         ",\"type\":\"%s\",\"slot\":%d,\"deleted\":true}",
                         is_gif ? "gif" : "image", slot);
            }
        }
    }
    else if (strcmp(cmd, "erase_all_images") == 0) {
        media_storage_erase_all_images();
        int len = phone_ctl_format_resp_header(resp_buf, sizeof(resp_buf), "ok", cmd, id_item);
        snprintf(resp_buf + len, sizeof(resp_buf) - len, ",\"erased\":true}");
    }
    else if (strcmp(cmd, "format_media") == 0) {
        media_storage_format();
        int len = phone_ctl_format_resp_header(resp_buf, sizeof(resp_buf), "ok", cmd, id_item);
        snprintf(resp_buf + len, sizeof(resp_buf) - len, ",\"formatted\":true}");
    }

    /* ========================================================================
     * 9. CONFIGURAÇÃO GOOGLE FAST PAIR (MODEL ID)
     * ======================================================================== */
    else if (strcmp(cmd, "get_model_id") == 0) {
        uint8_t mid[3] = {0};
        if (bt_fastpair_get_model_id(mid) == ESP_OK) {
            int len = phone_ctl_format_resp_header(resp_buf, sizeof(resp_buf), "ok", cmd, id_item);
            snprintf(resp_buf + len, sizeof(resp_buf) - len,
                     ",\"model_id\":\"%02X%02X%02X\"}", mid[0], mid[1], mid[2]);
        } else {
            phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "fastpair_disabled", "fast pair not initialized");
        }
    }
    else if (strcmp(cmd, "set_model_id") == 0) {
        cJSON *hex_str = cJSON_GetObjectItem(root, "val");
        if (!hex_str || !cJSON_IsString(hex_str)) {
            phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "missing_arg", "val string is required");
        } else {
            const char *s = hex_str->valuestring;
            if (strncmp(s, "0x", 2) == 0 || strncmp(s, "0X", 2) == 0) {
                s += 2;
            }
            if (strlen(s) != 6) {
                phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "invalid_len", "val must have exactly 6 hex characters (3 bytes)");
            } else {
                uint8_t new_id[3] = {0};
                char byte_str[3] = {0};
                bool ok = true;
                for (int i = 0; i < 3; i++) {
                    byte_str[0] = s[i * 2];
                    byte_str[1] = s[i * 2 + 1];
                    char *endp = NULL;
                    long v = strtol(byte_str, &endp, 16);
                    if (!endp || *endp != '\0') {
                        ok = false;
                        break;
                    }
                    new_id[i] = (uint8_t)v;
                }
                if (ok && bt_fastpair_set_model_id(new_id) == ESP_OK) {
                    int len = phone_ctl_format_resp_header(resp_buf, sizeof(resp_buf), "ok", cmd, id_item);
                    snprintf(resp_buf + len, sizeof(resp_buf) - len,
                             ",\"model_id\":\"%02X%02X%02X\"}", new_id[0], new_id[1], new_id[2]);
                } else {
                    phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "invalid_hex", "failed to parse hex or write model id");
                }
            }
        }
    }

    /* ========================================================================
     * 10. TRANSPORTE DE MÍDIA AVRCP (PASSTHROUGH)
     * ======================================================================== */
    else if (strcmp(cmd, "media_play_pause") == 0) {
        ESP_LOGI(TAG, "Comando BLE: Play/Pause");
        bt_avrcp_send_play();
        int len = phone_ctl_format_resp_header(resp_buf, sizeof(resp_buf), "ok", cmd, id_item);
        snprintf(resp_buf + len, sizeof(resp_buf) - len, ",\"action\":\"play_pause\"}");
    }
    else if (strcmp(cmd, "media_play") == 0) {
        ESP_LOGI(TAG, "Comando BLE: Play");
        bt_avrcp_send_play();
        int len = phone_ctl_format_resp_header(resp_buf, sizeof(resp_buf), "ok", cmd, id_item);
        snprintf(resp_buf + len, sizeof(resp_buf) - len, ",\"action\":\"play\"}");
    }
    else if (strcmp(cmd, "media_pause") == 0) {
        ESP_LOGI(TAG, "Comando BLE: Pause");
        bt_avrcp_send_pause();
        int len = phone_ctl_format_resp_header(resp_buf, sizeof(resp_buf), "ok", cmd, id_item);
        snprintf(resp_buf + len, sizeof(resp_buf) - len, ",\"action\":\"pause\"}");
    }
    else if (strcmp(cmd, "media_next") == 0) {
        ESP_LOGI(TAG, "Comando BLE: Next Track");
        bt_avrcp_send_next();
        int len = phone_ctl_format_resp_header(resp_buf, sizeof(resp_buf), "ok", cmd, id_item);
        snprintf(resp_buf + len, sizeof(resp_buf) - len, ",\"action\":\"next\"}");
    }
    else if (strcmp(cmd, "media_prev") == 0) {
        ESP_LOGI(TAG, "Comando BLE: Previous Track");
        bt_avrcp_send_prev();
        int len = phone_ctl_format_resp_header(resp_buf, sizeof(resp_buf), "ok", cmd, id_item);
        snprintf(resp_buf + len, sizeof(resp_buf) - len, ",\"action\":\"prev\"}");
    }

    /* ========================================================================
     * 11. EQUALIZADOR PARAMÉTRICO DE HARDWARE (WM8960)
     * ======================================================================== */
    else if (strcmp(cmd, "set_eq_preset") == 0) {
        cJSON *p_item = cJSON_GetObjectItem(root, "preset");
        if (!p_item) p_item = cJSON_GetObjectItem(root, "val");
        if (!p_item) {
            phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "missing_arg", "preset is required (0..3 or 255)");
        } else if (!cJSON_IsNumber(p_item)) {
            phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "invalid_type", "preset must be a number");
        } else {
            int p = p_item->valueint;
            if (p < 0 || (p > 3 && p != 255)) {
                phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "out_of_range", "preset must be 0..3 or 255");
            } else {
                uint8_t preset = (uint8_t)p;
                audio_cmd_set_eq_send(preset, NULL);
                int len = phone_ctl_format_resp_header(resp_buf, sizeof(resp_buf), "ok", cmd, id_item);
                snprintf(resp_buf + len, sizeof(resp_buf) - len, ",\"preset\":%u}", (unsigned int)preset);
            }
        }
    }
    else if (strcmp(cmd, "set_eq_gains") == 0) {
        cJSON *gains_arr = cJSON_GetObjectItem(root, "gains");
        if (!gains_arr) {
            phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "missing_arg", "gains array is required");
        } else if (!cJSON_IsArray(gains_arr) || cJSON_GetArraySize(gains_arr) != 5) {
            phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "invalid_args", "gains must be an array of 5 numbers (-24 to +24)");
        } else {
            int8_t gains[5];
            bool err_gain = false;
            for (int i = 0; i < 5; i++) {
                cJSON *g = cJSON_GetArrayItem(gains_arr, i);
                if (!g || !cJSON_IsNumber(g)) {
                    err_gain = true;
                    break;
                }
                int gv = g->valueint;
                if (gv < -24 || gv > 24) {
                    err_gain = true;
                    break;
                }
                gains[i] = (int8_t)gv;
            }
            if (err_gain) {
                phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "out_of_range", "each gain must be between -24 and +24");
            } else {
                audio_cmd_set_eq_send(0xFF, gains);
                int len = phone_ctl_format_resp_header(resp_buf, sizeof(resp_buf), "ok", cmd, id_item);
                snprintf(resp_buf + len, sizeof(resp_buf) - len,
                         ",\"gains\":[%d,%d,%d,%d,%d]}", gains[0], gains[1], gains[2], gains[3], gains[4]);
            }
        }
    }

    /* ========================================================================
     * 12. DIAGNÓSTICO E KEEPALIVE
     * ======================================================================== */
    else if (strcmp(cmd, "ping") == 0) {
        int len = phone_ctl_format_resp_header(resp_buf, sizeof(resp_buf), "ok", cmd, id_item);
        snprintf(resp_buf + len, sizeof(resp_buf) - len, ",\"pong\":true}");
    }
    else {
        phone_ctl_format_err(resp_buf, sizeof(resp_buf), cmd, id_item, "unknown_cmd", "command not recognized");
    }

    cJSON_Delete(root);

    /* Envia resposta via GATT Notify para o cliente conectado */
    if (resp_buf[0] != '\0') {
        bt_ctl_service_send_notify(conn_id, (const uint8_t *)resp_buf, strlen(resp_buf));
    }
}

/* ============================================================================
 * CICLO DE VIDA DO ACTOR phone_ctl
 * ============================================================================
 */

static void phone_ctl_on_start(void *ctx)
{
    ESP_LOGI(TAG, "Actor phone_ctl sob demanda iniciado (Stack alocada: %d B)", PHONE_CTL_STACK_SIZE);
}

static void phone_ctl_on_stop(void *ctx)
{
    ESP_LOGI(TAG, "Actor phone_ctl encerrou por inatividade (-%d B liberados de stack)", PHONE_CTL_STACK_SIZE);
}

static void phone_ctl_on_msg(hs_actor_t *self, const hs_msg_t *msg)
{
    if (!msg) return;

    switch (msg->cmd) {
    case PHONE_CMD_PROCESS_MSG: {
        const phone_cmd_msg_t *p = (const phone_cmd_msg_t *)msg->data;
        if (p->len == 0) break;

        /* Monta buffer local protegido por mutex */
        xSemaphoreTake(s_ctl_mtx, portMAX_DELAY);
        if (s_incoming_len + p->len < INCOMING_BUF_MAX - 1) {
            memcpy(s_incoming_buf + s_incoming_len, p->raw, p->len);
            s_incoming_len += p->len;
            s_incoming_buf[s_incoming_len] = '\0';

            /* Verifica se formou um objeto JSON completo ou linha terminada */
            if (s_incoming_buf[s_incoming_len - 1] == '}' || s_incoming_buf[s_incoming_len - 1] == '\n') {
                char temp[INCOMING_BUF_MAX];
                strncpy(temp, s_incoming_buf, sizeof(temp) - 1);
                temp[sizeof(temp) - 1] = '\0';
                s_incoming_len = 0;
                xSemaphoreGive(s_ctl_mtx);

                handle_json_command(p->conn_id, temp);
                return;
            }
        } else {
            /* Overflow no buffer: reseta para evitar estado preso */
            s_incoming_len = 0;
        }
        xSemaphoreGive(s_ctl_mtx);
        break;
    }

    case PHONE_CMD_CLIENT_CONNECTED: {
        const phone_cmd_msg_t *p = (const phone_cmd_msg_t *)msg->data;
        uint16_t conn = p ? p->conn_id : 0;
        ESP_LOGI(TAG, "Notificacoes BLE (CCCD) ativadas pelo cliente conn_id %u. Enviando app.connected proativo.", conn);
        const char *welcome = "{\"evt\":\"app.connected\",\"data\":{\"status\":\"ready\",\"name\":\"HeadSet4\",\"fw_version\":\"1.0.0\",\"api_version\":1,\"mtu\":512}}";
        bt_ctl_service_send_notify(conn, (const uint8_t *)welcome, strlen(welcome));
        break;
    }

    case PHONE_CMD_CLIENT_DISCONNECTED: {
        xSemaphoreTake(s_ctl_mtx, portMAX_DELAY);
        s_incoming_len = 0;
        /* Se houver sessao de upload de imagem ou GIF ativa no momento da desconexao,
         * desaloca imediatamente o buffer na PSRAM para evitar vazamento de memoria. */
        if (s_upload_ctx.active && s_upload_ctx.buffer) {
            ESP_LOGW(TAG, "Conexao BLE interrompida durante upload de midia! Desalocando buffer PSRAM.");
            heap_caps_free(s_upload_ctx.buffer);
            s_upload_ctx.buffer = NULL;
            s_upload_ctx.active = false;
        }
        xSemaphoreGive(s_ctl_mtx);
        ESP_LOGI(TAG, "Cliente desconectado: aguardando timeout de inatividade para encerramento");
        break;
    }

    default:
        break;
    }
}

/* ============================================================================
 * INTERFACE PÚBLICA
 * ============================================================================
 */

esp_err_t phone_ctl_init(void)
{
    if (s_phone_actor) {
        return ESP_OK;
    }

    s_ctl_mtx = xSemaphoreCreateMutex();
    if (!s_ctl_mtx) {
        return ESP_ERR_NO_MEM;
    }

    hs_actor_cfg_t cfg = {
        .name       = "act_phone_ctl",
        .stack      = PHONE_CTL_STACK_SIZE,
        .prio       = 5,
        .core       = 0,
        .queue_len  = PHONE_CTL_QUEUE_LEN,
        .idle_ms    = PHONE_CTL_IDLE_MS,
        .on_msg     = phone_ctl_on_msg,
        .on_start   = phone_ctl_on_start,
        .on_stop    = phone_ctl_on_stop,
        .ctx        = NULL
    };

    esp_err_t err = hs_actor_create(&cfg, &s_phone_actor);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao criar actor phone_ctl: %s", esp_err_to_name(err));
        return err;
    }

    /* Inscreve nos eventos do barramento central hs_ctrl_evt (AGENTS.md WP 7.3 / R.3) */
    hs_event_register(SENSOR_EVT, ESP_EVENT_ANY_ID, on_sensor_event, NULL);
    hs_event_register(BT_EVT, ESP_EVENT_ANY_ID, on_bt_event, NULL);
    hs_event_register(AUDIO_EVT, ESP_EVENT_ANY_ID, on_audio_event, NULL);
    hs_event_register(CFG_EVT, ESP_EVENT_ANY_ID, on_cfg_event, NULL);

    ESP_LOGI(TAG, "Actor phone_ctl configurado com sucesso (idle_ms=%u)", (unsigned int)PHONE_CTL_IDLE_MS);
    return ESP_OK;
}

hs_actor_t *phone_ctl_get_actor(void)
{
    return s_phone_actor;
}

esp_err_t phone_ctl_post_incoming_data(uint16_t conn_id, const uint8_t *data, size_t len)
{
    if (!s_phone_actor || !data || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Fragmenta chunks em pedacos compativeis com o payload inline de comando */
    size_t offset = 0;
    while (offset < len) {
        size_t chunk_len = len - offset;
        if (chunk_len > sizeof(((phone_cmd_msg_t *)0)->raw)) {
            chunk_len = sizeof(((phone_cmd_msg_t *)0)->raw);
        }

        phone_cmd_msg_t cmd_msg;
        cmd_msg.conn_id = conn_id;
        cmd_msg.len = (uint16_t)chunk_len;
        memcpy(cmd_msg.raw, data + offset, chunk_len);

        esp_err_t err = hs_actor_send(s_phone_actor, PHONE_CMD_PROCESS_MSG, &cmd_msg, sizeof(cmd_msg), pdMS_TO_TICKS(50));
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Falha ao enfileirar chunk no actor phone_ctl: %s", esp_err_to_name(err));
            return err;
        }
        offset += chunk_len;
    }

    return ESP_OK;
}

void phone_ctl_notify_connect(uint16_t conn_id)
{
    if (s_phone_actor) {
        phone_cmd_msg_t cmd_msg = {
            .conn_id = conn_id,
            .len = 0
        };
        hs_actor_send(s_phone_actor, PHONE_CMD_CLIENT_CONNECTED, &cmd_msg, sizeof(cmd_msg), pdMS_TO_TICKS(50));
    }
}

void phone_ctl_notify_disconnect(uint16_t conn_id)
{
    if (s_phone_actor) {
        hs_actor_send(s_phone_actor, PHONE_CMD_CLIENT_DISCONNECTED, NULL, 0, 0);
    }
}

/**
 * @brief Notifica o aplicativo Android conectado sobre a alteração da mídia ativa (feita no fone).
 *
 * Dispara uma notificação GATT com evento estruturado JSON ("display.wallpaper_changed" ou
 * "display.gif_changed"). Esta notificação informa ao app qual o índice ativo (0..5) e o
 * nome correspondente do arquivo ou slot, permitindo ao app sincronizar a sua UI em tempo
 * real quando o usuário troca o papel de parede ou animação diretamente na tela do headset.
 *
 * @param is_gif true se for alteração de animação GIF, false se for papel de parede estático.
 * @param active_idx Índice ativo selecionado (0 a 5).
 * @param name Nome textual descritivo do papel de parede ou animação GIF.
 */
void phone_ctl_notify_media_changed(bool is_gif, uint8_t active_idx, const char *name)
{
    char buf[128];
    snprintf(buf, sizeof(buf),
             "{\"evt\":\"%s\",\"data\":{\"active_idx\":%u,\"name\":\"%s\"}}",
             is_gif ? "display.gif_changed" : "display.wallpaper_changed",
             active_idx,
             name ? name : "");

    /* Envia a notificacao via GATT Notify para o conn_id ativo (se houver cliente conectado) */
    uint16_t conn_id = bt_ctl_service_get_conn_id();
    if (conn_id != 0xFFFF) {
        bt_ctl_service_send_notify(conn_id, (const uint8_t *)buf, strlen(buf));
        ESP_LOGI(TAG, "Notificacao de midia alterada transmitida via BLE para conn_id %u: %s", conn_id, buf);
    }
}

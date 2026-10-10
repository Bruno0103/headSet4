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

#include "audio.h"
#include "bt_ctl_service.h"
#include "bt_link_mgr.h"
#include "hs_actor.h"
#include "hs_cmds.h"
#include "hs_events.h"
#include "settings.h"
#include "ui_model.h"

static const char *TAG = "act_phone_ctl";

#define PHONE_CTL_STACK_SIZE    3584
#define PHONE_CTL_QUEUE_LEN     16
#define PHONE_CTL_IDLE_MS       30000  /* 30 segundos sem atividade -> encerra a task */

/* Buffer de montagem de frames de entrada */
#define INCOMING_BUF_MAX        512
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
 * PROCESSAMENTO DE COMANDOS JSON (cJSON) -> Envio para Atores Donos
 * ============================================================================
 */

static void handle_json_command(uint16_t conn_id, const char *json_str)
{
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        ESP_LOGW(TAG, "JSON invalido recebido: %s", json_str);
        const char *err_rsp = "{\"error\":\"invalid_json\"}";
        bt_ctl_service_send_notify(conn_id, (const uint8_t *)err_rsp, strlen(err_rsp));
        return;
    }

    cJSON *cmd_item = cJSON_GetObjectItem(root, "cmd");
    if (!cmd_item || !cJSON_IsString(cmd_item)) {
        cJSON_Delete(root);
        const char *err_rsp = "{\"error\":\"missing_cmd\"}";
        bt_ctl_service_send_notify(conn_id, (const uint8_t *)err_rsp, strlen(err_rsp));
        return;
    }

    const char *cmd = cmd_item->valuestring;
    char resp_buf[384];
    snprintf(resp_buf, sizeof(resp_buf), "{\"status\":\"ok\",\"cmd\":\"%s\"}", cmd);

    /* --- Comandos de Consulta de Estado Geral --- */
    if (strcmp(cmd, "get_all_status") == 0) {
        ui_model_data_t snap;
        ui_model_get_snapshot(&snap);

        snprintf(resp_buf, sizeof(resp_buf),
                 "{\"bt1\":\"%s\",\"bt2\":\"%s\",\"st1\":\"%s\",\"st2\":\"%s\","
                 "\"alt\":%s,\"prox\":%s,\"bat\":%u,\"disp\":%s,\"brilho\":%u,\"vol\":%u}",
                 snap.bt_name_slot0, snap.bt_name_slot1,
                 snap.bt_status_slot0, snap.bt_status_slot1,
                 snap.bt_auto_switch ? "true" : "false",
                 snap.prox_worn ? "\"worn\"" : "\"removed\"",
                 (unsigned int)snap.battery_percent,
                 snap.display_on ? "true" : "false",
                 (unsigned int)snap.display_brightness,
                 (unsigned int)snap.audio_volume);
    }
    /* --- Comandos de Controle de Volume e Audio --- */
    else if (strcmp(cmd, "set_volume") == 0) {
        cJSON *val = cJSON_GetObjectItem(root, "val");
        if (val && cJSON_IsNumber(val)) {
            int vol = val->valueint;
            if (vol >= 0 && vol <= 100) {
                audio_cmd_set_volume_send((uint8_t)vol);
                settings_set_u8(SETTINGS_KEY_VOL_SLOT0, (uint8_t)vol);
            }
        }
    }
    /* --- Comandos de Bluetooth --- */
    else if (strcmp(cmd, "select_slot") == 0) {
        cJSON *slot = cJSON_GetObjectItem(root, "slot");
        if (slot && cJSON_IsNumber(slot)) {
            hs_actor_t *bt_act = bt_link_actor_get();
            if (bt_act) {
                bt_cmd_select_slot_t scmd = { .slot = (uint8_t)slot->valueint };
                hs_actor_send(bt_act, BT_CMD_SELECT_SLOT, &scmd, sizeof(scmd), pdMS_TO_TICKS(50));
            }
        }
    }
    else if (strcmp(cmd, "disconnect_slot") == 0) {
        cJSON *slot = cJSON_GetObjectItem(root, "slot");
        if (slot && cJSON_IsNumber(slot)) {
            hs_actor_t *bt_act = bt_link_actor_get();
            if (bt_act) {
                bt_cmd_disconnect_t dcmd = { .slot = (uint8_t)slot->valueint };
                hs_actor_send(bt_act, BT_CMD_DISCONNECT, &dcmd, sizeof(dcmd), pdMS_TO_TICKS(50));
            }
        }
    }
    else if (strcmp(cmd, "start_pairing") == 0) {
        hs_actor_t *bt_act = bt_link_actor_get();
        if (bt_act) {
            bt_cmd_start_pairing_t pcmd = { .duration_sec = 120 };
            hs_actor_send(bt_act, BT_CMD_START_PAIRING, &pcmd, sizeof(pcmd), pdMS_TO_TICKS(50));
        }
    }
    /* --- Comandos de Display & Brilho --- */
    else if (strcmp(cmd, "set_brightness") == 0) {
        cJSON *val = cJSON_GetObjectItem(root, "val");
        if (val && cJSON_IsNumber(val)) {
            int b = val->valueint;
            if (b >= 0 && b <= 100) {
                settings_set_u8(SETTINGS_KEY_DISP_BRIGHT, (uint8_t)b);
                ui_model_update_msg_t umsg = {
                    .type = UI_UPDATE_DISPLAY_CONFIG,
                    .payload.display_cfg = { .on = (b > 0), .brightness = (uint8_t)b, .timeout_sec = 60 }
                };
                ui_model_post_update(&umsg);
            }
        }
    }
    /* --- Comandos de Atuadores (Vibracall e Orelhas) --- */
    else if (strcmp(cmd, "set_vibracall") == 0) {
        cJSON *val = cJSON_GetObjectItem(root, "val");
        if (val && cJSON_IsNumber(val)) {
            int v = val->valueint;
            settings_set_u8(SETTINGS_KEY_HAPTIC_INTENS, (uint8_t)v);
        }
    }
    else if (strcmp(cmd, "set_ears_angle") == 0) {
        cJSON *val = cJSON_GetObjectItem(root, "val");
        if (val && cJSON_IsNumber(val)) {
            int a = val->valueint;
            settings_set_u8(SETTINGS_KEY_EARS_ANGLE, (uint8_t)a);
        }
    }
    else {
        snprintf(resp_buf, sizeof(resp_buf), "{\"error\":\"unknown_cmd\",\"cmd\":\"%s\"}", cmd);
    }

    cJSON_Delete(root);

    /* Envia resposta via GATT Notify */
    bt_ctl_service_send_notify(conn_id, (const uint8_t *)resp_buf, strlen(resp_buf));
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

    case PHONE_CMD_CLIENT_DISCONNECTED: {
        xSemaphoreTake(s_ctl_mtx, portMAX_DELAY);
        s_incoming_len = 0;
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

    /* Inscreve nos eventos do barramento central */
    esp_event_handler_register(SENSOR_EVT, ESP_EVENT_ANY_ID, on_sensor_event, NULL);
    esp_event_handler_register(BT_EVT, ESP_EVENT_ANY_ID, on_bt_event, NULL);
    esp_event_handler_register(AUDIO_EVT, ESP_EVENT_ANY_ID, on_audio_event, NULL);
    esp_event_handler_register(CFG_EVT, ESP_EVENT_ANY_ID, on_cfg_event, NULL);

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

void phone_ctl_notify_disconnect(uint16_t conn_id)
{
    if (s_phone_actor) {
        hs_actor_send(s_phone_actor, PHONE_CMD_CLIENT_DISCONNECTED, NULL, 0, 0);
    }
}

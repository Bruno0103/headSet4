/**
 * @file ui_model.c
 * @brief Implementação do modelo reativo de dados da interface e subjects LVGL 9.5.
 *
 * Em conformidade com AGENTS.md (§4.2, WP 6.1):
 * - Mantém o estado da UI em memória estática, desacoplando completamente a GUI dos drivers.
 * - Suporta subjects reativos do LVGL 9 (lv_subject_t) para notificação automática de telas.
 * - Fornece fila thread-safe FreeRTOS (s_ui_update_queue) para que callbacks de eventos (esp_event)
 *   e outros atores enviem dados com segurança sem tocar diretamente em objetos ou subjects.
 * - Toda a drenagem e mutação de subjects acontece deterministicamente dentro de ui_model_drain_updates()
 *   executado na task do LVGL.
 */

#include "ui_model.h"

#include <string.h>
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "hs_events.h"
#include "headset_events.h"
#include "settings.h"

static const char *TAG = "ui_model";

#define UI_UPDATE_QUEUE_LEN 24

static QueueHandle_t s_ui_update_queue = NULL;
static SemaphoreHandle_t s_model_mutex = NULL;

/* Snapshot interno em RAM */
static ui_model_data_t s_model = {
    .bt_name_slot0 = "Dispositivo 1",
    .bt_name_slot1 = "Dispositivo 2",
    .bt_status_slot0 = "Desconectado",
    .bt_status_slot1 = "Desconectado",
    .bt_active_slot = 0,
    .bt_auto_switch = true,
    .bt_pairing_active = false,

    .battery_percent = 85,
    .battery_mv = 3950,
    .battery_charging = false,

    .prox_enabled = true,
    .prox_worn = true,
    .prox_sensitivity = 80,

    .vibracall_enabled = true,
    .vibracall_intensity = 80,
    .vibracall_active = false,

    .orelhas_enabled = true,
    .orelhas_max_angle = 120,

    .display_on = true,
    .display_brightness = 100,
    .display_timeout_sec = 30,
    .display_selected_img_idx = 0,
    .display_selected_gif_idx = 0,

    .audio_volume = 70,
    .audio_muted = false,
    .audio_eq_preset = 0,
};

/* Subjects reativos do LVGL 9 */
lv_subject_t g_subj_battery_percent;
lv_subject_t g_subj_battery_mv;
lv_subject_t g_subj_bt_active_slot;
lv_subject_t g_subj_display_brightness;
lv_subject_t g_subj_prox_worn;

static bool s_subjects_initialized = false;

/* ============================================================================
 * INICIALIZAÇÃO
 * ============================================================================ */

esp_err_t ui_model_init(void)
{
    if (s_ui_update_queue == NULL) {
        s_ui_update_queue = xQueueCreate(UI_UPDATE_QUEUE_LEN, sizeof(ui_model_update_msg_t));
        ESP_RETURN_ON_FALSE(s_ui_update_queue, ESP_ERR_NO_MEM, TAG, "queue create failed");
    }

    if (s_model_mutex == NULL) {
        s_model_mutex = xSemaphoreCreateMutex();
        ESP_RETURN_ON_FALSE(s_model_mutex, ESP_ERR_NO_MEM, TAG, "mutex create failed");
    }

    /* Leitura inicial das configurações persistidas no actor settings */
    uint8_t u8_val = 0;
    uint16_t u16_val = 0;

    if (settings_get_u8(SETTINGS_KEY_DISP_BRIGHT, &u8_val, 100) == ESP_OK) {
        s_model.display_brightness = u8_val;
    }
    if (settings_get_u16(SETTINGS_KEY_DISP_TIMEOUT, &u16_val, 30) == ESP_OK) {
        s_model.display_timeout_sec = u16_val;
    }
    if (settings_get_u8(SETTINGS_KEY_HAPTIC_INTENS, &u8_val, 80) == ESP_OK) {
        s_model.vibracall_intensity = u8_val;
    }
    if (settings_get_u8(SETTINGS_KEY_EARS_ANGLE, &u8_val, 120) == ESP_OK) {
        s_model.orelhas_max_angle = u8_val;
    }
    if (settings_get_u8(SETTINGS_KEY_BT_AUTOSWITCH, &u8_val, 1) == ESP_OK) {
        s_model.bt_auto_switch = (u8_val != 0);
    }
    if (settings_get_u8(SETTINGS_KEY_BT_SEL_SLOT, &u8_val, 0) == ESP_OK) {
        s_model.bt_active_slot = u8_val;
    }

    /* Inicializa subjects do LVGL 9 (deve rodar após lv_init()) */
    if (!s_subjects_initialized) {
        lv_subject_init_int(&g_subj_battery_percent, s_model.battery_percent);
        lv_subject_init_int(&g_subj_battery_mv, s_model.battery_mv);
        lv_subject_init_int(&g_subj_bt_active_slot, s_model.bt_active_slot);
        lv_subject_init_int(&g_subj_display_brightness, s_model.display_brightness);
        lv_subject_init_int(&g_subj_prox_worn, s_model.prox_worn ? 1 : 0);
        s_subjects_initialized = true;
    }

    ESP_LOGI(TAG, "UI Model inicializado com sucesso (Subjects prontos)");
    return ESP_OK;
}

/* ============================================================================
 * TRATAMENTO DE ATUALIZAÇÕES E DRENAGEM (TASK LVGL)
 * ============================================================================ */

void ui_model_drain_updates(void)
{
    if (!s_ui_update_queue) return;

    ui_model_update_msg_t msg;
    while (xQueueReceive(s_ui_update_queue, &msg, 0) == pdTRUE) {
        if (s_model_mutex) xSemaphoreTake(s_model_mutex, portMAX_DELAY);

        switch (msg.type) {
            case UI_UPDATE_BT_SLOT_NAME:
                if (msg.payload.bt_name.slot == 0) {
                    strncpy(s_model.bt_name_slot0, msg.payload.bt_name.name, sizeof(s_model.bt_name_slot0) - 1);
                    s_model.bt_name_slot0[sizeof(s_model.bt_name_slot0) - 1] = '\0';
                } else if (msg.payload.bt_name.slot == 1) {
                    strncpy(s_model.bt_name_slot1, msg.payload.bt_name.name, sizeof(s_model.bt_name_slot1) - 1);
                    s_model.bt_name_slot1[sizeof(s_model.bt_name_slot1) - 1] = '\0';
                }
                break;

            case UI_UPDATE_BT_SLOT_STATUS:
                if (msg.payload.bt_status.slot == 0) {
                    strncpy(s_model.bt_status_slot0, msg.payload.bt_status.status, sizeof(s_model.bt_status_slot0) - 1);
                    s_model.bt_status_slot0[sizeof(s_model.bt_status_slot0) - 1] = '\0';
                } else if (msg.payload.bt_status.slot == 1) {
                    strncpy(s_model.bt_status_slot1, msg.payload.bt_status.status, sizeof(s_model.bt_status_slot1) - 1);
                    s_model.bt_status_slot1[sizeof(s_model.bt_status_slot1) - 1] = '\0';
                }
                break;

            case UI_UPDATE_BT_ACTIVE_SLOT:
                s_model.bt_active_slot = msg.payload.bt_slot.slot;
                if (s_subjects_initialized) {
                    lv_subject_set_int(&g_subj_bt_active_slot, s_model.bt_active_slot);
                }
                break;

            case UI_UPDATE_BT_AUTO_SWITCH:
                s_model.bt_auto_switch = msg.payload.bt_auto_sw.auto_switch;
                break;

            case UI_UPDATE_BT_PAIRING:
                s_model.bt_pairing_active = msg.payload.bt_pair.pairing;
                break;

            case UI_UPDATE_BATTERY:
                s_model.battery_percent = msg.payload.battery.percent;
                s_model.battery_mv = msg.payload.battery.mv;
                s_model.battery_charging = msg.payload.battery.charging;
                if (s_subjects_initialized) {
                    lv_subject_set_int(&g_subj_battery_percent, s_model.battery_percent);
                    lv_subject_set_int(&g_subj_battery_mv, s_model.battery_mv);
                }
                break;

            case UI_UPDATE_PROX_STATE:
                s_model.prox_worn = msg.payload.prox_state.worn;
                if (s_subjects_initialized) {
                    lv_subject_set_int(&g_subj_prox_worn, s_model.prox_worn ? 1 : 0);
                }
                break;

            case UI_UPDATE_PROX_CONFIG:
                s_model.prox_enabled = msg.payload.prox_cfg.enabled;
                s_model.prox_sensitivity = msg.payload.prox_cfg.sensitivity;
                break;

            case UI_UPDATE_VIBRACALL_STATE:
                s_model.vibracall_active = msg.payload.vib_state.active;
                break;

            case UI_UPDATE_VIBRACALL_CONFIG:
                s_model.vibracall_enabled = msg.payload.vib_cfg.enabled;
                s_model.vibracall_intensity = msg.payload.vib_cfg.intensity;
                break;

            case UI_UPDATE_ORELHAS_CONFIG:
                s_model.orelhas_enabled = msg.payload.orelhas_cfg.enabled;
                s_model.orelhas_max_angle = msg.payload.orelhas_cfg.max_angle;
                break;

            case UI_UPDATE_DISPLAY_CONFIG:
                s_model.display_on = msg.payload.display_cfg.on;
                s_model.display_brightness = msg.payload.display_cfg.brightness;
                s_model.display_timeout_sec = msg.payload.display_cfg.timeout_sec;
                s_model.display_selected_img_idx = msg.payload.display_cfg.img_idx;
                s_model.display_selected_gif_idx = msg.payload.display_cfg.gif_idx;
                if (s_subjects_initialized) {
                    lv_subject_set_int(&g_subj_display_brightness, s_model.display_brightness);
                }
                break;

            case UI_UPDATE_AUDIO_STATE:
                s_model.audio_volume = msg.payload.audio_state.volume;
                s_model.audio_muted = msg.payload.audio_state.muted;
                s_model.audio_eq_preset = msg.payload.audio_state.eq_preset;
                break;

            default:
                break;
        }

        if (s_model_mutex) xSemaphoreGive(s_model_mutex);
    }
}

void ui_model_get_snapshot(ui_model_data_t *out_data)
{
    if (!out_data) return;
    if (s_model_mutex) xSemaphoreTake(s_model_mutex, portMAX_DELAY);
    memcpy(out_data, &s_model, sizeof(ui_model_data_t));
    if (s_model_mutex) xSemaphoreGive(s_model_mutex);
}

esp_err_t ui_model_post_update(const ui_model_update_msg_t *msg)
{
    if (!s_ui_update_queue || !msg) return ESP_ERR_INVALID_STATE;
    if (xQueueSend(s_ui_update_queue, msg, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Fila de atualizacao da UI cheia, mensagem descartada");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

/* ============================================================================
 * HANDLERS DE EVENTOS (ESP_EVENT PUB/SUB)
 * ============================================================================ */

static void on_sensor_event(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    (void)handler_args; (void)base;
    if (id == SENSOR_EVT_WORN) {
        ui_model_update_msg_t msg = {
            .type = UI_UPDATE_PROX_STATE,
            .payload.prox_state = { .worn = true }
        };
        ui_model_post_update(&msg);
    } else if (id == SENSOR_EVT_REMOVED) {
        ui_model_update_msg_t msg = {
            .type = UI_UPDATE_PROX_STATE,
            .payload.prox_state = { .worn = false }
        };
        ui_model_post_update(&msg);
    } else if (id == SENSOR_EVT_BATTERY && event_data) {
        sensor_battery_evt_t *bat = (sensor_battery_evt_t *)event_data;
        ui_model_update_msg_t msg = {
            .type = UI_UPDATE_BATTERY,
            .payload.battery = {
                .percent = bat->percent,
                .mv = bat->millivolts,
                .charging = bat->is_charging,
            }
        };
        ui_model_post_update(&msg);
    }
}

static void on_bt_event(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    (void)handler_args; (void)base;
    if (id == BT_EVT_SLOT_CHANGED && event_data) {
        bt_slot_evt_t *ev = (bt_slot_evt_t *)event_data;
        ui_model_update_msg_t msg_slot = {
            .type = UI_UPDATE_BT_ACTIVE_SLOT,
            .payload.bt_slot = { .slot = ev->active_slot }
        };
        ui_model_post_update(&msg_slot);

        ui_model_update_msg_t msg_st = {
            .type = UI_UPDATE_BT_SLOT_STATUS,
            .payload.bt_status = {
                .slot = ev->active_slot,
            }
        };
        if (ev->is_connected) {
            strncpy(msg_st.payload.bt_status.status, "Conectado", sizeof(msg_st.payload.bt_status.status) - 1);
        } else {
            strncpy(msg_st.payload.bt_status.status, "Desconectado", sizeof(msg_st.payload.bt_status.status) - 1);
        }
        ui_model_post_update(&msg_st);
    } else if (id == BT_EVT_PEER_NAME && event_data) {
        bt_peer_name_evt_t *ev = (bt_peer_name_evt_t *)event_data;
        ui_model_update_msg_t msg = {
            .type = UI_UPDATE_BT_SLOT_NAME,
            .payload.bt_name = {
                .slot = ev->slot,
            }
        };
        strncpy(msg.payload.bt_name.name, ev->name, sizeof(msg.payload.bt_name.name) - 1);
        msg.payload.bt_name.name[sizeof(msg.payload.bt_name.name) - 1] = '\0';
        ui_model_post_update(&msg);
    } else if (id == BT_EVT_PAIRING_MODE && event_data) {
        bt_pairing_evt_t *ev = (bt_pairing_evt_t *)event_data;
        ui_model_update_msg_t msg = {
            .type = UI_UPDATE_BT_PAIRING,
            .payload.bt_pair = { .pairing = ev->active }
        };
        ui_model_post_update(&msg);
    }
}

static void on_audio_event(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    (void)handler_args; (void)base;
    if (id == AUDIO_EVT_VOLUME_CHANGED && event_data) {
        audio_volume_evt_t *ev = (audio_volume_evt_t *)event_data;
        ui_model_update_msg_t msg = {
            .type = UI_UPDATE_AUDIO_STATE,
            .payload.audio_state = {
                .volume = ev->volume_percent,
                .muted = ev->muted,
                .eq_preset = 0,
            }
        };
        ui_model_post_update(&msg);
    } else if (id == AUDIO_EVT_EQ_CHANGED && event_data) {
        audio_eq_evt_t *ev = (audio_eq_evt_t *)event_data;
        ui_model_update_msg_t msg = {
            .type = UI_UPDATE_AUDIO_STATE,
            .payload.audio_state = {
                .volume = 70,
                .muted = false,
                .eq_preset = ev->preset_index,
            }
        };
        ui_model_post_update(&msg);
    }
}

/* Shim legado HEADSET_EVENT para baterias ou eventos antigos */
static void on_headset_event_legacy(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    (void)handler_args; (void)base;
    if (id == HEADSET_EVT_BATTERY && event_data) {
        headset_battery_evt_t *ev = (headset_battery_evt_t *)event_data;
        ui_model_update_msg_t msg = {
            .type = UI_UPDATE_BATTERY,
            .payload.battery = {
                .percent = (uint8_t)ev->percent,
                .mv = (uint16_t)ev->millivolts,
                .charging = false,
            }
        };
        ui_model_post_update(&msg);
    } else if (id == HEADSET_EVT_WORN) {
        ui_model_update_msg_t msg = {
            .type = UI_UPDATE_PROX_STATE,
            .payload.prox_state = { .worn = true }
        };
        ui_model_post_update(&msg);
    } else if (id == HEADSET_EVT_REMOVED) {
        ui_model_update_msg_t msg = {
            .type = UI_UPDATE_PROX_STATE,
            .payload.prox_state = { .worn = false }
        };
        ui_model_post_update(&msg);
    }
}

esp_err_t ui_model_register_events(void)
{
    esp_event_handler_register(SENSOR_EVT, ESP_EVENT_ANY_ID, on_sensor_event, NULL);
    esp_event_handler_register(BT_EVT, ESP_EVENT_ANY_ID, on_bt_event, NULL);
    esp_event_handler_register(AUDIO_EVT, ESP_EVENT_ANY_ID, on_audio_event, NULL);
    headset_event_register(HEADSET_EVT_BATTERY, on_headset_event_legacy, NULL);
    headset_event_register(HEADSET_EVT_WORN, on_headset_event_legacy, NULL);
    headset_event_register(HEADSET_EVT_REMOVED, on_headset_event_legacy, NULL);

    ESP_LOGI(TAG, "UI Model inscrito nos eventos do sistema (SENSOR_EVT, BT_EVT, AUDIO_EVT)");
    return ESP_OK;
}

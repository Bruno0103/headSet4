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
#include "settings.h"
#include "ui_gen.h"
#include "display.h"

static const char *TAG = "ui_model";

#define UI_UPDATE_QUEUE_LEN 48

static QueueHandle_t s_ui_update_queue = NULL;
static SemaphoreHandle_t s_model_mutex = NULL;

/* Snapshot interno em RAM */
static ui_model_data_t s_model = {
    .bt_name_slot0 = CONST_BLUETOOTH_NOME_DISPOSITIVO_1_GLOBAL,
    .bt_name_slot1 = CONST_BLUETOOTH_NOME_DISPOSITIVO_2_GLOBAL,
    .bt_status_slot0 = CONST_BLUETOOTH_STATUS_DISPOSITIVO_1_GLOBAL,
    .bt_status_slot1 = CONST_BLUETOOTH_STATUS_DISPOSITIVO_2_GLOBAL,
    .bt_active_slot = 0,
    .bt_auto_switch = (CONST_BLUETOOTH_ALTERNANCIA_ATIVA_GLOBAL != 0),
    .bt_pairing_active = false,

    .battery_percent = CONST_BATERIA_PORCENTAGEM_GLOBAL,
    .battery_mv = CONST_BATERIA_TENSAO_MV_GLOBAL,
    .battery_charging = false,

    .prox_enabled = (CONST_PROXIMIDADE_ATIVO_GLOBAL != 0),
    .prox_worn = true,
    .prox_sensitivity = CONST_PROXIMIDADE_SENSIBILIDADE_GLOBAL,

    .vibracall_enabled = (CONST_VIBRACALL_ATIVO_GLOBAL != 0),
    .vibracall_intensity = CONST_VIBRACALL_INTENSIDADE_GLOBAL,
    .vibracall_active = false,

    .orelhas_enabled = (CONST_ORELHAS_ATIVO_GLOBAL != 0),
    .orelhas_max_angle = CONST_ORELHAS_ANGULO_MAXIMO_GLOBAL,

    .display_on = (CONST_DISPLAY_LIGADO_GLOBAL != 0),
    .display_brightness = CONST_DISPLAY_BRILHO_GLOBAL,
    .display_timeout_sec = CONST_DISPLAY_TIMEOUT_SEGUNDOS_GLOBAL,
    .display_selected_img_idx = 0,
    .display_selected_gif_idx = 0,

    .audio_volume = 70,
    .audio_muted = false,
    .audio_eq_preset = 0,

    .track_title = "Nome da musica",
    .track_artist = "Artista",
    .track_album = "Album / Playlist",
    .has_cover_art = false,
    .cover_art_dsc = NULL,
};

/* Subjects reativos do LVGL 9 */
/* --- Bateria --- */
lv_subject_t g_subj_bateria_status_texto;
lv_subject_t g_subj_bateria_porcentagem;
lv_subject_t g_subj_bateria_tensao_mv;
static char s_buf_bateria_status[UI_MODEL_STR_MAX_LEN] = CONST_BATERIA_STATUS_TEXTO_GLOBAL;

/* --- Bluetooth --- */
lv_subject_t g_subj_bt_nome_1;
lv_subject_t g_subj_bt_status_1;
lv_subject_t g_subj_bt_nome_2;
lv_subject_t g_subj_bt_status_2;
lv_subject_t g_subj_bt_active_slot;
lv_subject_t g_subj_bt_status_alternancia;
lv_subject_t g_subj_bt_alternancia_ativa;
static char s_buf_bt_nome_1[UI_MODEL_STR_MAX_LEN] = CONST_BLUETOOTH_NOME_DISPOSITIVO_1_GLOBAL;
static char s_buf_bt_status_1[UI_MODEL_STR_MAX_LEN] = CONST_BLUETOOTH_STATUS_DISPOSITIVO_1_GLOBAL;
static char s_buf_bt_nome_2[UI_MODEL_STR_MAX_LEN] = CONST_BLUETOOTH_NOME_DISPOSITIVO_2_GLOBAL;
static char s_buf_bt_status_2[UI_MODEL_STR_MAX_LEN] = CONST_BLUETOOTH_STATUS_DISPOSITIVO_2_GLOBAL;
static char s_buf_bt_alternancia[UI_MODEL_STR_MAX_LEN] = CONST_BLUETOOTH_STATUS_ALTERNANCIA_GLOBAL;

/* --- Proximidade --- */
lv_subject_t g_subj_proximidade_status_texto;
lv_subject_t g_subj_proximidade_ativo;
lv_subject_t g_subj_proximidade_sensibilidade;
lv_subject_t g_subj_prox_worn;
static char s_buf_prox_status[UI_MODEL_STR_MAX_LEN] = CONST_PROXIMIDADE_STATUS_TEXTO_GLOBAL;

/* --- Vibracall --- */
lv_subject_t g_subj_vibracall_status_texto;
lv_subject_t g_subj_vibracall_ativo;
lv_subject_t g_subj_vibracall_intensidade;
static char s_buf_vibracall_status[UI_MODEL_STR_MAX_LEN] = CONST_VIBRACALL_STATUS_TEXTO_GLOBAL;

/* --- Orelhas --- */
lv_subject_t g_subj_orelhas_status_texto;
lv_subject_t g_subj_orelhas_ativo;
lv_subject_t g_subj_orelhas_angulo_maximo;
static char s_buf_orelhas_status[UI_MODEL_STR_MAX_LEN] = CONST_ORELHAS_STATUS_TEXTO_GLOBAL;

/* --- Display --- */
lv_subject_t g_subj_display_ligado;
lv_subject_t g_subj_display_brilho;
lv_subject_t g_subj_display_timeout_segundos;

/* --- Áudio --- */
lv_subject_t g_subj_audio_volume;
lv_subject_t g_subj_audio_muted;
lv_subject_t g_subj_audio_eq_preset;

static bool s_subjects_initialized = false;

/* Helper interno para atualizar strings e formatar textos compostos dos subjects */
static void update_string_subjects_internal(void)
{
    if (!s_subjects_initialized) return;

    /* Bateria: "85% (3.95 V)" */
    float volts = s_model.battery_mv / 1000.0f;
    snprintf(s_buf_bateria_status, sizeof(s_buf_bateria_status), "%u%% (%.2f V)", 
             (unsigned int)s_model.battery_percent, volts);
    lv_subject_copy_string(&g_subj_bateria_status_texto, s_buf_bateria_status);

    /* Bluetooth Slot 1 */
    snprintf(s_buf_bt_nome_1, sizeof(s_buf_bt_nome_1), "%s", s_model.bt_name_slot0);
    lv_subject_copy_string(&g_subj_bt_nome_1, s_buf_bt_nome_1);

    if (s_model.bt_active_slot == 0) {
        snprintf(s_buf_bt_status_1, sizeof(s_buf_bt_status_1), "%.48s (Ativo)", s_model.bt_status_slot0);
    } else {
        snprintf(s_buf_bt_status_1, sizeof(s_buf_bt_status_1), "%.60s", s_model.bt_status_slot0);
    }
    lv_subject_copy_string(&g_subj_bt_status_1, s_buf_bt_status_1);

    /* Bluetooth Slot 2 */
    snprintf(s_buf_bt_nome_2, sizeof(s_buf_bt_nome_2), "%.60s", s_model.bt_name_slot1);
    lv_subject_copy_string(&g_subj_bt_nome_2, s_buf_bt_nome_2);

    if (s_model.bt_active_slot == 1) {
        snprintf(s_buf_bt_status_2, sizeof(s_buf_bt_status_2), "%.48s (Ativo)", s_model.bt_status_slot1);
    } else {
        snprintf(s_buf_bt_status_2, sizeof(s_buf_bt_status_2), "%.60s", s_model.bt_status_slot1);
    }
    lv_subject_copy_string(&g_subj_bt_status_2, s_buf_bt_status_2);

    /* Bluetooth Alternância: Modo 1-Clique Estável */
    snprintf(s_buf_bt_alternancia, sizeof(s_buf_bt_alternancia), "1-Clique (Slot %u ativo)", (unsigned int)(s_model.bt_active_slot + 1));
    lv_subject_copy_string(&g_subj_bt_status_alternancia, s_buf_bt_alternancia);

    /* Proximidade */
    if (!s_model.prox_enabled) {
        snprintf(s_buf_prox_status, sizeof(s_buf_prox_status), "Desativado");
    } else if (s_model.prox_worn) {
        snprintf(s_buf_prox_status, sizeof(s_buf_prox_status), "No ouvido");
    } else {
        snprintf(s_buf_prox_status, sizeof(s_buf_prox_status), "Fora do ouvido");
    }
    lv_subject_copy_string(&g_subj_proximidade_status_texto, s_buf_prox_status);

    /* Vibracall */
    if (!s_model.vibracall_enabled) {
        snprintf(s_buf_vibracall_status, sizeof(s_buf_vibracall_status), "Desativado (%u%%)", (unsigned int)s_model.vibracall_intensity);
    } else if (s_model.vibracall_active) {
        snprintf(s_buf_vibracall_status, sizeof(s_buf_vibracall_status), "Vibrando (%u%%)", (unsigned int)s_model.vibracall_intensity);
    } else {
        snprintf(s_buf_vibracall_status, sizeof(s_buf_vibracall_status), "Ativado (%u%%)", (unsigned int)s_model.vibracall_intensity);
    }
    lv_subject_copy_string(&g_subj_vibracall_status_texto, s_buf_vibracall_status);

    /* Orelhas */
    if (!s_model.orelhas_enabled) {
        snprintf(s_buf_orelhas_status, sizeof(s_buf_orelhas_status), "Desativado");
    } else {
        snprintf(s_buf_orelhas_status, sizeof(s_buf_orelhas_status), "Ativo (%u deg)", (unsigned int)s_model.orelhas_max_angle);
    }
    lv_subject_copy_string(&g_subj_orelhas_status_texto, s_buf_orelhas_status);

    /* Sincronização direta com os Subjects gerados pela UI (components/ui/ui_gen.h) */
    lv_subject_copy_string(&subject_g_subj_bt_nome_1, s_buf_bt_nome_1);
    lv_subject_copy_string(&subject_g_subj_bt_status_1, s_buf_bt_status_1);
    lv_subject_copy_string(&subject_g_subj_bt_nome_2, s_buf_bt_nome_2);
    lv_subject_copy_string(&subject_g_subj_bt_status_2, s_buf_bt_status_2);
    lv_subject_copy_string(&subject_g_subj_bt_status_alternancia, s_buf_bt_alternancia);
    lv_subject_copy_string(&subject_g_subj_bt_active_slot, 
                           (s_model.bt_active_slot == 0) ? s_model.bt_status_slot0 : s_model.bt_status_slot1);
    lv_subject_copy_string(&subject_g_subj_proximidade_status_texto, s_buf_prox_status);
    lv_subject_copy_string(&subject_g_subj_vibracall_status_texto, s_buf_vibracall_status);
    lv_subject_copy_string(&subject_g_subj_orelhas_status_texto, s_buf_orelhas_status);
    lv_subject_set_int(&subject_subj_battery_percent, s_model.battery_percent);
    lv_subject_set_int(&subject_g_subj_bateria_tensao_mv, s_model.battery_mv);

    /* Sincroniza o status textual do temporizador de display ("Temporizador ligado" ou "Temporizador desligado") com o subject da tela */
    lv_subject_copy_string(&subject_g_subj_display_ligado, s_model.display_timer_enabled ? "Temporizador ligado" : "Temporizador desligado");

    /* Sincroniza o modo de alternância Bluetooth ("Automática" ou "Manual") para telemetria/status */
    lv_subject_copy_string(&subject_g_subj_bt_alternancia_ativa, s_model.bt_auto_switch ? "Automática" : "Manual");
}

void ui_model_sync_ui_gen(void)
{
    if (s_model_mutex) xSemaphoreTake(s_model_mutex, portMAX_DELAY);
    lv_subject_set_int(&subject_g_subj_display_brilho, s_model.display_brightness);
    lv_subject_set_int(&subject_g_subj_display_timeout_segundos, s_model.display_timeout_sec);
    lv_subject_set_int(&subject_g_subj_proximidade_sensibilidade, s_model.prox_sensitivity);
    lv_subject_set_int(&subject_g_subj_vibracall_intensidade, s_model.vibracall_intensity);
    lv_subject_set_int(&subject_g_subj_orelhas_angulo_maximo, s_model.orelhas_max_angle);
    update_string_subjects_internal();
    if (s_model_mutex) xSemaphoreGive(s_model_mutex);
}

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
    if (settings_get_u8(SETTINGS_KEY_DISP_TIMER_EN, &u8_val, 1) == ESP_OK) {
        s_model.display_timer_enabled = (u8_val != 0);
    } else {
        s_model.display_timer_enabled = true;
    }
    s_model.display_on = true;
    if (settings_get_u8(SETTINGS_KEY_HAPTIC_ENABLE, &u8_val, 1) == ESP_OK) {
        s_model.vibracall_enabled = (u8_val != 0);
    }
    if (settings_get_u8(SETTINGS_KEY_HAPTIC_INTENS, &u8_val, 80) == ESP_OK) {
        s_model.vibracall_intensity = u8_val;
    }
    if (settings_get_u8(SETTINGS_KEY_EARS_ANGLE, &u8_val, 120) == ESP_OK) {
        s_model.orelhas_max_angle = u8_val;
    }
    if (settings_get_u8(SETTINGS_KEY_BT_AUTOSWITCH, &u8_val, CONST_BLUETOOTH_ALTERNANCIA_ATIVA_GLOBAL) == ESP_OK) {
        s_model.bt_auto_switch = (u8_val != 0);
    }
    if (settings_get_u8(SETTINGS_KEY_BT_SEL_SLOT, &u8_val, 0) == ESP_OK) {
        s_model.bt_active_slot = u8_val;
    }
    if (settings_get_u8(SETTINGS_KEY_SENS_PROX_EN, &u8_val, CONST_PROXIMIDADE_ATIVO_GLOBAL) == ESP_OK) {
        s_model.prox_enabled = (u8_val != 0);
    }
    if (settings_get_u16(SETTINGS_KEY_SENS_THRESH_ON, &u16_val, CONST_PROXIMIDADE_SENSIBILIDADE_GLOBAL) == ESP_OK) {
        s_model.prox_sensitivity = u16_val;
    }

    /* Carrega nomes amigáveis salvos dos dispositivos Bluetooth */
    char saved_name[UI_MODEL_STR_MAX_LEN] = {0};
    if (settings_get_blob(SETTINGS_KEY_PEER_NAME_SLOT0, saved_name, sizeof(saved_name) - 1) == ESP_OK && saved_name[0] != '\0') {
        strncpy(s_model.bt_name_slot0, saved_name, sizeof(s_model.bt_name_slot0) - 1);
        s_model.bt_name_slot0[sizeof(s_model.bt_name_slot0) - 1] = '\0';
    }
    memset(saved_name, 0, sizeof(saved_name));
    if (settings_get_blob(SETTINGS_KEY_PEER_NAME_SLOT1, saved_name, sizeof(saved_name) - 1) == ESP_OK && saved_name[0] != '\0') {
        strncpy(s_model.bt_name_slot1, saved_name, sizeof(s_model.bt_name_slot1) - 1);
        s_model.bt_name_slot1[sizeof(s_model.bt_name_slot1) - 1] = '\0';
    }

    /* Inicializa subjects do LVGL 9 (deve rodar após lv_init()) */
    if (!s_subjects_initialized) {
        /* Bateria */
        lv_subject_init_string(&g_subj_bateria_status_texto, s_buf_bateria_status, NULL, sizeof(s_buf_bateria_status), s_buf_bateria_status);
        lv_subject_init_int(&g_subj_bateria_porcentagem, s_model.battery_percent);
        lv_subject_init_int(&g_subj_bateria_tensao_mv, s_model.battery_mv);

        /* Bluetooth */
        lv_subject_init_string(&g_subj_bt_nome_1, s_buf_bt_nome_1, NULL, sizeof(s_buf_bt_nome_1), s_buf_bt_nome_1);
        lv_subject_init_string(&g_subj_bt_status_1, s_buf_bt_status_1, NULL, sizeof(s_buf_bt_status_1), s_buf_bt_status_1);
        lv_subject_init_string(&g_subj_bt_nome_2, s_buf_bt_nome_2, NULL, sizeof(s_buf_bt_nome_2), s_buf_bt_nome_2);
        lv_subject_init_string(&g_subj_bt_status_2, s_buf_bt_status_2, NULL, sizeof(s_buf_bt_status_2), s_buf_bt_status_2);
        lv_subject_init_int(&g_subj_bt_active_slot, s_model.bt_active_slot);
        lv_subject_init_string(&g_subj_bt_status_alternancia, s_buf_bt_alternancia, NULL, sizeof(s_buf_bt_alternancia), s_buf_bt_alternancia);
        lv_subject_init_int(&g_subj_bt_alternancia_ativa, s_model.bt_auto_switch ? 1 : 0);

        /* Proximidade */
        lv_subject_init_string(&g_subj_proximidade_status_texto, s_buf_prox_status, NULL, sizeof(s_buf_prox_status), s_buf_prox_status);
        lv_subject_init_int(&g_subj_proximidade_ativo, s_model.prox_enabled ? 1 : 0);
        lv_subject_init_int(&g_subj_proximidade_sensibilidade, s_model.prox_sensitivity);
        lv_subject_init_int(&g_subj_prox_worn, s_model.prox_worn ? 1 : 0);

        /* Vibracall */
        lv_subject_init_string(&g_subj_vibracall_status_texto, s_buf_vibracall_status, NULL, sizeof(s_buf_vibracall_status), s_buf_vibracall_status);
        lv_subject_init_int(&g_subj_vibracall_ativo, s_model.vibracall_enabled ? 1 : 0);
        lv_subject_init_int(&g_subj_vibracall_intensidade, s_model.vibracall_intensity);

        /* Orelhas */
        lv_subject_init_string(&g_subj_orelhas_status_texto, s_buf_orelhas_status, NULL, sizeof(s_buf_orelhas_status), s_buf_orelhas_status);
        lv_subject_init_int(&g_subj_orelhas_ativo, s_model.orelhas_enabled ? 1 : 0);
        lv_subject_init_int(&g_subj_orelhas_angulo_maximo, s_model.orelhas_max_angle);

        /* Display */
        lv_subject_init_int(&g_subj_display_ligado, s_model.display_on ? 1 : 0);
        lv_subject_init_int(&g_subj_display_brilho, s_model.display_brightness);
        lv_subject_init_int(&g_subj_display_timeout_segundos, s_model.display_timeout_sec);

        /* Áudio */
        lv_subject_init_int(&g_subj_audio_volume, s_model.audio_volume);
        lv_subject_init_int(&g_subj_audio_muted, s_model.audio_muted ? 1 : 0);
        lv_subject_init_int(&g_subj_audio_eq_preset, s_model.audio_eq_preset);

        s_subjects_initialized = true;
        update_string_subjects_internal();
    }

    ESP_LOGI(TAG, "UI Model inicializado com sucesso (Todos os Subjects prontos)");
    return ESP_OK;
}

/* ============================================================================
 * TRATAMENTO DE ATUALIZAÇÕES E DRENAGEM (TASK LVGL)
 * ============================================================================ */

void ui_model_drain_updates(void)
{
    if (!s_ui_update_queue) return;

    ui_model_update_msg_t msg;
    bool needs_string_update = false;

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
                needs_string_update = true;
                break;

            case UI_UPDATE_BT_SLOT_STATUS:
                if (msg.payload.bt_status.slot == 0) {
                    strncpy(s_model.bt_status_slot0, msg.payload.bt_status.status, sizeof(s_model.bt_status_slot0) - 1);
                    s_model.bt_status_slot0[sizeof(s_model.bt_status_slot0) - 1] = '\0';
                } else if (msg.payload.bt_status.slot == 1) {
                    strncpy(s_model.bt_status_slot1, msg.payload.bt_status.status, sizeof(s_model.bt_status_slot1) - 1);
                    s_model.bt_status_slot1[sizeof(s_model.bt_status_slot1) - 1] = '\0';
                }
                needs_string_update = true;
                break;

            case UI_UPDATE_BT_ACTIVE_SLOT:
                s_model.bt_active_slot = msg.payload.bt_slot.slot;
                if (s_subjects_initialized) {
                    lv_subject_set_int(&g_subj_bt_active_slot, s_model.bt_active_slot);
                }
                needs_string_update = true;
                break;

            case UI_UPDATE_BT_AUTO_SWITCH:
                s_model.bt_auto_switch = msg.payload.bt_auto_sw.auto_switch;
                if (s_subjects_initialized) {
                    lv_subject_set_int(&g_subj_bt_alternancia_ativa, s_model.bt_auto_switch ? 1 : 0);
                }
                needs_string_update = true;
                break;

            case UI_UPDATE_BT_PAIRING:
                s_model.bt_pairing_active = msg.payload.bt_pair.pairing;
                break;

            case UI_UPDATE_BATTERY:
                s_model.battery_percent = msg.payload.battery.percent;
                s_model.battery_mv = msg.payload.battery.mv;
                s_model.battery_charging = msg.payload.battery.charging;
                if (s_subjects_initialized) {
                    /* Atualiza o subject interno do modelo */
                    lv_subject_set_int(&g_subj_bateria_porcentagem, s_model.battery_percent);
                    lv_subject_set_int(&g_subj_bateria_tensao_mv, s_model.battery_mv);
                    /* Atualiza o subject oficial gerado pelo Figma (components/ui/ui_gen.h) */
                    lv_subject_set_int(&subject_subj_battery_percent, s_model.battery_percent);
                }
                needs_string_update = true;
                break;

            case UI_UPDATE_PROX_STATE:
                s_model.prox_worn = msg.payload.prox_state.worn;
                if (s_subjects_initialized) {
                    lv_subject_set_int(&g_subj_prox_worn, s_model.prox_worn ? 1 : 0);
                }
                needs_string_update = true;
                break;

            case UI_UPDATE_PROX_CONFIG:
                s_model.prox_enabled = msg.payload.prox_cfg.enabled;
                s_model.prox_sensitivity = msg.payload.prox_cfg.sensitivity;
                if (s_subjects_initialized) {
                    lv_subject_set_int(&g_subj_proximidade_ativo, s_model.prox_enabled ? 1 : 0);
                    lv_subject_set_int(&g_subj_proximidade_sensibilidade, s_model.prox_sensitivity);
                    lv_subject_set_int(&subject_g_subj_proximidade_sensibilidade, s_model.prox_sensitivity);
                }
                needs_string_update = true;
                break;

            case UI_UPDATE_VIBRACALL_STATE:
                s_model.vibracall_active = msg.payload.vib_state.active;
                needs_string_update = true;
                break;

            case UI_UPDATE_VIBRACALL_CONFIG:
                s_model.vibracall_enabled = msg.payload.vib_cfg.enabled;
                s_model.vibracall_intensity = msg.payload.vib_cfg.intensity;
                if (s_subjects_initialized) {
                    lv_subject_set_int(&g_subj_vibracall_ativo, s_model.vibracall_enabled ? 1 : 0);
                    lv_subject_set_int(&g_subj_vibracall_intensidade, s_model.vibracall_intensity);
                    lv_subject_set_int(&subject_g_subj_vibracall_intensidade, s_model.vibracall_intensity);
                }
                needs_string_update = true;
                break;

            case UI_UPDATE_ORELHAS_CONFIG:
                s_model.orelhas_enabled = msg.payload.orelhas_cfg.enabled;
                s_model.orelhas_max_angle = msg.payload.orelhas_cfg.max_angle;
                if (s_subjects_initialized) {
                    lv_subject_set_int(&g_subj_orelhas_ativo, s_model.orelhas_enabled ? 1 : 0);
                    lv_subject_set_int(&g_subj_orelhas_angulo_maximo, s_model.orelhas_max_angle);
                    lv_subject_set_int(&subject_g_subj_orelhas_angulo_maximo, s_model.orelhas_max_angle);
                }
                needs_string_update = true;
                break;

            case UI_UPDATE_DISPLAY_CONFIG: {
                bool state_changed = (s_model.display_on != msg.payload.display_cfg.on);
                bool timer_changed = (s_model.display_timer_enabled != msg.payload.display_cfg.timer_enabled);
                s_model.display_on = msg.payload.display_cfg.on;
                s_model.display_timer_enabled = msg.payload.display_cfg.timer_enabled;
                s_model.display_brightness = msg.payload.display_cfg.brightness;
                s_model.display_timeout_sec = msg.payload.display_cfg.timeout_sec;
                s_model.display_selected_img_idx = msg.payload.display_cfg.img_idx;
                s_model.display_selected_gif_idx = msg.payload.display_cfg.gif_idx;
                if (s_subjects_initialized) {
                    lv_subject_set_int(&g_subj_display_ligado, s_model.display_timer_enabled ? 1 : 0);
                    lv_subject_set_int(&g_subj_display_brilho, s_model.display_brightness);
                    lv_subject_set_int(&g_subj_display_timeout_segundos, s_model.display_timeout_sec);
                    lv_subject_set_int(&subject_g_subj_display_brilho, s_model.display_brightness);
                    lv_subject_set_int(&subject_g_subj_display_timeout_segundos, s_model.display_timeout_sec);
                }

                /* Atualiza imediatamente o wallpaper e o GIF na interface gráfica do display (thread-safe com LVGL) */
                display_update_wallpaper();
                display_update_gif();

                /* Se o temporizador acabou de ser desativado (modo 'Manter ligado'), restaura o display imediatamente */
                if (timer_changed && !s_model.display_timer_enabled) {
                    display_backlight_wake();
                } else if (state_changed) {
                    if (s_model.display_on) {
                        display_backlight_wake();
                    } else {
                        display_backlight_sleep();
                    }
                }
                needs_string_update = true;
                break;
            }

            case UI_UPDATE_AUDIO_STATE:
                s_model.audio_volume = msg.payload.audio_state.volume;
                s_model.audio_muted = msg.payload.audio_state.muted;
                s_model.audio_eq_preset = msg.payload.audio_state.eq_preset;
                if (s_subjects_initialized) {
                    lv_subject_set_int(&g_subj_audio_volume, s_model.audio_volume);
                    lv_subject_set_int(&g_subj_audio_muted, s_model.audio_muted ? 1 : 0);
                    lv_subject_set_int(&g_subj_audio_eq_preset, s_model.audio_eq_preset);
                }
                break;

            case UI_UPDATE_COVER_ART:
                s_model.has_cover_art = (msg.payload.cover_art.dsc != NULL);
                s_model.cover_art_dsc = msg.payload.cover_art.dsc;
                display_update_cover_art(msg.payload.cover_art.dsc);
                break;

            case UI_UPDATE_TRACK_METADATA:
                if (strlen(msg.payload.track_meta.title) > 0) {
                    strncpy(s_model.track_title, msg.payload.track_meta.title, sizeof(s_model.track_title) - 1);
                }
                if (strlen(msg.payload.track_meta.artist) > 0) {
                    strncpy(s_model.track_artist, msg.payload.track_meta.artist, sizeof(s_model.track_artist) - 1);
                }
                if (strlen(msg.payload.track_meta.album) > 0) {
                    strncpy(s_model.track_album, msg.payload.track_meta.album, sizeof(s_model.track_album) - 1);
                }
                display_update_track_metadata(s_model.track_title, s_model.track_artist, s_model.track_album);
                break;

            default:
                break;
        }

        if (s_model_mutex) xSemaphoreGive(s_model_mutex);
    }

    if (needs_string_update) {
        update_string_subjects_internal();
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

    /* Tenta o envio não-bloqueante convencional */
    if (xQueueSend(s_ui_update_queue, msg, 0) == pdTRUE) {
        return ESP_OK;
    }

    /*
     * Se a fila estiver cheia:
     * Para mensagens de configuração de sliders contínuos (brilho, vibracall, orelhas, etc.),
     * descartar a mensagem intermediária mais antiga ou coalescer na fila evita perda do estado final.
     * Tentamos descartar uma mensagem pendente da fila para dar lugar à mensagem mais recente.
     */
    ui_model_update_msg_t dummy;
    if (xQueueReceive(s_ui_update_queue, &dummy, 0) == pdTRUE) {
        if (xQueueSend(s_ui_update_queue, msg, 0) == pdTRUE) {
            return ESP_OK;
        }
    }

    ESP_LOGW(TAG, "Fila de atualizacao da UI cheia, mensagem tipo %d descartada", msg->type);
    return ESP_ERR_NO_MEM;
}

/* ============================================================================
 * HANDLERS DE EVENTOS (ESP_EVENT PUB/SUB)
 * ============================================================================ */

static void on_sensor_event(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    (void)handler_args; (void)base;
    if (id == SENSOR_EVT_WORN) {
        /* Se proximidade estiver desativada pelo usuário, não sobrescreve com evento físico */
        if (!s_model.prox_enabled) return;

        ui_model_update_msg_t msg = {
            .type = UI_UPDATE_PROX_STATE,
            .payload.prox_state = { .worn = true }
        };
        ui_model_post_update(&msg);
    } else if (id == SENSOR_EVT_REMOVED) {
        if (!s_model.prox_enabled) return;

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
    } else if (id == BT_EVT_STREAMING && event_data) {
        bt_streaming_evt_t *ev = (bt_streaming_evt_t *)event_data;
        /* Inibe o touch XPT2046 durante o streaming para eliminar toques fantasmas causados por ruído de RF */
        display_touch_set_audio_streaming(ev->streaming);
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

static void on_cfg_event(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    (void)handler_args; (void)base;
    if (id == CFG_EVT_SETTING_CHANGED && event_data) {
        cfg_changed_evt_t *ev = (cfg_changed_evt_t *)event_data;
        if (strcmp(ev->key, SETTINGS_KEY_DISP_BRIGHT) == 0) {
            /* Só posta na fila se houver diferença real, quebrando qualquer ciclo com sliders */
            if (s_model.display_brightness != ev->value_u8) {
                ui_model_update_msg_t msg = {
                    .type = UI_UPDATE_DISPLAY_CONFIG,
                    .payload.display_cfg = {
                        .on = (ev->value_u8 > 0),
                        .timer_enabled = s_model.display_timer_enabled,
                        .brightness = ev->value_u8,
                        .timeout_sec = s_model.display_timeout_sec,
                        .img_idx = s_model.display_selected_img_idx,
                        .gif_idx = s_model.display_selected_gif_idx,
                    }
                };
                ui_model_post_update(&msg);
            }
        } else if (strcmp(ev->key, SETTINGS_KEY_DISP_TIMER_EN) == 0) {
            bool new_timer = (ev->value_u8 != 0);
            if (s_model.display_timer_enabled != new_timer) {
                ui_model_update_msg_t msg = {
                    .type = UI_UPDATE_DISPLAY_CONFIG,
                    .payload.display_cfg = {
                        .on = s_model.display_on,
                        .timer_enabled = new_timer,
                        .brightness = s_model.display_brightness,
                        .timeout_sec = s_model.display_timeout_sec,
                        .img_idx = s_model.display_selected_img_idx,
                        .gif_idx = s_model.display_selected_gif_idx,
                    }
                };
                ui_model_post_update(&msg);
            }
        } else if (strcmp(ev->key, SETTINGS_KEY_BT_AUTOSWITCH) == 0) {
            bool new_sw = (ev->value_u8 != 0);
            if (s_model.bt_auto_switch != new_sw) {
                ui_model_update_msg_t msg = {
                    .type = UI_UPDATE_BT_AUTO_SWITCH,
                    .payload.bt_auto_sw = { .auto_switch = new_sw }
                };
                ui_model_post_update(&msg);
            }
        } else if (strcmp(ev->key, SETTINGS_KEY_HAPTIC_ENABLE) == 0) {
            bool new_en = (ev->value_u8 != 0);
            if (s_model.vibracall_enabled != new_en) {
                ui_model_update_msg_t msg = {
                    .type = UI_UPDATE_VIBRACALL_CONFIG,
                    .payload.vib_cfg = {
                        .enabled = new_en,
                        .intensity = s_model.vibracall_intensity,
                    }
                };
                ui_model_post_update(&msg);
            }
        } else if (strcmp(ev->key, SETTINGS_KEY_HAPTIC_INTENS) == 0) {
            if (s_model.vibracall_intensity != ev->value_u8) {
                ui_model_update_msg_t msg = {
                    .type = UI_UPDATE_VIBRACALL_CONFIG,
                    .payload.vib_cfg = {
                        .enabled = s_model.vibracall_enabled,
                        .intensity = ev->value_u8,
                    }
                };
                ui_model_post_update(&msg);
            }
        } else if (strcmp(ev->key, SETTINGS_KEY_EARS_ANGLE) == 0) {
            if (s_model.orelhas_max_angle != ev->value_u8) {
                ui_model_update_msg_t msg = {
                    .type = UI_UPDATE_ORELHAS_CONFIG,
                    .payload.orelhas_cfg = {
                        .enabled = s_model.orelhas_enabled,
                        .max_angle = ev->value_u8,
                    }
                };
                ui_model_post_update(&msg);
            }
        } else if (strcmp(ev->key, SETTINGS_KEY_SENS_PROX_EN) == 0) {
            bool new_en = (ev->value_u8 != 0);
            if (s_model.prox_enabled != new_en) {
                ui_model_update_msg_t msg = {
                    .type = UI_UPDATE_PROX_CONFIG,
                    .payload.prox_cfg = {
                        .enabled = new_en,
                        .sensitivity = s_model.prox_sensitivity,
                    }
                };
                ui_model_post_update(&msg);
            }
        }
    }
}

esp_err_t ui_model_register_events(void)
{
    hs_event_register(SENSOR_EVT, ESP_EVENT_ANY_ID, on_sensor_event, NULL);
    hs_event_register(BT_EVT, ESP_EVENT_ANY_ID, on_bt_event, NULL);
    hs_event_register(AUDIO_EVT, ESP_EVENT_ANY_ID, on_audio_event, NULL);
    hs_event_register(CFG_EVT, ESP_EVENT_ANY_ID, on_cfg_event, NULL);

    ESP_LOGI(TAG, "UI Model inscrito nos eventos do sistema (SENSOR_EVT, BT_EVT, AUDIO_EVT, CFG_EVT)");
    return ESP_OK;
}

esp_err_t ui_model_post_cover_art(const lv_image_dsc_t *dsc)
{
    ui_model_update_msg_t msg = {
        .type = UI_UPDATE_COVER_ART,
        .payload.cover_art.dsc = dsc,
    };
    return ui_model_post_update(&msg);
}

esp_err_t ui_model_post_track_metadata(const char *title, const char *artist, const char *album)
{
    ui_model_update_msg_t msg = {
        .type = UI_UPDATE_TRACK_METADATA,
    };
    if (title) {
        strncpy(msg.payload.track_meta.title, title, sizeof(msg.payload.track_meta.title) - 1);
        msg.payload.track_meta.title[sizeof(msg.payload.track_meta.title) - 1] = '\0';
    }
    if (artist) {
        strncpy(msg.payload.track_meta.artist, artist, sizeof(msg.payload.track_meta.artist) - 1);
        msg.payload.track_meta.artist[sizeof(msg.payload.track_meta.artist) - 1] = '\0';
    }
    if (album) {
        strncpy(msg.payload.track_meta.album, album, sizeof(msg.payload.track_meta.album) - 1);
        msg.payload.track_meta.album[sizeof(msg.payload.track_meta.album) - 1] = '\0';
    }
    return ui_model_post_update(&msg);
}

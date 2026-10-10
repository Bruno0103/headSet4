/**
 * @file ui_model.h
 * @brief Modelo de dados e estado reativo da interface grafica (LVGL 9.5).
 *
 * Arquitetura Event-Driven & Actor Model (AGENTS.md §4.2, WP 6.1):
 * - Centraliza o estado reativo da UI via lv_subject_t (LVGL 9 Observer Pattern).
 * - Thread Safety Estrita:
 *   Como LV_USE_OS=NONE, SOMENTE a thread/task do LVGL pode acessar ou alterar
 *   diretamente os subjects e objetos do LVGL.
 * - Outras threads/atores (via esp_event ou comandos) postam atualizações
 *   para uma fila interna de atualizações (ui_model_update_t), que é drenada
 *   com segurança no início de cada iteração da task do LVGL (ui_model_drain_updates()).
 * - Os getters consumidos pela UI Bridge leem do modelo mantido em memória
 *   sem bloquear nem acessar drivers de hardware.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UI_MODEL_STR_MAX_LEN 48

/* ============================================================================
 * ESTRUTURA DO MODELO REATIVO DE DADOS
 * ============================================================================ */

typedef struct {
    /* --- Bluetooth --- */
    char bt_name_slot0[UI_MODEL_STR_MAX_LEN];
    char bt_name_slot1[UI_MODEL_STR_MAX_LEN];
    char bt_status_slot0[UI_MODEL_STR_MAX_LEN];
    char bt_status_slot1[UI_MODEL_STR_MAX_LEN];
    uint8_t bt_active_slot;
    bool bt_auto_switch;
    bool bt_pairing_active;

    /* --- Bateria --- */
    uint8_t battery_percent;
    uint16_t battery_mv;
    bool battery_charging;

    /* --- Proximidade (APDS-9930) --- */
    bool prox_enabled;
    bool prox_worn;
    uint16_t prox_sensitivity;

    /* --- Atuadores: Vibracall e Orelhas --- */
    bool vibracall_enabled;
    uint8_t vibracall_intensity;
    bool vibracall_active;

    bool orelhas_enabled;
    uint8_t orelhas_max_angle;

    /* --- Display & Backlight --- */
    bool display_on;
    uint8_t display_brightness;
    uint16_t display_timeout_sec;
    int display_selected_img_idx;
    int display_selected_gif_idx;

    /* --- Áudio --- */
    uint8_t audio_volume;
    bool audio_muted;
    uint8_t audio_eq_preset;
} ui_model_data_t;

/* ============================================================================
 * DECLARAÇÃO DOS SUBJECTS PÚBLICOS DO LVGL 9 (Para binding e observers)
 * ============================================================================ */

extern lv_subject_t g_subj_battery_percent;
extern lv_subject_t g_subj_battery_mv;
extern lv_subject_t g_subj_bt_active_slot;
extern lv_subject_t g_subj_display_brightness;
extern lv_subject_t g_subj_prox_worn;

/* ============================================================================
 * INTERFACE PÚBLICA DO UI MODEL
 * ============================================================================ */

/**
 * @brief Inicializa os subjects do LVGL e o subsistema de mensagens da UI.
 * Deve ser chamado a partir da thread do LVGL logo após lv_init().
 *
 * @return esp_err_t ESP_OK em caso de sucesso.
 */
esp_err_t ui_model_init(void);

/**
 * @brief Registra a inscrição aos eventos centrais do Headset (esp_event)
 * para alimentar a fila de atualização do modelo.
 *
 * @return esp_err_t ESP_OK em caso de sucesso.
 */
esp_err_t ui_model_register_events(void);

/**
 * @brief Processa e drena todas as atualizações enfileiradas atualizando os subjects.
 * DEVE ser executada exclusivamente dentro do loop da task do LVGL (display_task),
 * antes de invocar lv_timer_handler().
 */
void ui_model_drain_updates(void);

/**
 * @brief Obtém cópia do estado snapshot do modelo (Thread-safe para leitura de getters).
 *
 * @param[out] out_data Ponteiro para buffer que receberá a cópia.
 */
void ui_model_get_snapshot(ui_model_data_t *out_data);

/* ============================================================================
 * POSTAGEM SEGURA DE ATUALIZAÇÕES PARA O CONTEXTO DA UI
 * ============================================================================ */

typedef enum {
    UI_UPDATE_BT_SLOT_NAME = 0,
    UI_UPDATE_BT_SLOT_STATUS,
    UI_UPDATE_BT_ACTIVE_SLOT,
    UI_UPDATE_BT_AUTO_SWITCH,
    UI_UPDATE_BT_PAIRING,
    UI_UPDATE_BATTERY,
    UI_UPDATE_PROX_STATE,
    UI_UPDATE_PROX_CONFIG,
    UI_UPDATE_VIBRACALL_STATE,
    UI_UPDATE_VIBRACALL_CONFIG,
    UI_UPDATE_ORELHAS_CONFIG,
    UI_UPDATE_DISPLAY_CONFIG,
    UI_UPDATE_AUDIO_STATE,
} ui_model_update_type_t;

typedef struct {
    ui_model_update_type_t type;
    union {
        struct {
            uint8_t slot;
            char name[UI_MODEL_STR_MAX_LEN];
        } bt_name;
        struct {
            uint8_t slot;
            char status[UI_MODEL_STR_MAX_LEN];
        } bt_status;
        struct {
            uint8_t slot;
        } bt_slot;
        struct {
            bool auto_switch;
        } bt_auto_sw;
        struct {
            bool pairing;
        } bt_pair;
        struct {
            uint8_t percent;
            uint16_t mv;
            bool charging;
        } battery;
        struct {
            bool worn;
        } prox_state;
        struct {
            bool enabled;
            uint16_t sensitivity;
        } prox_cfg;
        struct {
            bool active;
        } vib_state;
        struct {
            bool enabled;
            uint8_t intensity;
        } vib_cfg;
        struct {
            bool enabled;
            uint8_t max_angle;
        } orelhas_cfg;
        struct {
            bool on;
            uint8_t brightness;
            uint16_t timeout_sec;
            int img_idx;
            int gif_idx;
        } display_cfg;
        struct {
            uint8_t volume;
            bool muted;
            uint8_t eq_preset;
        } audio_state;
    } payload;
} ui_model_update_msg_t;

/**
 * @brief Envia uma atualização para a fila do modelo de UI. Pode ser chamada de qualquer thread.
 *
 * @param msg Mensagem contendo os dados a atualizar.
 * @return esp_err_t ESP_OK se postada na fila com sucesso.
 */
esp_err_t ui_model_post_update(const ui_model_update_msg_t *msg);

#ifdef __cplusplus
}
#endif

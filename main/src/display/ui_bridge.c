/**
 * @file ui_bridge.c
 * @brief Fachada Fina (Thin Facade) entre o LVGL e os recursos do HeadSet.
 *
 * Em conformidade com AGENTS.md (§4.2, WP 6.2):
 * - ZERO ACESSO DIRETO A HARDWARE OU GPIOS (sem driver/gpio.h, sem gpio_set_level).
 * - ZERO ACOPLAMENTO COM DRIVERS ESPECÍFICOS (sem apds9930.h, sem bt_gap.h).
 * - GETTERS leem exclusivamente do modelo em memória (ui_model) de forma não-bloqueante.
 * - SETTERS despacham comandos padronizados usando hs_actor_send() para os respectivos
 *   atores donos (bt_link, audio, settings, actuators) ou postam atualizações locais.
 * - Todos os nomes mockados ("Smartphone A", "Notebook B") foram eliminados em favor
 *   do estado dinâmico gerenciado pelo sistema e recebido por eventos.
 */

#include "ui_bridge.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "audio.h"
#include "actuators.h"
#include "apds9930.h"
#include "bt_avrcp.h"
#include "bt_fastpair.h"
#include "bt_link_mgr.h"
#include "hs_actor.h"
#include "hs_cmds.h"
#include "settings.h"
#include "ui_model.h"

static const char *TAG = "ui_bridge";

/* Itens da galeria de imagens disponíveis na UI (assets do display) */
static const ui_bridge_gallery_item_t s_gallery_items[] = {
    {.name = "Headphones", .img_src = NULL},
    {.name = "Bluetooth Icon", .img_src = NULL},
    {.name = "CPU Chip", .img_src = NULL},
    {.name = "Bateria Normal", .img_src = NULL},
    {.name = "Bateria Carga", .img_src = NULL},
};
#define GALLERY_ITEMS_COUNT (sizeof(s_gallery_items) / sizeof(s_gallery_items[0]))

/* Itens da galeria de GIFs / Animações disponíveis na UI */
static const ui_bridge_gallery_gif_item_t s_gallery_gif_items[] = {
    {.name = "Animacao Padrao", .gif_src = NULL},
    {.name = "Ondas Sonoras", .gif_src = NULL},
    {.name = "Equalizador Grafico", .gif_src = NULL},
    {.name = "Pulso de Energia", .gif_src = NULL},
};
#define GALLERY_GIF_ITEMS_COUNT (sizeof(s_gallery_gif_items) / sizeof(s_gallery_gif_items[0]))

/* ============================================================================
 * INICIALIZAÇÃO DA CAMADA BRIDGE
 * ============================================================================
 */

esp_err_t ui_bridge_init(void)
{
    ESP_LOGI(TAG, "Fachada fina UI Bridge inicializada");
    return ESP_OK;
}

/* ============================================================================
 * 1. BLUETOOTH
 * ============================================================================
 */

void ui_bridge_bt_get_nome_1(char *out_name, size_t max_len)
{
    if (!out_name || max_len == 0) return;
    ui_model_data_t snap;
    ui_model_get_snapshot(&snap);
    strncpy(out_name, snap.bt_name_slot0, max_len - 1);
    out_name[max_len - 1] = '\0';
}

void ui_bridge_bt_get_nome_2(char *out_name, size_t max_len)
{
    if (!out_name || max_len == 0) return;
    ui_model_data_t snap;
    ui_model_get_snapshot(&snap);
    strncpy(out_name, snap.bt_name_slot1, max_len - 1);
    out_name[max_len - 1] = '\0';
}

bool ui_bridge_bt_set_desconectar_dispositivo_1(bool disconnect)
{
    if (!disconnect) return false;

    hs_actor_t *bt_act = bt_link_actor_get();
    if (bt_act) {
        bt_cmd_disconnect_t cmd = { .slot = 0 };
        esp_err_t err = hs_actor_send(bt_act, BT_CMD_DISCONNECT, &cmd, sizeof(cmd), pdMS_TO_TICKS(50));
        return (err == ESP_OK);
    }
    return false;
}

bool ui_bridge_bt_set_desconectar_dispositivo_2(bool disconnect)
{
    if (!disconnect) return false;

    hs_actor_t *bt_act = bt_link_actor_get();
    if (bt_act) {
        bt_cmd_disconnect_t cmd = { .slot = 1 };
        esp_err_t err = hs_actor_send(bt_act, BT_CMD_DISCONNECT, &cmd, sizeof(cmd), pdMS_TO_TICKS(50));
        return (err == ESP_OK);
    }
    return false;
}

void ui_bridge_bt_get_status_dispositivo_1(char *out_status, size_t max_len)
{
    if (!out_status || max_len == 0) return;
    ui_model_data_t snap;
    ui_model_get_snapshot(&snap);
    if (snap.bt_active_slot == 0) {
        snprintf(out_status, max_len, "%s (Ativo)", snap.bt_status_slot0);
    } else {
        snprintf(out_status, max_len, "%s", snap.bt_status_slot0);
    }
}

void ui_bridge_bt_get_status_dispositivo_2(char *out_status, size_t max_len)
{
    if (!out_status || max_len == 0) return;
    ui_model_data_t snap;
    ui_model_get_snapshot(&snap);
    if (snap.bt_active_slot == 1) {
        snprintf(out_status, max_len, "%s (Ativo)", snap.bt_status_slot1);
    } else {
        snprintf(out_status, max_len, "%s", snap.bt_status_slot1);
    }
}

void ui_bridge_bt_get_status_alternancia(char *out_status, size_t max_len)
{
    if (!out_status || max_len == 0) return;
    ui_model_data_t snap;
    ui_model_get_snapshot(&snap);
    if (!snap.bt_auto_switch) {
        snprintf(out_status, max_len, "Manual (dispositivo %u)", (unsigned int)(snap.bt_active_slot + 1));
    } else {
        snprintf(out_status, max_len, "Automatico (dispositivo %u)", (unsigned int)(snap.bt_active_slot + 1));
    }
}

bool ui_bridge_bt_set_alternar_ativo(bool active)
{
    hs_actor_t *bt_act = bt_link_actor_get();
    if (bt_act) {
        bt_cmd_set_auto_switch_t cmd = { .enabled = active };
        hs_actor_send(bt_act, BT_CMD_SET_AUTO_SWITCH, &cmd, sizeof(cmd), pdMS_TO_TICKS(50));
    }
    /* Persiste configuração no ator de settings */
    settings_set_u8(SETTINGS_KEY_BT_AUTOSWITCH, active ? 1 : 0);

    ui_model_update_msg_t msg = {
        .type = UI_UPDATE_BT_AUTO_SWITCH,
        .payload.bt_auto_sw = { .auto_switch = active }
    };
    ui_model_post_update(&msg);
    return true;
}

/* ============================================================================
 * 2. PROXIMIDADE (Sensor APDS-9930)
 * ============================================================================
 */

void ui_bridge_proximidade_get_status(char *out_status, size_t max_len)
{
    if (!out_status || max_len == 0) return;
    ui_model_data_t snap;
    ui_model_get_snapshot(&snap);

    if (!snap.prox_enabled) {
        snprintf(out_status, max_len, "Desativado");
    } else if (snap.prox_worn) {
        snprintf(out_status, max_len, "No ouvido");
    } else {
        snprintf(out_status, max_len, "Fora do ouvido");
    }
}

bool ui_bridge_proximidade_set_ativar(bool enable)
{
    settings_set_u8(SETTINGS_KEY_SENS_PROX_EN, enable ? 1 : 0);
    apds9930_set_enabled(enable);

    ui_model_data_t snap;
    ui_model_get_snapshot(&snap);

    ESP_LOGI(TAG, "Configurando sensor de proximidade: %s (sensibilidade mantida: %u)", 
             enable ? "ATIVAR" : "DESATIVAR", (unsigned int)snap.prox_sensitivity);

    ui_model_update_msg_t msg = {
        .type = UI_UPDATE_PROX_CONFIG,
        .payload.prox_cfg = {
            .enabled = enable,
            .sensitivity = snap.prox_sensitivity /* Mantém a sensibilidade atual do snapshot */
        }
    };
    ui_model_post_update(&msg);
    return true;
}

bool ui_bridge_proximidade_set_desativar(bool disable)
{
    return ui_bridge_proximidade_set_ativar(!disable);
}

int ui_bridge_proximidade_get_distancia_sensibilidade(void)
{
    ui_model_data_t snap;
    ui_model_get_snapshot(&snap);
    return snap.prox_sensitivity;
}

bool ui_bridge_proximidade_set_distancia_sensibilidade(int sensibilidade)
{
    if (sensibilidade < 5 || sensibilidade > 1023) return false;

    settings_set_u16(SETTINGS_KEY_SENS_THRESH_ON, (uint16_t)sensibilidade);
    settings_set_u16(SETTINGS_KEY_SENS_THRESH_OFF, (uint16_t)((sensibilidade * 6) / 10));
    apds9930_set_threshold((uint16_t)sensibilidade);

    ui_model_data_t snap;
    ui_model_get_snapshot(&snap);

    ui_model_update_msg_t msg = {
        .type = UI_UPDATE_PROX_CONFIG,
        .payload.prox_cfg = {
            .enabled = snap.prox_enabled,
            .sensitivity = (uint16_t)sensibilidade
        }
    };
    ui_model_post_update(&msg);
    return true;
}

/* ============================================================================
 * 3. VIBRACALL
 * ============================================================================
 */

void ui_bridge_vibracall_get_status(char *out_status, size_t max_len)
{
    if (!out_status || max_len == 0) return;
    ui_model_data_t snap;
    ui_model_get_snapshot(&snap);

    if (!snap.vibracall_enabled) {
        snprintf(out_status, max_len, "Desativado (%u%%)", (unsigned int)snap.vibracall_intensity);
    } else {
        snprintf(out_status, max_len, "Ativado (%u%%)", (unsigned int)snap.vibracall_intensity);
    }
}

bool ui_bridge_vibracall_set_ativar(bool enable)
{
    settings_set_u8(SETTINGS_KEY_HAPTIC_ENABLE, enable ? 1 : 0);

    ui_model_data_t snap;
    ui_model_get_snapshot(&snap);

    ui_model_update_msg_t msg = {
        .type = UI_UPDATE_VIBRACALL_CONFIG,
        .payload.vib_cfg = {
            .enabled = enable,
            .intensity = snap.vibracall_intensity
        }
    };
    ui_model_post_update(&msg);

    /* Se ativado, confirma com vibração breve de feedback */
    if (enable && snap.vibracall_intensity > 0) {
        actuators_vibrate(50, snap.vibracall_intensity);
    }
    return true;
}

bool ui_bridge_vibracall_set_desativar(bool disable)
{
    return ui_bridge_vibracall_set_ativar(!disable);
}

int ui_bridge_vibracall_get_intensidade(void)
{
    ui_model_data_t snap;
    ui_model_get_snapshot(&snap);
    return snap.vibracall_intensity;
}

bool ui_bridge_vibracall_set_intensidade(int intensidade)
{
    if (intensidade < 0 || intensidade > 100) return false;

    settings_set_u8(SETTINGS_KEY_HAPTIC_INTENS, (uint8_t)intensidade);

    ui_model_data_t snap;
    ui_model_get_snapshot(&snap);

    ui_model_update_msg_t msg = {
        .type = UI_UPDATE_VIBRACALL_CONFIG,
        .payload.vib_cfg = {
            .enabled = snap.vibracall_enabled,
            .intensity = (uint8_t)intensidade
        }
    };
    ui_model_post_update(&msg);

    /*
     * Resposta háptica imediata ao interagir com o slider (seja pelo display ou pelo app android).
     * O headset devolve uma resposta vibratória de 50 ms com a nova intensidade selecionada.
     */
    if (snap.vibracall_enabled && intensidade > 0) {
        actuators_vibrate(50, (uint8_t)intensidade);
    }

    return true;
}

/* ============================================================================
 * 4. ORELHAS (Servomotores)
 * ============================================================================
 */

void ui_bridge_orelhas_get_status(char *out_status, size_t max_len)
{
    if (!out_status || max_len == 0) return;
    ui_model_data_t snap;
    ui_model_get_snapshot(&snap);

    if (!snap.orelhas_enabled) {
        snprintf(out_status, max_len, "Desativadas");
    } else {
        snprintf(out_status, max_len, "Ativas (Max: %u deg)", (unsigned int)snap.orelhas_max_angle);
    }
}

bool ui_bridge_orelhas_set_ativar(bool enable)
{
    /* Salva o estado de habilitado no modelo e no settings se houver chave correspondente */
    ui_model_data_t snap;
    ui_model_get_snapshot(&snap);

    ESP_LOGI(TAG, "Configurando orelhas: %s (angulo max: %u deg)", 
             enable ? "ATIVAR" : "DESATIVAR", (unsigned int)snap.orelhas_max_angle);

    ui_model_update_msg_t msg = {
        .type = UI_UPDATE_ORELHAS_CONFIG,
        .payload.orelhas_cfg = {
            .enabled = enable,
            .max_angle = snap.orelhas_max_angle
        }
    };
    ui_model_post_update(&msg);
    return true;
}

bool ui_bridge_orelhas_set_desativar(bool disable)
{
    return ui_bridge_orelhas_set_ativar(!disable);
}

int ui_bridge_orelhas_get_angulo_maximo(void)
{
    ui_model_data_t snap;
    ui_model_get_snapshot(&snap);
    return snap.orelhas_max_angle;
}

bool ui_bridge_orelhas_set_angulo_maximo(int angulo)
{
    if (angulo < 0 || angulo > 180) return false;

    settings_set_u8(SETTINGS_KEY_EARS_ANGLE, (uint8_t)angulo);

    ui_model_update_msg_t msg = {
        .type = UI_UPDATE_ORELHAS_CONFIG,
        .payload.orelhas_cfg = {
            .enabled = true,
            .max_angle = (uint8_t)angulo
        }
    };
    ui_model_post_update(&msg);
    return true;
}

/* ============================================================================
 * 5. DISPLAY & INTERFACE
 * ============================================================================
 */

bool ui_bridge_display_get_status(void)
{
    ui_model_data_t snap;
    ui_model_get_snapshot(&snap);
    return snap.display_on;
}

bool ui_bridge_display_set_ativar(bool enable)
{
    ui_model_data_t snap;
    ui_model_get_snapshot(&snap);

    ui_model_update_msg_t msg = {
        .type = UI_UPDATE_DISPLAY_CONFIG,
        .payload.display_cfg = {
            .on = enable,
            .brightness = snap.display_brightness,
            .timeout_sec = snap.display_timeout_sec,
            .img_idx = snap.display_selected_img_idx,
            .gif_idx = snap.display_selected_gif_idx,
        }
    };
    ui_model_post_update(&msg);
    return true;
}

bool ui_bridge_display_set_desativar(bool disable)
{
    return ui_bridge_display_set_ativar(!disable);
}

int ui_bridge_display_get_brilho(void)
{
    ui_model_data_t snap;
    ui_model_get_snapshot(&snap);
    return snap.display_brightness;
}

bool ui_bridge_display_set_brilho(int brilho)
{
    if (brilho < 0 || brilho > 100) return false;

    settings_set_u8(SETTINGS_KEY_DISP_BRIGHT, (uint8_t)brilho);

    ui_model_data_t snap;
    ui_model_get_snapshot(&snap);

    ui_model_update_msg_t msg = {
        .type = UI_UPDATE_DISPLAY_CONFIG,
        .payload.display_cfg = {
            .on = (brilho > 0),
            .brightness = (uint8_t)brilho,
            .timeout_sec = snap.display_timeout_sec,
            .img_idx = snap.display_selected_img_idx,
            .gif_idx = snap.display_selected_gif_idx,
        }
    };
    ui_model_post_update(&msg);
    return true;
}

int ui_bridge_display_get_tempo_tela(void)
{
    ui_model_data_t snap;
    ui_model_get_snapshot(&snap);
    return snap.display_timeout_sec;
}

bool ui_bridge_display_set_tempo_tela(int segundos)
{
    if (segundos < 0 || segundos > 3600) return false;

    settings_set_u16(SETTINGS_KEY_DISP_TIMEOUT, (uint16_t)segundos);

    ui_model_data_t snap;
    ui_model_get_snapshot(&snap);

    ui_model_update_msg_t msg = {
        .type = UI_UPDATE_DISPLAY_CONFIG,
        .payload.display_cfg = {
            .on = snap.display_on,
            .brightness = snap.display_brightness,
            .timeout_sec = (uint16_t)segundos,
            .img_idx = snap.display_selected_img_idx,
            .gif_idx = snap.display_selected_gif_idx,
        }
    };
    ui_model_post_update(&msg);
    return true;
}

int ui_bridge_display_get_galeria_imagens(ui_bridge_gallery_item_t *out_items, int max_items)
{
    if (!out_items || max_items <= 0) return 0;
    int count = (int)GALLERY_ITEMS_COUNT;
    if (count > max_items) count = max_items;

    for (int i = 0; i < count; i++) {
        out_items[i] = s_gallery_items[i];
    }
    return count;
}

const void *ui_bridge_display_get_imagem_principal(void)
{
    ui_model_data_t snap;
    ui_model_get_snapshot(&snap);
    int idx = snap.display_selected_img_idx;
    if (idx < 0 || idx >= (int)GALLERY_ITEMS_COUNT) idx = 0;
    return s_gallery_items[idx].img_src;
}

bool ui_bridge_display_set_imagem_principal(int index)
{
    if (index < 0 || index >= (int)GALLERY_ITEMS_COUNT) return false;

    ui_model_data_t snap;
    ui_model_get_snapshot(&snap);

    ui_model_update_msg_t msg = {
        .type = UI_UPDATE_DISPLAY_CONFIG,
        .payload.display_cfg = {
            .on = snap.display_on,
            .brightness = snap.display_brightness,
            .timeout_sec = snap.display_timeout_sec,
            .img_idx = index,
            .gif_idx = snap.display_selected_gif_idx,
        }
    };
    ui_model_post_update(&msg);
    return true;
}

int ui_bridge_display_get_galeria_gifs(ui_bridge_gallery_gif_item_t *out_items, int max_items)
{
    if (!out_items || max_items <= 0) return 0;
    int count = (int)GALLERY_GIF_ITEMS_COUNT;
    if (count > max_items) count = max_items;

    for (int i = 0; i < count; i++) {
        out_items[i] = s_gallery_gif_items[i];
    }
    return count;
}

const void *ui_bridge_display_get_gif_principal(void)
{
    ui_model_data_t snap;
    ui_model_get_snapshot(&snap);
    int idx = snap.display_selected_gif_idx;
    if (idx < 0 || idx >= (int)GALLERY_GIF_ITEMS_COUNT) idx = 0;
    return s_gallery_gif_items[idx].gif_src;
}

bool ui_bridge_display_set_gif_principal(int index)
{
    if (index < 0 || index >= (int)GALLERY_GIF_ITEMS_COUNT) return false;

    ui_model_data_t snap;
    ui_model_get_snapshot(&snap);

    ui_model_update_msg_t msg = {
        .type = UI_UPDATE_DISPLAY_CONFIG,
        .payload.display_cfg = {
            .on = snap.display_on,
            .brightness = snap.display_brightness,
            .timeout_sec = snap.display_timeout_sec,
            .img_idx = snap.display_selected_img_idx,
            .gif_idx = index,
        }
    };
    ui_model_post_update(&msg);
    return true;
}

/* ============================================================================
 * 6. BATERIA
 * ============================================================================
 */

void ui_bridge_bateria_get_status(char *out_status, size_t max_len)
{
    if (!out_status || max_len == 0) return;
    ui_model_data_t snap;
    ui_model_get_snapshot(&snap);

    float volts = snap.battery_mv / 1000.0f;
    if (snap.battery_percent <= 20) {
        snprintf(out_status, max_len, "%u%% (%.2f V) - Bateria Baixa", (unsigned int)snap.battery_percent, volts);
    } else if (snap.battery_percent >= 100) {
        snprintf(out_status, max_len, "%u%% (%.2f V) - Carga Completa", (unsigned int)snap.battery_percent, volts);
    } else {
        snprintf(out_status, max_len, "%u%% (%.2f V)", (unsigned int)snap.battery_percent, volts);
    }
}

int ui_bridge_bateria_get_status_porcentagem(void)
{
    ui_model_data_t snap;
    ui_model_get_snapshot(&snap);
    return snap.battery_percent;
}

int ui_bridge_bateria_get_status_tencao(void)
{
    ui_model_data_t snap;
    ui_model_get_snapshot(&snap);
    return snap.battery_mv;
}

/* ============================================================================
 * 7. ÁUDIO, CODEC E MÍDIA (AVRCP / CODEC WM8960)
 * ============================================================================
 */

bool ui_bridge_media_play_pause(void)
{
    /* Despacho assíncrono via Bluetooth AVRCP */
    ESP_LOGI(TAG, "Comando de mídia acionado: Play/Pause via AVRCP");
    /* Alterna play/pause */
    esp_err_t err = bt_avrcp_send_play();
    return (err == ESP_OK);
}

bool ui_bridge_media_next(void)
{
    ESP_LOGI(TAG, "Comando de mídia acionado: Próxima Faixa (Forward) via AVRCP");
    esp_err_t err = bt_avrcp_send_next();
    return (err == ESP_OK);
}

bool ui_bridge_media_prev(void)
{
    ESP_LOGI(TAG, "Comando de mídia acionado: Faixa Anterior (Backward) via AVRCP");
    esp_err_t err = bt_avrcp_send_prev();
    return (err == ESP_OK);
}

bool ui_bridge_audio_set_volume(uint8_t volume)
{
    if (volume > 100) volume = 100;

    /* Envia comando assíncrono para o ator de áudio configurar o ganho do codec WM8960 */
    audio_cmd_set_volume_send(volume);

    /* Persiste configuração no NVS com proteção de coalescência */
    settings_set_u8(SETTINGS_KEY_VOL_SLOT0, volume);

    /* Notifica o modelo reativo de UI */
    ui_model_data_t snap;
    ui_model_get_snapshot(&snap);

    ui_model_update_msg_t msg = {
        .type = UI_UPDATE_AUDIO_STATE,
        .payload.audio_state = {
            .volume = volume,
            .muted = snap.audio_muted,
            .eq_preset = snap.audio_eq_preset,
        }
    };
    ui_model_post_update(&msg);
    return true;
}

uint8_t ui_bridge_audio_get_volume(void)
{
    ui_model_data_t snap;
    ui_model_get_snapshot(&snap);
    return snap.audio_volume;
}

bool ui_bridge_audio_set_mute(bool mute)
{
    audio_cmd_set_mute_send(mute);

    ui_model_data_t snap;
    ui_model_get_snapshot(&snap);

    ui_model_update_msg_t msg = {
        .type = UI_UPDATE_AUDIO_STATE,
        .payload.audio_state = {
            .volume = snap.audio_volume,
            .muted = mute,
            .eq_preset = snap.audio_eq_preset,
        }
    };
    ui_model_post_update(&msg);
    return true;
}

bool ui_bridge_audio_get_mute(void)
{
    ui_model_data_t snap;
    ui_model_get_snapshot(&snap);
    return snap.audio_muted;
}

bool ui_bridge_audio_set_eq_preset(uint8_t preset_id)
{
    if (preset_id > 3 && preset_id != 0xFF) return false;

    /* Aplica o preset de EQ no hardware codec */
    audio_cmd_set_eq_send(preset_id, NULL);

    ui_model_data_t snap;
    ui_model_get_snapshot(&snap);

    ui_model_update_msg_t msg = {
        .type = UI_UPDATE_AUDIO_STATE,
        .payload.audio_state = {
            .volume = snap.audio_volume,
            .muted = snap.audio_muted,
            .eq_preset = preset_id,
        }
    };
    ui_model_post_update(&msg);
    return true;
}

uint8_t ui_bridge_audio_get_eq_preset(void)
{
    ui_model_data_t snap;
    ui_model_get_snapshot(&snap);
    return snap.audio_eq_preset;
}

bool ui_bridge_audio_set_eq_gains(const int8_t gains_db[5])
{
    if (!gains_db) return false;

    /* Aplica as 5 bandas customizadas */
    audio_cmd_set_eq_send(0xFF, gains_db);

    ui_model_data_t snap;
    ui_model_get_snapshot(&snap);

    ui_model_update_msg_t msg = {
        .type = UI_UPDATE_AUDIO_STATE,
        .payload.audio_state = {
            .volume = snap.audio_volume,
            .muted = snap.audio_muted,
            .eq_preset = 0xFF,
        }
    };
    ui_model_post_update(&msg);
    return true;
}

/* ============================================================================
 * 8. EXTENSÕES DE BLUETOOTH MULTIPONTO & FAST PAIR
 * ============================================================================
 */

bool ui_bridge_bt_select_slot(uint8_t slot)
{
    if (slot > 1) return false;

    hs_actor_t *bt_act = bt_link_actor_get();
    if (bt_act) {
        bt_cmd_select_slot_t cmd = { .slot = slot };
        esp_err_t err = hs_actor_send(bt_act, BT_CMD_SELECT_SLOT, &cmd, sizeof(cmd), pdMS_TO_TICKS(50));
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Slot Bluetooth %u selecionado com sucesso via Bridge", slot);
            return true;
        }
    }
    return false;
}

bool ui_bridge_bt_toggle_active_slot(void)
{
    ui_model_data_t snap;
    ui_model_get_snapshot(&snap);
    uint8_t next_slot = (snap.bt_active_slot == 0) ? 1 : 0;
    return ui_bridge_bt_select_slot(next_slot);
}

bool ui_bridge_bt_start_pairing(void)
{
    hs_actor_t *bt_act = bt_link_actor_get();
    if (bt_act) {
        bt_cmd_start_pairing_t cmd = { .duration_sec = 120 };
        esp_err_t err = hs_actor_send(bt_act, BT_CMD_START_PAIRING, &cmd, sizeof(cmd), pdMS_TO_TICKS(50));
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Modo de pareamento iniciado (120 s) via Bridge");
            return true;
        }
    }
    return false;
}

bool ui_bridge_vibracall_trigger_test(uint16_t duration_ms, uint8_t intensity)
{
    if (duration_ms == 0) duration_ms = 200;
    if (intensity == 0) {
        ui_model_data_t snap;
        ui_model_get_snapshot(&snap);
        intensity = snap.vibracall_intensity > 0 ? snap.vibracall_intensity : 80;
    }
    ESP_LOGI(TAG, "Disparando teste de vibracao: %u ms com intensidade %u%%", duration_ms, intensity);
    actuators_vibrate(duration_ms, intensity);
    return true;
}

bool ui_bridge_fastpair_get_model_id(char *out_hex, size_t max_len)
{
    if (!out_hex || max_len < 7) return false;
    uint8_t mid[3] = {0};
    if (bt_fastpair_get_model_id(mid) == ESP_OK) {
        snprintf(out_hex, max_len, "%02X%02X%02X", mid[0], mid[1], mid[2]);
        return true;
    }
    return false;
}

bool ui_bridge_fastpair_set_model_id(const char *hex_str)
{
    if (!hex_str) return false;
    const char *s = hex_str;
    if (strncmp(s, "0x", 2) == 0 || strncmp(s, "0X", 2) == 0) {
        s += 2;
    }
    if (strlen(s) != 6) return false;

    uint8_t new_id[3] = {0};
    char byte_str[3] = {0};
    for (int i = 0; i < 3; i++) {
        byte_str[0] = s[i * 2];
        byte_str[1] = s[i * 2 + 1];
        byte_str[2] = '\0';
        char *endptr = NULL;
        long val = strtol(byte_str, &endptr, 16);
        if (endptr == byte_str) return false;
        new_id[i] = (uint8_t)val;
    }

    esp_err_t err = bt_fastpair_set_model_id(new_id);
    return (err == ESP_OK);
}

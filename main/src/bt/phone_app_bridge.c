/**
 * @file phone_app_bridge.c
 * @brief Implementação da comunicação entre o aplicativo de celular Bluetooth e
 * recursos do HeadSet.
 *
 * Faz o direcionamento e integração direta entre os comandos recebidos do
 * smartphone e as rotinas de hardware e sistema do headset (LVGL, Bluetooth
 * Link Manager, APDS-9930, etc.).
 *
 * Suporta também o processamento de comandos em JSON para transmissão via GATT
 * BLE ou SPP Bluetooth.
 */

#include "phone_app_bridge.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "ui_bridge.h"
#include "cJSON.h"
#include "bt_ctl_service.h"
#include "phone_ctl.h"

static const char *TAG = "phone_app_bridge";


esp_err_t phone_app_bridge_init(void) {
  ESP_LOGI(TAG, "Ponte de comunicacao Phone App <-> HeadSet inicializando");
  esp_err_t err = phone_ctl_init();
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "phone_ctl_init: %s", esp_err_to_name(err));
  }
  return ESP_OK;
}



/* ============================================================================
 * 1. BLUETOOTH
 * ============================================================================
 */

void phone_app_bt_get_nome_1(char *out_name, size_t max_len) {
  ui_bridge_bt_get_nome_1(out_name, max_len);
}

void phone_app_bt_get_nome_2(char *out_name, size_t max_len) {
  ui_bridge_bt_get_nome_2(out_name, max_len);
}

bool phone_app_bt_set_desconectar_dispositivo_1(bool disconnect) {
  return ui_bridge_bt_set_desconectar_dispositivo_1(disconnect);
}

bool phone_app_bt_set_desconectar_dispositivo_2(bool disconnect) {
  return ui_bridge_bt_set_desconectar_dispositivo_2(disconnect);
}

void phone_app_bt_get_status_dispositivo_1(char *out_status, size_t max_len) {
  ui_bridge_bt_get_status_dispositivo_1(out_status, max_len);
}

void phone_app_bt_get_status_dispositivo_2(char *out_status, size_t max_len) {
  ui_bridge_bt_get_status_dispositivo_2(out_status, max_len);
}

void phone_app_bt_get_status_alternancia(char *out_status, size_t max_len) {
  ui_bridge_bt_get_status_alternancia(out_status, max_len);
}

bool phone_app_bt_set_alternar_ativo(bool active) {
  return ui_bridge_bt_set_alternar_ativo(active);
}

/* ============================================================================
 * 2. PROXIMIDADE (Sensor APDS-9930)
 * ============================================================================
 */

void phone_app_proximidade_get_status(char *out_status, size_t max_len) {
  ui_bridge_proximidade_get_status(out_status, max_len);
}

bool phone_app_proximidade_set_ativar(bool enable) {
  return ui_bridge_proximidade_set_ativar(enable);
}

bool phone_app_proximidade_set_desativar(bool disable) {
  return ui_bridge_proximidade_set_desativar(disable);
}

int phone_app_proximidade_get_distancia_sensibilidade(void) {
  return ui_bridge_proximidade_get_distancia_sensibilidade();
}

bool phone_app_proximidade_set_distancia_sensibilidade(int sensibilidade) {
  return ui_bridge_proximidade_set_distancia_sensibilidade(sensibilidade);
}

/* ============================================================================
 * 3. VIBRACALL
 * ============================================================================
 */

void phone_app_vibracall_get_status(char *out_status, size_t max_len) {
  ui_bridge_vibracall_get_status(out_status, max_len);
}

bool phone_app_vibracall_set_ativar(bool enable) {
  return ui_bridge_vibracall_set_ativar(enable);
}

bool phone_app_vibracall_set_desativar(bool disable) {
  return ui_bridge_vibracall_set_desativar(disable);
}

int phone_app_vibracall_get_intensidade(void) {
  return ui_bridge_vibracall_get_intensidade();
}

bool phone_app_vibracall_set_intensidade(int intensidade) {
  return ui_bridge_vibracall_set_intensidade(intensidade);
}

/* ============================================================================
 * 4. ORELHAS (Servomotores)
 * ============================================================================
 */

void phone_app_orelhas_get_status(char *out_status, size_t max_len) {
  ui_bridge_orelhas_get_status(out_status, max_len);
}

bool phone_app_orelhas_set_ativar(bool enable) {
  return ui_bridge_orelhas_set_ativar(enable);
}

bool phone_app_orelhas_set_desativar(bool disable) {
  return ui_bridge_orelhas_set_desativar(disable);
}

int phone_app_orelhas_get_angulo_maximo(void) {
  return ui_bridge_orelhas_get_angulo_maximo();
}

bool phone_app_orelhas_set_angulo_maximo(int angulo) {
  return ui_bridge_orelhas_set_angulo_maximo(angulo);
}

/* ============================================================================
 * 5. DISPLAY
 * ============================================================================
 */

bool phone_app_display_get_status(void) {
  return ui_bridge_display_get_status();
}

bool phone_app_display_set_ativar(bool enable) {
  return ui_bridge_display_set_ativar(enable);
}

bool phone_app_display_set_desativar(bool disable) {
  return ui_bridge_display_set_desativar(disable);
}

int phone_app_display_get_brilho(void) {
  return ui_bridge_display_get_brilho();
}

bool phone_app_display_set_brilho(int brilho) {
  return ui_bridge_display_set_brilho(brilho);
}

int phone_app_display_get_tempo_tela(void) {
  return ui_bridge_display_get_tempo_tela();
}

bool phone_app_display_set_tempo_tela(int segundos) {
  return ui_bridge_display_set_tempo_tela(segundos);
}

void phone_app_display_get_galeria_imagens(char *out_json, size_t max_len) {
  if (!out_json || max_len == 0)
    return;

  ui_bridge_gallery_item_t items[UI_BRIDGE_MAX_GALLERY_ITEMS];
  int count =
      ui_bridge_display_get_galeria_imagens(items, UI_BRIDGE_MAX_GALLERY_ITEMS);

  int pos = snprintf(out_json, max_len, "[");
  for (int i = 0; i < count && pos < (int)max_len; i++) {
    pos += snprintf(out_json + pos, max_len - pos, "%s\"%s\"",
                    (i > 0) ? ", " : "", items[i].name);
  }
  if (pos < (int)max_len) {
    snprintf(out_json + pos, max_len - pos, "]");
  }
}

void phone_app_display_get_imagem_principal(char *out_nome, size_t max_len) {
  if (!out_nome || max_len == 0)
    return;

  /* Obtém a galeria para relacionar a imagem atual */
  ui_bridge_gallery_item_t items[UI_BRIDGE_MAX_GALLERY_ITEMS];
  int count =
      ui_bridge_display_get_galeria_imagens(items, UI_BRIDGE_MAX_GALLERY_ITEMS);
  const void *curr = ui_bridge_display_get_imagem_principal();

  for (int i = 0; i < count; i++) {
    if (items[i].img_src == curr) {
      strncpy(out_nome, items[i].name, max_len - 1);
      out_nome[max_len - 1] = '\0';
      return;
    }
  }
  snprintf(out_nome, max_len, "Padrao");
}

bool phone_app_display_set_imagem_principal(int index) {
  return ui_bridge_display_set_imagem_principal(index);
}

void phone_app_display_get_galeria_gifs(char *out_json, size_t max_len) {
  if (!out_json || max_len == 0)
    return;

  ui_bridge_gallery_gif_item_t items[UI_BRIDGE_MAX_GALLERY_ITEMS];
  int count =
      ui_bridge_display_get_galeria_gifs(items, UI_BRIDGE_MAX_GALLERY_ITEMS);

  int pos = snprintf(out_json, max_len, "[");
  for (int i = 0; i < count && pos < (int)max_len; i++) {
    pos += snprintf(out_json + pos, max_len - pos, "%s\"%s\"",
                    (i > 0) ? ", " : "", items[i].name);
  }
  if (pos < (int)max_len) {
    snprintf(out_json + pos, max_len - pos, "]");
  }
}

void phone_app_display_get_gif_principal(char *out_nome, size_t max_len) {
  if (!out_nome || max_len == 0)
    return;

  ui_bridge_gallery_gif_item_t items[UI_BRIDGE_MAX_GALLERY_ITEMS];
  int count =
      ui_bridge_display_get_galeria_gifs(items, UI_BRIDGE_MAX_GALLERY_ITEMS);
  const void *curr = ui_bridge_display_get_gif_principal();

  for (int i = 0; i < count; i++) {
    if (items[i].gif_src == curr) {
      strncpy(out_nome, items[i].name, max_len - 1);
      out_nome[max_len - 1] = '\0';
      return;
    }
  }
  /* Se for índice padrão */
  if (count > 0) {
    strncpy(out_nome, items[0].name, max_len - 1);
    out_nome[max_len - 1] = '\0';
  } else {
    snprintf(out_nome, max_len, "Padrao");
  }
}

bool phone_app_display_set_gif_principal(int index) {
  return ui_bridge_display_set_gif_principal(index);
}

/* ============================================================================
 * 6. BATERIA
 * ============================================================================
 */

void phone_app_bateria_get_status(char *out_status, size_t max_len) {
  ui_bridge_bateria_get_status(out_status, max_len);
}

int phone_app_bateria_get_status_porcentagem(void) {
  return ui_bridge_bateria_get_status_porcentagem();
}

int phone_app_bateria_get_status_tencao(void) {
  return ui_bridge_bateria_get_status_tencao();
}

/* ============================================================================
 * PROCESSADOR DE PACOTES DE COMANDO DO APP (JSON)
 * ============================================================================
 */

esp_err_t phone_app_bridge_process_command(const char *json_cmd,
                                           char *json_resp,
                                           size_t max_resp_len) {
  if (!json_cmd || !json_resp || max_resp_len == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  ESP_LOGI(TAG, "Processando comando recebido do app: %s", json_cmd);

  cJSON *root = cJSON_Parse(json_cmd);
  if (!root) {
    snprintf(json_resp, max_resp_len, "{\"status\":\"error\",\"msg\":\"json_invalido\"}");
    return ESP_ERR_INVALID_ARG;
  }

  cJSON *cmd_item = cJSON_GetObjectItem(root, "cmd");
  const char *cmd = cmd_item && cJSON_IsString(cmd_item) ? cmd_item->valuestring : "";

  if (strcmp(cmd, "get_all_status") == 0) {
    char n1[32], n2[32], st1[32], st2[32], st_alt[32];
    phone_app_bt_get_nome_1(n1, sizeof(n1));
    phone_app_bt_get_nome_2(n2, sizeof(n2));
    phone_app_bt_get_status_dispositivo_1(st1, sizeof(st1));
    phone_app_bt_get_status_dispositivo_2(st2, sizeof(st2));
    phone_app_bt_get_status_alternancia(st_alt, sizeof(st_alt));

    char prox_st[32], vib_st[32], ear_st[32], bat_st[32];
    phone_app_proximidade_get_status(prox_st, sizeof(prox_st));
    phone_app_vibracall_get_status(vib_st, sizeof(vib_st));
    phone_app_orelhas_get_status(ear_st, sizeof(ear_st));
    phone_app_bateria_get_status(bat_st, sizeof(bat_st));

    snprintf(json_resp, max_resp_len,
             "{\"bt_d1\":\"%s\",\"bt_d2\":\"%s\",\"st1\":\"%s\",\"st2\":\"%s\","
             "\"alt\":\"%s\","
             "\"prox\":\"%s\",\"vib\":\"%s\",\"orelhas\":\"%s\",\"bat\":\"%s\",\"disp\":%s,"
             "\"brilho\":%d}",
             n1, n2, st1, st2, st_alt, prox_st, vib_st, ear_st, bat_st,
             phone_app_display_get_status() ? "true" : "false",
             phone_app_display_get_brilho());
  } else {
    /* Resposta padrão de confirmação (ACK) */
    snprintf(json_resp, max_resp_len, "{\"status\":\"ok\"}");
  }

  cJSON_Delete(root);
  return ESP_OK;
}


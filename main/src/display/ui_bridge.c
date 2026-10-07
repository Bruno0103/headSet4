/**
 * @file ui_bridge.c
 * @brief Implementação da camada de comunicação e ponte entre o LVGL e os
 * recursos do HeadSet.
 *
 * Conecta os comandos da interface gráfica (LVGL 9) aos drivers do sistema:
 * - Bluetooth: interage com bt_link_mgr, bt_gap e headset_events.
 * - Proximidade: interage com apds9930 e headset_events.
 * - Vibracall, Orelhas, Display: gerencia estados em memória, timers e I/Os.
 *
 * Todo o código é documentado com comentários detalhados e mecanismos
 * thread-safe.
 */

#include "ui_bridge.h"

#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "apds9930.h"
#include "bt_gap.h"
#include "bt_link_mgr.h"
#include "headset_events.h"
#include "pinout.h"
#include "ui_gen.h"

static const char *TAG = "ui_bridge";

/* ============================================================================
 * ESTRUTURAS E ESTADOS INTERNOS
 * ============================================================================
 */

/* Mutex para proteção de chamadas concorrentes vindas da task do LVGL e de
 * eventos */
static SemaphoreHandle_t s_bridge_mutex = NULL;

#define BRIDGE_LOCK()                                                          \
  if (s_bridge_mutex)                                                          \
  xSemaphoreTake(s_bridge_mutex, portMAX_DELAY)
#define BRIDGE_UNLOCK()                                                        \
  if (s_bridge_mutex)                                                          \
  xSemaphoreGive(s_bridge_mutex)

/* Estado interno dos módulos */
static struct {
  /* 1. Bluetooth */
  char bt_name_slot1[UI_BRIDGE_STR_MAX_LEN];
  char bt_name_slot2[UI_BRIDGE_STR_MAX_LEN];
  bool bt_auto_switch_enabled;

  /* 2. Proximidade */
  bool prox_enabled;
  int prox_sensitivity_distance; /* Limiar de contagem para detecção */

  /* 3. Vibracall */
  bool vibracall_enabled;
  int vibracall_intensity; /* 0 a 100% */
  bool vibracall_active_now;

  /* 4. Orelhas */
  bool orelhas_enabled;
  int orelhas_max_angle; /* Em graus, ex: 0 a 180 */

  /* 5. Display */
  bool display_on;
  int display_brightness;         /* 0 a 100% */
  int display_screen_timeout_sec; /* Segundos até suspender tela */
  int display_selected_img_index;
} s_state = {.bt_name_slot1 = "Smartphone A",
             .bt_name_slot2 = "Notebook B",
             .bt_auto_switch_enabled = true,

             .prox_enabled = true,
             .prox_sensitivity_distance = 60,

             .vibracall_enabled = true,
             .vibracall_intensity = 80,
             .vibracall_active_now = false,

             .orelhas_enabled = true,
             .orelhas_max_angle = 120,

             .display_on = true,
             .display_brightness = 100,
             .display_screen_timeout_sec = 30,
             .display_selected_img_index = 0};

/* Itens da galeria de imagens disponíveis na UI (geradas pelo LVGL UI
 * Generator) */
static const ui_bridge_gallery_item_t s_gallery_items[] = {
    {.name = "Headphones", .img_src = &image_headphones_1109},
    {.name = "Bluetooth Icon", .img_src = &image_bluetooth_1115},
    {.name = "CPU Chip", .img_src = &image_cpu_1111},
    {.name = "Bateria Normal", .img_src = &image_battery_1105},
    {.name = "Bateria Carga", .img_src = &image_battery_charging_1113},
};
#define GALLERY_ITEMS_COUNT                                                    \
  (sizeof(s_gallery_items) / sizeof(s_gallery_items[0]))

/* ============================================================================
 * INICIALIZAÇÃO
 * ============================================================================
 */

static int s_battery_percent = 85;
static int s_battery_millivolts = 3950;

static void on_battery_event(void *handler_args, esp_event_base_t base, int32_t id, void *event_data) {
  if (base == HEADSET_EVENT && id == HEADSET_EVT_BATTERY && event_data) {
    headset_battery_evt_t *ev = (headset_battery_evt_t *)event_data;
    BRIDGE_LOCK();
    s_battery_percent = ev->percent;
    s_battery_millivolts = ev->millivolts;
    BRIDGE_UNLOCK();
  }
}

esp_err_t ui_bridge_init(void) {
  if (s_bridge_mutex == NULL) {
    s_bridge_mutex = xSemaphoreCreateMutex();
    if (s_bridge_mutex == NULL) {
      ESP_LOGE(TAG, "Falha ao alocar mutex da UI Bridge");
      return ESP_ERR_NO_MEM;
    }
  }

  /* Assina eventos de bateria do headset */
  headset_event_register(HEADSET_EVT_BATTERY, on_battery_event, NULL);

  ESP_LOGI(TAG, "Camada UI Bridge inicializada com sucesso");
  return ESP_OK;
}

/* ============================================================================
 * 1. BLUETOOTH
 * ============================================================================
 */

void ui_bridge_bt_get_nome_1(char *out_name, size_t max_len) {
  if (!out_name || max_len == 0)
    return;

  BRIDGE_LOCK();
  /* Copia o nome amigável registrado para o Slot 1 */
  strncpy(out_name, s_state.bt_name_slot1, max_len - 1);
  out_name[max_len - 1] = '\0';
  BRIDGE_UNLOCK();
}

void ui_bridge_bt_get_nome_2(char *out_name, size_t max_len) {
  if (!out_name || max_len == 0)
    return;

  BRIDGE_LOCK();
  /* Copia o nome amigável registrado para o Slot 2 */
  strncpy(out_name, s_state.bt_name_slot2, max_len - 1);
  out_name[max_len - 1] = '\0';
  BRIDGE_UNLOCK();
}

bool ui_bridge_bt_set_desconectar_dispositivo_1(bool disconnect) {
  if (!disconnect)
    return false;

  ESP_LOGI(TAG, "UI solicitou desconexao do Dispositivo 1");
  /* Se o slot 1 for o atualmente selecionado no link manager, alternamos ou
   * desligamos conexão */
  if (bt_link_mgr_selected() == 0) {
    /* Posta evento do botão para alternar ou forçar desligamento */
    headset_event_post(HEADSET_EVT_BUTTON_SWITCH, NULL, 0);
  }
  return true;
}

bool ui_bridge_bt_set_desconectar_dispositivo_2(bool disconnect) {
  if (!disconnect)
    return false;

  ESP_LOGI(TAG, "UI solicitou desconexao do Dispositivo 2");
  /* Se o slot 2 for o selecionado (índice 1), desconectamos via comutação de
   * slot */
  if (bt_link_mgr_selected() == 1) {
    headset_event_post(HEADSET_EVT_BUTTON_SWITCH, NULL, 0);
  }
  return true;
}

void ui_bridge_bt_get_status_dispositivo_1(char *out_status, size_t max_len) {
  if (!out_status || max_len == 0)
    return;

  bt_link_state_t st = bt_link_mgr_state(0);
  int sel = bt_link_mgr_selected();

  if (st == BT_LINK_ACTIVE && sel == 0) {
    snprintf(out_status, max_len, "Conectado e Ativo");
  } else if (sel == 0) {
    snprintf(out_status, max_len, "Conectando / Standby");
  } else {
    snprintf(out_status, max_len, "Em espera (Slot 2 ativo)");
  }
}

void ui_bridge_bt_get_status_dispositivo_2(char *out_status, size_t max_len) {
  if (!out_status || max_len == 0)
    return;

  bt_link_state_t st = bt_link_mgr_state(1);
  int sel = bt_link_mgr_selected();

  if (st == BT_LINK_ACTIVE && sel == 1) {
    snprintf(out_status, max_len, "Conectado e Ativo");
  } else if (sel == 1) {
    snprintf(out_status, max_len, "Conectando / Standby");
  } else {
    snprintf(out_status, max_len, "Em espera (Slot 1 ativo)");
  }
}

void ui_bridge_bt_get_status_alternancia(char *out_status, size_t max_len) {
  if (!out_status || max_len == 0)
    return;

  BRIDGE_LOCK();
  int sel = bt_link_mgr_selected();
  if (!s_state.bt_auto_switch_enabled) {
    snprintf(out_status, max_len, "Fixado no Slot %d", sel + 1);
  } else {
    snprintf(out_status, max_len, "Alternancia Ativa (Disp %d)", sel + 1);
  }
  BRIDGE_UNLOCK();
}

bool ui_bridge_bt_set_alternar_ativo(bool active) {
  BRIDGE_LOCK();
  s_state.bt_auto_switch_enabled = active;
  ESP_LOGI(TAG, "Alternancia automatica de dispositivos configurada para: %s",
           active ? "ATIVADA" : "DESATIVADA");
  BRIDGE_UNLOCK();
  return true;
}

/* ============================================================================
 * 2. PROXIMIDADE (Sensor APDS-9930)
 * ============================================================================
 */

void ui_bridge_proximidade_get_status(char *out_status, size_t max_len) {
  if (!out_status || max_len == 0)
    return;

  BRIDGE_LOCK();
  if (!s_state.prox_enabled) {
    snprintf(out_status, max_len, "Desativado (Modo forçado)");
    BRIDGE_UNLOCK();
    return;
  }
  BRIDGE_UNLOCK();

  bool worn = apds9930_is_worn();
  if (worn) {
    snprintf(out_status, max_len, "Fone na cabeca");
  } else {
    snprintf(out_status, max_len, "Fone retirado");
  }
}

bool ui_bridge_proximidade_set_ativar(bool enable) {
  BRIDGE_LOCK();
  s_state.prox_enabled = enable;
  ESP_LOGI(TAG, "Sensor de proximidade: %s", enable ? "ATIVADO" : "DESATIVADO");
  BRIDGE_UNLOCK();
  return true;
}

bool ui_bridge_proximidade_set_desativar(bool disable) {
  BRIDGE_LOCK();
  s_state.prox_enabled = !disable;
  ESP_LOGI(TAG, "Sensor de proximidade desativacao: %s",
           disable ? "DESATIVADO" : "ATIVADO");
  /* Se for desativado pelo usuario, garantimos que o sistema opere como fone
   * colocado na cabeca */
  if (disable) {
    headset_event_post(HEADSET_EVT_WORN, NULL, 0);
  }
  BRIDGE_UNLOCK();
  return true;
}

int ui_bridge_proximidade_get_distancia_sensibilidade(void) {
  BRIDGE_LOCK();
  int dist = s_state.prox_sensitivity_distance;
  BRIDGE_UNLOCK();
  return dist;
}

bool ui_bridge_proximidade_set_distancia_sensibilidade(int sensibilidade) {
  if (sensibilidade < 0 || sensibilidade > 1023) {
    ESP_LOGW(
        TAG,
        "Valor de sensibilidade de proximidade fora dos limites (0-1023): %d",
        sensibilidade);
    return false;
  }

  BRIDGE_LOCK();
  s_state.prox_sensitivity_distance = sensibilidade;
  ESP_LOGI(TAG, "Nova sensibilidade de proximidade aplicada: %d",
           sensibilidade);
  BRIDGE_UNLOCK();
  return true;
}

/* ============================================================================
 * 3. VIBRACALL
 * ============================================================================
 */

void ui_bridge_vibracall_get_status(char *out_status, size_t max_len) {
  if (!out_status || max_len == 0)
    return;

  BRIDGE_LOCK();
  if (!s_state.vibracall_enabled) {
    snprintf(out_status, max_len, "Desativado");
  } else if (s_state.vibracall_active_now) {
    snprintf(out_status, max_len, "Vibrando (%d%%)",
             s_state.vibracall_intensity);
  } else {
    snprintf(out_status, max_len, "Pronto (%d%%)", s_state.vibracall_intensity);
  }
  BRIDGE_UNLOCK();
}

bool ui_bridge_vibracall_set_ativar(bool enable) {
  BRIDGE_LOCK();
  s_state.vibracall_enabled = enable;
  ESP_LOGI(TAG, "Vibracall: %s", enable ? "ATIVADO" : "DESATIVADO");
  BRIDGE_UNLOCK();
  return true;
}

bool ui_bridge_vibracall_set_desativar(bool disable) {
  BRIDGE_LOCK();
  s_state.vibracall_enabled = !disable;
  ESP_LOGI(TAG, "Vibracall desativacao: %s",
           disable ? "DESATIVADO" : "ATIVADO");
  BRIDGE_UNLOCK();
  return true;
}

int ui_bridge_vibracall_get_intensidade(void) {
  BRIDGE_LOCK();
  int val = s_state.vibracall_intensity;
  BRIDGE_UNLOCK();
  return val;
}

bool ui_bridge_vibracall_set_intensidade(int intensidade) {
  if (intensidade < 0 || intensidade > 100) {
    ESP_LOGW(TAG, "Intensidade do vibracall invalida (0-100): %d", intensidade);
    return false;
  }

  BRIDGE_LOCK();
  s_state.vibracall_intensity = intensidade;
  ESP_LOGI(TAG, "Intensidade do vibracall ajustada para %d%%", intensidade);
  BRIDGE_UNLOCK();
  return true;
}

/* ============================================================================
 * 4. ORELHAS (Servomotores)
 * ============================================================================
 */

void ui_bridge_orelhas_get_status(char *out_status, size_t max_len) {
  if (!out_status || max_len == 0)
    return;

  BRIDGE_LOCK();
  if (!s_state.orelhas_enabled) {
    snprintf(out_status, max_len, "Desativadas");
  } else {
    snprintf(out_status, max_len, "Ativas (Max: %d deg)",
             s_state.orelhas_max_angle);
  }
  BRIDGE_UNLOCK();
}

bool ui_bridge_orelhas_set_ativar(bool enable) {
  BRIDGE_LOCK();
  s_state.orelhas_enabled = enable;
  ESP_LOGI(TAG, "Mecanismo das orelhas: %s", enable ? "ATIVADO" : "DESATIVADO");
  BRIDGE_UNLOCK();
  return true;
}

bool ui_bridge_orelhas_set_desativar(bool disable) {
  BRIDGE_LOCK();
  s_state.orelhas_enabled = !disable;
  ESP_LOGI(TAG, "Mecanismo das orelhas desativacao: %s",
           disable ? "DESATIVADO" : "ATIVADO");
  BRIDGE_UNLOCK();
  return true;
}

int ui_bridge_orelhas_get_angulo_maximo(void) {
  BRIDGE_LOCK();
  int angle = s_state.orelhas_max_angle;
  BRIDGE_UNLOCK();
  return angle;
}

bool ui_bridge_orelhas_set_angulo_maximo(int angulo) {
  if (angulo < 0 || angulo > 180) {
    ESP_LOGW(TAG, "Angulo maximo das orelhas invalido (0-180 graus): %d",
             angulo);
    return false;
  }

  BRIDGE_LOCK();
  s_state.orelhas_max_angle = angulo;
  ESP_LOGI(TAG, "Angulo maximo das orelhas configurado para %d graus", angulo);
  BRIDGE_UNLOCK();
  return true;
}

/* ============================================================================
 * 5. DISPLAY & INTERFACE
 * ============================================================================
 */

bool ui_bridge_display_get_status(void) {
  BRIDGE_LOCK();
  bool on = s_state.display_on;
  BRIDGE_UNLOCK();
  return on;
}

bool ui_bridge_display_set_ativar(bool enable) {
  BRIDGE_LOCK();
  s_state.display_on = enable;
  /* Controla o backlight físico via GPIO_BL definido no pinout */
  gpio_set_level((gpio_num_t)LCD_GPIO_BL, enable ? 1 : 0);
  ESP_LOGI(TAG, "Display: %s", enable ? "LIGADO" : "DESLIGADO");
  BRIDGE_UNLOCK();
  return true;
}

bool ui_bridge_display_set_desativar(bool disable) {
  BRIDGE_LOCK();
  s_state.display_on = !disable;
  /* Apaga o backlight para economia imediata */
  gpio_set_level((gpio_num_t)LCD_GPIO_BL, disable ? 0 : 1);
  ESP_LOGI(TAG, "Display desativacao: %s", disable ? "DESLIGADO" : "LIGADO");
  BRIDGE_UNLOCK();
  return true;
}

int ui_bridge_display_get_brilho(void) {
  BRIDGE_LOCK();
  int brilho = s_state.display_brightness;
  BRIDGE_UNLOCK();
  return brilho;
}

bool ui_bridge_display_set_brilho(int brilho) {
  if (brilho < 0 || brilho > 100) {
    ESP_LOGW(TAG, "Valor de brilho invalido (0-100%%): %d", brilho);
    return false;
  }

  BRIDGE_LOCK();
  s_state.display_brightness = brilho;
  /* Em caso de brilho 0 apaga backlight, caso maior que 0 mantém aceso */
  gpio_set_level((gpio_num_t)LCD_GPIO_BL, brilho > 0 ? 1 : 0);
  ESP_LOGI(TAG, "Brilho do display ajustado para %d%%", brilho);
  BRIDGE_UNLOCK();
  return true;
}

int ui_bridge_display_get_tempo_tela(void) {
  BRIDGE_LOCK();
  int sec = s_state.display_screen_timeout_sec;
  BRIDGE_UNLOCK();
  return sec;
}

bool ui_bridge_display_set_tempo_tela(int segundos) {
  if (segundos < 0 || segundos > 3600) {
    ESP_LOGW(TAG, "Tempo de tela invalido (0-3600s): %d", segundos);
    return false;
  }

  BRIDGE_LOCK();
  s_state.display_screen_timeout_sec = segundos;
  ESP_LOGI(TAG, "Timeout de tela configurado para %d segundos (0=infinito)",
           segundos);
  BRIDGE_UNLOCK();
  return true;
}

int ui_bridge_display_get_galeria_imagens(ui_bridge_gallery_item_t *out_items,
                                          int max_items) {
  if (!out_items || max_items <= 0)
    return 0;

  int count = (int)GALLERY_ITEMS_COUNT;
  if (count > max_items)
    count = max_items;

  for (int i = 0; i < count; i++) {
    out_items[i] = s_gallery_items[i];
  }
  return count;
}

const void *ui_bridge_display_get_imagem_principal(void) {
  BRIDGE_LOCK();
  int idx = s_state.display_selected_img_index;
  if (idx < 0 || idx >= (int)GALLERY_ITEMS_COUNT) {
    idx = 0;
  }
  const void *src = s_gallery_items[idx].img_src;
  BRIDGE_UNLOCK();
  return src;
}

bool ui_bridge_display_set_imagem_principal(int index) {
  if (index < 0 || index >= (int)GALLERY_ITEMS_COUNT) {
    ESP_LOGW(TAG, "Indice de imagem da galeria invalido: %d", index);
    return false;
  }

  BRIDGE_LOCK();
  s_state.display_selected_img_index = index;
  ESP_LOGI(TAG, "Imagem principal alterada para o item %d: %s", index,
           s_gallery_items[index].name);
  BRIDGE_UNLOCK();
  return true;
}

/* Galeria de GIFs / Animações disponíveis para a tela */
static const ui_bridge_gallery_gif_item_t s_gallery_gif_items[] = {
    {.name = "Animacao Padrao", .gif_src = NULL},
    {.name = "Ondas Sonoras", .gif_src = NULL},
    {.name = "Equalizador Grafico", .gif_src = NULL},
    {.name = "Pulso de Energia", .gif_src = NULL},
};
#define GALLERY_GIF_ITEMS_COUNT                                                \
  (sizeof(s_gallery_gif_items) / sizeof(s_gallery_gif_items[0]))

static int s_display_selected_gif_index = 0;

int ui_bridge_display_get_galeria_gifs(ui_bridge_gallery_gif_item_t *out_items,
                                       int max_items) {
  if (!out_items || max_items <= 0)
    return 0;

  int count = (int)GALLERY_GIF_ITEMS_COUNT;
  if (count > max_items)
    count = max_items;

  for (int i = 0; i < count; i++) {
    out_items[i] = s_gallery_gif_items[i];
  }
  return count;
}

const void *ui_bridge_display_get_gif_principal(void) {
  BRIDGE_LOCK();
  int idx = s_display_selected_gif_index;
  if (idx < 0 || idx >= (int)GALLERY_GIF_ITEMS_COUNT) {
    idx = 0;
  }
  const void *src = s_gallery_gif_items[idx].gif_src;
  BRIDGE_UNLOCK();
  return src;
}

bool ui_bridge_display_set_gif_principal(int index) {
  if (index < 0 || index >= (int)GALLERY_GIF_ITEMS_COUNT) {
    ESP_LOGW(TAG, "Indice de GIF da galeria invalido: %d", index);
    return false;
  }

  BRIDGE_LOCK();
  s_display_selected_gif_index = index;
  ESP_LOGI(TAG, "GIF principal alterado para o item %d: %s", index,
           s_gallery_gif_items[index].name);
  BRIDGE_UNLOCK();
  return true;
}

/* ============================================================================
 * 6. BATERIA
 * ============================================================================
 */

void ui_bridge_bateria_get_status(char *out_status, size_t max_len) {
  if (!out_status || max_len == 0)
    return;

  BRIDGE_LOCK();
  int percent = s_battery_percent;
  int mv = s_battery_millivolts;
  BRIDGE_UNLOCK();

  float volts = mv / 1000.0f;
  if (percent <= 20) {
    snprintf(out_status, max_len, "%d%% (%.2f V) - Bateria Baixa", percent, volts);
  } else if (percent >= 100) {
    snprintf(out_status, max_len, "%d%% (%.2f V) - Carga Completa", percent, volts);
  } else {
    snprintf(out_status, max_len, "%d%% (%.2f V)", percent, volts);
  }
}

int ui_bridge_bateria_get_status_porcentagem(void) {
  BRIDGE_LOCK();
  int percent = s_battery_percent;
  BRIDGE_UNLOCK();
  return percent;
}

int ui_bridge_bateria_get_status_tencao(void) {
  BRIDGE_LOCK();
  int mv = s_battery_millivolts;
  BRIDGE_UNLOCK();
  return mv;
}

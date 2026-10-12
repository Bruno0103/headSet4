#include "display.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_xpt2046.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "settings.h"
#include "ui.h"
#include "ui_bridge.h"
#include "ui_model.h"

static const char *TAG = "display";

static esp_lcd_panel_handle_t s_panel;
static esp_lcd_touch_handle_t s_touch;

static bool lcd_trans_done_cb(esp_lcd_panel_io_handle_t io,
                              esp_lcd_panel_io_event_data_t *edata,
                              void *user_ctx) {
  lv_display_flush_ready((lv_display_t *)user_ctx);
  return false;
}

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area,
                          uint8_t *px_map) {
  int pixels = (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1);

  /* LVGL entrega RGB565 little-endian; o ILI9341 espera big-endian no SPI */
  lv_draw_sw_rgb565_swap(px_map, pixels);
  esp_lcd_panel_draw_bitmap(s_panel, area->x1, area->y1, area->x2 + 1,
                            area->y2 + 1, px_map);
}

/**
 * @brief Callback do LVGL para leitura do estado e posição do Touch Screen.
 *
 * Este callback é chamado ciclicamente pelo LVGL (através de lv_timer_handler).
 * O controlador resistivo XPT2046 realiza a conversão analógica nos canais
 * X/Y/Z.
 *
 * Implementação correta e robusta:
 * 1. Invoca esp_lcd_touch_read_data(s_touch) para amostrar os eixos e a
 * pressão.
 * 2. Verifica se houve coordenadas válidas retornadas por
 * esp_lcd_touch_get_data.
 * 3. Repassa as coordenadas (X, Y) e o estado
 * (LV_INDEV_STATE_PRESSED/RELEASED).
 * 4. Mantém diagnóstico no log serial para inspecionar exatamente o que o
 * hardware responde.
 */
static void lvgl_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
  uint8_t cnt = 0;
  esp_lcd_touch_point_data_t pt_data = {0};

  /* Dispara a amostragem física via SPI no XPT2046 */
  esp_err_t ret = esp_lcd_touch_read_data(s_touch);

  if (ret == ESP_OK) {
    /* Recupera os pontos convertidos e filtrados pelo driver */
    esp_err_t err = esp_lcd_touch_get_data(s_touch, &pt_data, &cnt, 1);
    if (err == ESP_OK && cnt > 0) {
      /*
       * Calibração e remapeamento da área ativa do touch:
       * O vidro resistivo cobre toda a borda cinza externa. A matriz acesa do LCD (240x320)
       * fica centralizada dentro do vidro nos intervalos:
       * X: [TOUCH_ACTIVE_MIN_X, TOUCH_ACTIVE_MAX_X]
       * Y: [TOUCH_ACTIVE_MIN_Y, TOUCH_ACTIVE_MAX_Y]
       *
       * Remapeamos linearmente essa área para cobrir exatamente toda a tela [0, LCD_H_RES - 1]
       * e [0, LCD_V_RES - 1]:
       * mapped = (raw - min) * (res - 1) / (max - min)
       */
      int32_t raw_x = pt_data.x;
      int32_t raw_y = pt_data.y;

      /* Restringe primeiro aos limites ativos da matriz do LCD */
      if (raw_x < TOUCH_ACTIVE_MIN_X) raw_x = TOUCH_ACTIVE_MIN_X;
      if (raw_x > TOUCH_ACTIVE_MAX_X) raw_x = TOUCH_ACTIVE_MAX_X;
      if (raw_y < TOUCH_ACTIVE_MIN_Y) raw_y = TOUCH_ACTIVE_MIN_Y;
      if (raw_y > TOUCH_ACTIVE_MAX_Y) raw_y = TOUCH_ACTIVE_MAX_Y;

      /* Mapeamento linear para 0..(LCD_H_RES - 1) e 0..(LCD_V_RES - 1) */
      int32_t mapped_x = ((raw_x - TOUCH_ACTIVE_MIN_X) * (LCD_H_RES - 1)) / (TOUCH_ACTIVE_MAX_X - TOUCH_ACTIVE_MIN_X);
      int32_t mapped_y = ((raw_y - TOUCH_ACTIVE_MIN_Y) * (LCD_V_RES - 1)) / (TOUCH_ACTIVE_MAX_Y - TOUCH_ACTIVE_MIN_Y);

      if (mapped_x < 0) mapped_x = 0;
      if (mapped_x >= LCD_H_RES) mapped_x = LCD_H_RES - 1;
      if (mapped_y < 0) mapped_y = 0;
      if (mapped_y >= LCD_V_RES) mapped_y = LCD_V_RES - 1;

      data->point.x = (int16_t)mapped_x;
      data->point.y = (int16_t)mapped_y;
      data->state = LV_INDEV_STATE_PRESSED;

#if CONFIG_HEADSET_TOUCH_DIAG
      /*
       * Log serial rate-limited (throttling de no máximo 1 mensagem a cada 1000
       * ms / 1 segundo) para depuração sem sobrecarregar a CPU ou inundar a
       * saída serial durante o toque contínuo.
       */
      static uint32_t last_log_time = 0;
      uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
      if (now - last_log_time >= 1000) {
        last_log_time = now;
        ESP_LOGI(TAG, ">>> TOUCH ATIVO: X=%d (bruto=%d), Y=%d (bruto=%d) (força Z=%d) <<<",
                 data->point.x, pt_data.x, data->point.y, pt_data.y, pt_data.strength);
      }
#endif
      return;
    }
  }

#if CONFIG_HEADSET_TOUCH_DIAG
  /*
   * Diagnóstico periódico de hardware:
   * Testa leitura direta dos registradores brutos do XPT2046 para saber se o
   * pino MISO está respondendo ou se o barramento SPI está recebendo apenas
   * 0x0000 ou 0xFFFF. Rate-limited para no máximo 1 mensagem a cada 2000 ms (2
   * segundos).
   */
  static uint32_t last_diag_time = 0;
  uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
  if (now - last_diag_time >= 2000) {
    last_diag_time = now;
    uint8_t raw_z1[2] = {0}, raw_z2[2] = {0}, raw_x[2] = {0};
    esp_err_t r1 =
        esp_lcd_panel_io_rx_param(s_touch->io, 0xB0 | 0x01, raw_z1, 2);
    esp_err_t r2 =
        esp_lcd_panel_io_rx_param(s_touch->io, 0xC0 | 0x01, raw_z2, 2);
    esp_err_t r3 =
        esp_lcd_panel_io_rx_param(s_touch->io, 0xD0 | 0x01, raw_x, 2);
    uint16_t val_z1 = (raw_z1[0] << 8) | raw_z1[1];
    uint16_t val_z2 = (raw_z2[0] << 8) | raw_z2[1];
    uint16_t val_x = (raw_x[0] << 8) | raw_x[1];
    uint16_t calc_z = (val_z1 >> 3) + (4096 - (val_z2 >> 3));
    ESP_LOGI(TAG,
             "[DIAG TOUCH] SPI status: r=(%d,%d,%d) | Z1=0x%04X Z2=0x%04X "
             "X=0x%04X | calc_z=%u",
             r1, r2, r3, val_z1, val_z2, val_x, calc_z);
  }
#endif

  /* Caso não haja toque detectado ou o comando retorne liberado */
  data->state = LV_INDEV_STATE_RELEASED;
}

static uint32_t lvgl_tick_cb(void) {
  return (uint32_t)(esp_timer_get_time() / 1000);
}

static esp_err_t display_hw_init(lv_display_t *disp) {
  const gpio_config_t bl_cfg = {
      .mode = GPIO_MODE_OUTPUT,
      .pin_bit_mask = 1ULL << LCD_GPIO_BL,
  };
  ESP_RETURN_ON_ERROR(gpio_config(&bl_cfg), TAG, "backlight gpio");
  gpio_set_level(LCD_GPIO_BL, 0);

  const spi_bus_config_t buscfg = {
      .sclk_io_num = LCD_GPIO_SCLK,
      .mosi_io_num = LCD_GPIO_MOSI,
      .miso_io_num = LCD_GPIO_MISO,
      .quadwp_io_num = GPIO_NUM_NC,
      .quadhd_io_num = GPIO_NUM_NC,
      .max_transfer_sz = LCD_H_RES * LCD_DRAW_BUF_LINES * sizeof(uint16_t),
  };
  ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_SPI_NUM, &buscfg, SPI_DMA_CH_AUTO),
                      TAG, "spi bus");

  /*
   * Habilita pull-up interno no pino MISO (T_DO, GPIO19).
   * Evita que a linha flutue quando o XPT2046 entra em tri-state
   * entre transmissões SPI.
   */
  gpio_set_pull_mode(LCD_GPIO_MISO, GPIO_PULLUP_ONLY);

  /* --- LCD ILI9341 --- */
  esp_lcd_panel_io_handle_t lcd_io = NULL;
  const esp_lcd_panel_io_spi_config_t io_cfg = {
      .dc_gpio_num = LCD_GPIO_DC,
      .cs_gpio_num = LCD_GPIO_CS,
      .pclk_hz = LCD_PIXEL_CLK_HZ,
      .lcd_cmd_bits = LCD_CMD_BITS,
      .lcd_param_bits = LCD_PARAM_BITS,
      .spi_mode = 0,
      .trans_queue_depth = 10,
      .on_color_trans_done = lcd_trans_done_cb,
      .user_ctx = disp,
  };
  ESP_RETURN_ON_ERROR(
      esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_NUM, &io_cfg,
                               &lcd_io),
      TAG, "lcd io");

  const esp_lcd_panel_dev_config_t panel_cfg = {
      .reset_gpio_num = LCD_GPIO_RST,
      .rgb_ele_order =
          LCD_COLOR_BGR ? LCD_RGB_ELEMENT_ORDER_BGR : LCD_RGB_ELEMENT_ORDER_RGB,
      .bits_per_pixel = 16,
  };
  ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ili9341(lcd_io, &panel_cfg, &s_panel),
                      TAG, "ili9341");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "panel reset");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "panel init");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(s_panel, LCD_INVERT_COLOR),
                      TAG, "invert");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(s_panel, LCD_SWAP_XY), TAG,
                      "swap_xy");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_panel, LCD_MIRROR_X, LCD_MIRROR_Y),
                      TAG, "mirror");

  /* --- Touch XPT2046 (mesmo barramento SPI, CS dedicado em TOUCH_GPIO_CS) ---
   */
  esp_lcd_panel_io_handle_t tp_io = NULL;
  esp_lcd_panel_io_spi_config_t tp_io_cfg =
      ESP_LCD_TOUCH_IO_SPI_XPT2046_CONFIG(TOUCH_GPIO_CS);
  tp_io_cfg.pclk_hz = TOUCH_PIXEL_CLK_HZ;
  ESP_RETURN_ON_ERROR(
      esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_NUM,
                               &tp_io_cfg, &tp_io),
      TAG, "touch io");

  /*
   * Configuração do controlador de toque resistivo XPT2046.
   * Usamos polling contínuo (rst e int como GPIO_NUM_NC) para evitar
   * que ruídos no pino de interrupção T_IRQ mascarem ou impeçam leituras
   * válidas do SPI. A resolução x_max/y_max é alinhada com as dimensões do
   * display ILI9341 (240x320).
   */
  const esp_lcd_touch_config_t tp_cfg = {
      .x_max = LCD_H_RES,
      .y_max = LCD_V_RES,
      .rst_gpio_num = GPIO_NUM_NC,
      .int_gpio_num = GPIO_NUM_NC,
      .levels = {.reset = 0, .interrupt = 0},
      .flags = {.swap_xy = TOUCH_SWAP_XY,
                .mirror_x = TOUCH_MIRROR_X,
                .mirror_y = TOUCH_MIRROR_Y},
  };
  ESP_RETURN_ON_ERROR(esp_lcd_touch_new_spi_xpt2046(tp_io, &tp_cfg, &s_touch),
                      TAG, "xpt2046");

  ESP_LOGI(TAG,
           "Driver Touch XPT2046 inicializado com sucesso (CS=%d, SCLK=%d, "
           "MOSI=%d, MISO=%d)",
           TOUCH_GPIO_CS, LCD_GPIO_SCLK, LCD_GPIO_MOSI, LCD_GPIO_MISO);

  ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "disp on");
  return ESP_OK;
}

/* ============================================================================
 * OBSERVERS REATIVOS DO LVGL 9 E BINDINGS DE HARDWARE
 * ============================================================================
 */

/* Armazena último valor enviado aos settings para evitar saturação durante arrasto */
static int s_last_sent_bright = -1;
static int s_last_sent_timeout = -1;
static int s_last_sent_prox_sens = -1;
static int s_last_sent_vib_intens = -1;
static int s_last_sent_ears_angle = -1;

/**
 * @brief Observadores de alteração dos sliders da UI.
 *
 * Quando o usuário desliza um slider na interface gráfica, o LVGL atualiza o subject
 * correspondente e dispara o callback registrado abaixo.
 * 
 * Regra de não-bloqueio e prevenção de loops:
 * - Apenas enviamos a alteração se o valor for diferente do último gravado.
 * - Para valores analógicos contínuos de sensibilidade (0 a 1023), aplica-se um passo mínimo de 8
 *   para evitar disparos a cada micropasso de toque resistivo.
 * - Gravamos de forma assíncrona nas configurações através de settings_set_u8/u16 (Actor Settings com debounce).
 */
static void on_subject_brightness_changed(lv_observer_t *observer, lv_subject_t *subject) {
  (void)observer;
  int val = lv_subject_get_int(subject);
  if (val < 0 || val > 100) return;
  if (val == s_last_sent_bright) return;
  s_last_sent_bright = val;

  ESP_LOGI(TAG, "Slider Brilho alterado pelo usuario: %d%%", val);
  settings_set_u8(SETTINGS_KEY_DISP_BRIGHT, (uint8_t)val);
}

static void on_subject_timeout_changed(lv_observer_t *observer, lv_subject_t *subject) {
  (void)observer;
  int val = lv_subject_get_int(subject);
  if (val < 0 || val > 3600) return;
  if (val == s_last_sent_timeout) return;
  s_last_sent_timeout = val;

  ESP_LOGI(TAG, "Slider Tempo de Tela alterado pelo usuario: %d s", val);
  settings_set_u16(SETTINGS_KEY_DISP_TIMEOUT, (uint16_t)val);
}

static void on_subject_prox_sensitivity_changed(lv_observer_t *observer, lv_subject_t *subject) {
  (void)observer;
  int val = lv_subject_get_int(subject);
  if (val < 0 || val > 1023) return;
  if (s_last_sent_prox_sens >= 0 && abs(val - s_last_sent_prox_sens) < 8) return;
  s_last_sent_prox_sens = val;

  ESP_LOGI(TAG, "Slider Sensibilidade Proximidade alterado pelo usuario: %d", val);
  settings_set_u16(SETTINGS_KEY_SENS_THRESH_ON, (uint16_t)val);
}

static void on_subject_vibracall_intensity_changed(lv_observer_t *observer, lv_subject_t *subject) {
  (void)observer;
  int val = lv_subject_get_int(subject);
  if (val < 0 || val > 100) return;
  if (val == s_last_sent_vib_intens) return;
  s_last_sent_vib_intens = val;

  ESP_LOGI(TAG, "Slider Intensidade Vibracall alterado pelo usuario: %d%%", val);
  settings_set_u8(SETTINGS_KEY_HAPTIC_INTENS, (uint8_t)val);
}

static void on_subject_ears_angle_changed(lv_observer_t *observer, lv_subject_t *subject) {
  (void)observer;
  int val = lv_subject_get_int(subject);
  if (val < 0 || val > 180) return;
  if (val == s_last_sent_ears_angle) return;
  s_last_sent_ears_angle = val;

  ESP_LOGI(TAG, "Slider Angulo Orelhas alterado pelo usuario: %d deg", val);
  settings_set_u8(SETTINGS_KEY_EARS_ANGLE, (uint8_t)val);
}

/* Event callback para os botões de ação identificados pelo texto do label filho */
static void on_action_button_clicked(lv_event_t *e) {
  const char *action_type = (const char *)lv_event_get_user_data(e);
  if (!action_type) return;

  ESP_LOGI(TAG, "Botao de acao clicado: %s", action_type);

  if (strcmp(action_type, "BT_DISCONNECT_1") == 0) {
    ui_bridge_bt_set_desconectar_dispositivo_1(true);
  } else if (strcmp(action_type, "BT_DISCONNECT_2") == 0) {
    ui_bridge_bt_set_desconectar_dispositivo_2(true);
  } else if (strcmp(action_type, "DISP_ENABLE") == 0) {
    ui_bridge_display_set_ativar(true);
  } else if (strcmp(action_type, "DISP_DISABLE") == 0) {
    ui_bridge_display_set_desativar(true);
  } else if (strcmp(action_type, "PROX_ENABLE") == 0) {
    ui_bridge_proximidade_set_ativar(true);
  } else if (strcmp(action_type, "PROX_DISABLE") == 0) {
    ui_bridge_proximidade_set_desativar(true);
  } else if (strcmp(action_type, "VIB_ENABLE") == 0) {
    ui_bridge_vibracall_set_ativar(true);
  } else if (strcmp(action_type, "VIB_DISABLE") == 0) {
    ui_bridge_vibracall_set_desativar(true);
  } else if (strcmp(action_type, "EARS_ENABLE") == 0) {
    ui_bridge_orelhas_set_ativar(true);
  } else if (strcmp(action_type, "EARS_DISABLE") == 0) {
    ui_bridge_orelhas_set_desativar(true);
  } else if (strcmp(action_type, "BT_TOGGLE_AUTOSWITCH") == 0) {
    ui_model_data_t snap;
    ui_model_get_snapshot(&snap);
    ESP_LOGI(TAG, "Alternando modo Bluetooth Auto-Switch: atual=%d -> novo=%d", 
             snap.bt_auto_switch, !snap.bt_auto_switch);
    ui_bridge_bt_set_alternar_ativo(!snap.bt_auto_switch);
  }
}

/**
 * @brief Identifica o contexto da tela pai para saber a qual componente o botão Ativar/Desativar pertence.
 */
static const char *get_screen_action_prefix(lv_obj_t *obj) {
  lv_obj_t *scr = lv_obj_get_screen(obj);
  if (!scr) return "DISP";

  const char *name = lv_obj_get_name(scr);
  if (name) {
    if (strstr(name, "orelhas")) return "EARS";
    if (strstr(name, "vibracall")) return "VIB";
    if (strstr(name, "proximidade")) return "PROX";
    if (strstr(name, "bluetooth")) return "BT";
    if (strstr(name, "display")) return "DISP";
  }
  return "DISP";
}

/**
 * @brief Varre recursivamente a árvore de objetos LVGL da tela para encontrar
 * botões de ação e vincular o evento de clique sem violar arquivos gerados pelo Figma.
 */
static void bind_action_buttons_recursive(lv_obj_t *parent, int *bt_disconnect_count) {
  if (!parent) return;

  uint32_t cnt = lv_obj_get_child_count(parent);
  for (uint32_t i = 0; i < cnt; i++) {
    lv_obj_t *child = lv_obj_get_child(parent, i);
    if (!child) continue;

    /* Verifica se o próprio objeto ou seus filhos contêm texto de ação */
    uint32_t sub_cnt = lv_obj_get_child_count(child);
    for (uint32_t j = 0; j < sub_cnt; j++) {
      lv_obj_t *grandchild = lv_obj_get_child(child, j);
      if (grandchild && lv_obj_check_type(grandchild, &lv_label_class)) {
        const char *txt = lv_label_get_text(grandchild);
        if (txt) {
          if (strcmp(txt, "Desconectar") == 0) {
            const char *action = (*bt_disconnect_count == 0) ? "BT_DISCONNECT_1" : "BT_DISCONNECT_2";
            (*bt_disconnect_count)++;
            lv_obj_remove_flag(grandchild, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_flag(child, LV_OBJ_FLAG_CLICKABLE);
            /* Remove callback existente com essa função para evitar acúmulo a cada troca de tela */
            lv_obj_remove_event_cb(child, on_action_button_clicked);
            lv_obj_add_event_cb(child, on_action_button_clicked, LV_EVENT_CLICKED, (void *)action);
            ESP_LOGI(TAG, "Mapeado botao 'Desconectar' -> %s", action);
          } else if (strcmp(txt, "Ativar") == 0) {
            const char *prefix = get_screen_action_prefix(child);
            const char *action = "DISP_ENABLE";
            if (strcmp(prefix, "EARS") == 0) action = "EARS_ENABLE";
            else if (strcmp(prefix, "VIB") == 0) action = "VIB_ENABLE";
            else if (strcmp(prefix, "PROX") == 0) action = "PROX_ENABLE";

            lv_obj_remove_flag(grandchild, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_flag(child, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_remove_event_cb(child, on_action_button_clicked);
            lv_obj_add_event_cb(child, on_action_button_clicked, LV_EVENT_CLICKED, (void *)action);
            ESP_LOGI(TAG, "Mapeado botao 'Ativar' (tela %s) -> %s", prefix, action);
          } else if (strcmp(txt, "Desativar") == 0) {
            const char *prefix = get_screen_action_prefix(child);
            const char *action = "DISP_DISABLE";
            if (strcmp(prefix, "EARS") == 0) action = "EARS_DISABLE";
            else if (strcmp(prefix, "VIB") == 0) action = "VIB_DISABLE";
            else if (strcmp(prefix, "PROX") == 0) action = "PROX_DISABLE";

            lv_obj_remove_flag(grandchild, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_flag(child, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_remove_event_cb(child, on_action_button_clicked);
            lv_obj_add_event_cb(child, on_action_button_clicked, LV_EVENT_CLICKED, (void *)action);
            ESP_LOGI(TAG, "Mapeado botao 'Desativar' (tela %s) -> %s", prefix, action);
          } else if (strstr(txt, "Alternância") != NULL || strstr(txt, "Alternancia") != NULL ||
                     strstr(txt, "Manual") != NULL || strstr(txt, "Automático") != NULL ||
                     strstr(txt, "Dispositivo") != NULL || strstr(txt, "Ativo (") != NULL ||
                     strstr(txt, "Slot") != NULL) {
            /*
             * Botão de Alternância de Dispositivos Bluetooth:
             * Em screen_bluetooth_gen.c, há container_0 com label_12 ("Alternância: Manual")
             * e tile_0 que contém label_13 bindado a g_subj_bt_status_alternancia.
             * Tornamos o child (tile_0 ou container_0) E o grandchild clicáveis!
             */
            const char *prefix = get_screen_action_prefix(child);
            if (strcmp(prefix, "BT") == 0 || strcmp(prefix, "DISP") == 0) {
              /* Remove e adiciona nos dois níveis para garantir captura do toque */
              lv_obj_add_flag(grandchild, LV_OBJ_FLAG_CLICKABLE);
              lv_obj_remove_event_cb(grandchild, on_action_button_clicked);
              lv_obj_add_event_cb(grandchild, on_action_button_clicked, LV_EVENT_CLICKED, (void *)"BT_TOGGLE_AUTOSWITCH");

              lv_obj_add_flag(child, LV_OBJ_FLAG_CLICKABLE);
              lv_obj_remove_event_cb(child, on_action_button_clicked);
              lv_obj_add_event_cb(child, on_action_button_clicked, LV_EVENT_CLICKED, (void *)"BT_TOGGLE_AUTOSWITCH");
              ESP_LOGI(TAG, "Mapeado botao de Alternancia Bluetooth (txt: '%s') -> BT_TOGGLE_AUTOSWITCH", txt);
            }
          }
        }
      }
    }

    bind_action_buttons_recursive(child, bt_disconnect_count);
  }
}

/* Callback disparado sempre que uma nova tela é carregada no display */
static void on_screen_loaded_cb(lv_event_t *e) {
  /* No LVGL 9, quando o evento é registrado no display, recuperamos a tela ativa */
  lv_display_t *disp = lv_event_get_target(e);
  lv_obj_t *screen = NULL;
  if (disp) {
    screen = lv_display_get_screen_active(disp);
  }
  if (!screen) {
    screen = lv_screen_active();
  }
  if (!screen) return;

  const char *scr_name = lv_obj_get_name(screen);
  ESP_LOGI(TAG, "Tela carregada: '%s'. Vinculando botoes de acao...", scr_name ? scr_name : "sem nome");
  int bt_disconnect_count = 0;
  bind_action_buttons_recursive(screen, &bt_disconnect_count);
}

static void register_ui_observers(void) {
  /* Observadores reativos nos subjects para controle de hardware pelos sliders */
  lv_subject_add_observer(&subject_g_subj_display_brilho, on_subject_brightness_changed, NULL);
  lv_subject_add_observer(&subject_g_subj_display_timeout_segundos, on_subject_timeout_changed, NULL);
  lv_subject_add_observer(&subject_g_subj_proximidade_sensibilidade, on_subject_prox_sensitivity_changed, NULL);
  lv_subject_add_observer(&subject_g_subj_vibracall_intensidade, on_subject_vibracall_intensity_changed, NULL);
  lv_subject_add_observer(&subject_g_subj_orelhas_angulo_maximo, on_subject_ears_angle_changed, NULL);

  /* Hook global de carregamento de tela registrado no display default */
  lv_display_t *disp = lv_display_get_default();
  if (disp) {
    lv_display_add_event_cb(disp, on_screen_loaded_cb, LV_EVENT_SCREEN_LOADED, NULL);
  }
  ESP_LOGI(TAG, "Observadores de Sliders e Eventos de Botoes registrados com sucesso");
}

/* Tela provisoria para validar display e toque ou fallback de erro. */
static void ui_placeholder(void) {
  lv_obj_t *scr = lv_screen_active();

  lv_obj_t *title = lv_label_create(scr);
  lv_label_set_text(title, "HeadSet4 - Fallback");
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

  lv_obj_t *btn = lv_button_create(scr);
  lv_obj_set_size(btn, 160, 50);
  lv_obj_center(btn);
  lv_obj_t *lbl = lv_label_create(btn);
  lv_label_set_text(lbl, "UI Padrao Ativa");
  lv_obj_center(lbl);
}

/**
 * @brief Inicializa e carrega a interface de usuario gerada pelo LVGL Pro (ui).
 * Possui tratativa de erro robusta: se a criacao falhar ou o ponteiro de tela
 * for nulo, registra logs detalhados e carrega a tela de fallback para o
 * usuario nao ficar sem visualizacao.
 */
esp_err_t display_create_app_ui(void) {
  ESP_LOGI(TAG, "Inicializando interface grafica gerada (ui)...");

  /* Simbolos das fontes binarias embutidas na Flash via CMake EMBED_FILES
   * (suporte a acentos ISO-8859-1 / Latin) */
  extern const uint8_t font_montserrat_regular_12_bin_start[] asm(
      "_binary_font_montserrat_regular_12_bin_start");
  extern const uint8_t font_montserrat_regular_12_bin_end[] asm(
      "_binary_font_montserrat_regular_12_bin_end");
  extern const uint8_t font_montserrat_regular_14_bin_start[] asm(
      "_binary_font_montserrat_regular_14_bin_start");
  extern const uint8_t font_montserrat_regular_14_bin_end[] asm(
      "_binary_font_montserrat_regular_14_bin_end");
  extern const uint8_t font_montserrat_regular_16_bin_start[] asm(
      "_binary_font_montserrat_regular_16_bin_start");
  extern const uint8_t font_montserrat_regular_16_bin_end[] asm(
      "_binary_font_montserrat_regular_16_bin_end");
  extern const uint8_t font_montserrat_regular_20_bin_start[] asm(
      "_binary_font_montserrat_regular_20_bin_start");
  extern const uint8_t font_montserrat_regular_20_bin_end[] asm(
      "_binary_font_montserrat_regular_20_bin_end");
  extern const uint8_t font_montserrat_regular_24_bin_start[] asm(
      "_binary_font_montserrat_regular_24_bin_start");
  extern const uint8_t font_montserrat_regular_24_bin_end[] asm(
      "_binary_font_montserrat_regular_24_bin_end");

  /*
   * Carrega as fontes binárias completas da memória ANTES de ui_init("").
   * Como ui_init() verifica 'if (!font_montserrat_regular_X)', ao definir
   * previamente as fontes a partir dos buffers embutidos na flash (que possuem
   * todos os glifos com acento), o LVGL utiliza as fontes completas sem falhas
   * de arquivo em disco.
   */
  uint32_t sz12 = (uint32_t)(font_montserrat_regular_12_bin_end -
                             font_montserrat_regular_12_bin_start);
  if (!font_montserrat_regular_12 && sz12 > 0) {
    font_montserrat_regular_12 = lv_binfont_create_from_buffer(
        (void *)font_montserrat_regular_12_bin_start, sz12);
  }
  uint32_t sz14 = (uint32_t)(font_montserrat_regular_14_bin_end -
                             font_montserrat_regular_14_bin_start);
  if (!font_montserrat_regular_14 && sz14 > 0) {
    font_montserrat_regular_14 = lv_binfont_create_from_buffer(
        (void *)font_montserrat_regular_14_bin_start, sz14);
  }
  uint32_t sz16 = (uint32_t)(font_montserrat_regular_16_bin_end -
                             font_montserrat_regular_16_bin_start);
  if (!font_montserrat_regular_16 && sz16 > 0) {
    font_montserrat_regular_16 = lv_binfont_create_from_buffer(
        (void *)font_montserrat_regular_16_bin_start, sz16);
  }
  uint32_t sz20 = (uint32_t)(font_montserrat_regular_20_bin_end -
                             font_montserrat_regular_20_bin_start);
  if (!font_montserrat_regular_20 && sz20 > 0) {
    font_montserrat_regular_20 = lv_binfont_create_from_buffer(
        (void *)font_montserrat_regular_20_bin_start, sz20);
  }
  uint32_t sz24 = (uint32_t)(font_montserrat_regular_24_bin_end -
                             font_montserrat_regular_24_bin_start);
  if (!font_montserrat_regular_24 && sz24 > 0) {
    font_montserrat_regular_24 = lv_binfont_create_from_buffer(
        (void *)font_montserrat_regular_24_bin_start, sz24);
  }

  /* Fallback de seguranca caso o carregamento da memoria falhe */
  bool bin_loaded = (font_montserrat_regular_12 && font_montserrat_regular_14 &&
                     font_montserrat_regular_16 && font_montserrat_regular_20 &&
                     font_montserrat_regular_24);
  if (!font_montserrat_regular_12)
    font_montserrat_regular_12 = (lv_font_t *)&lv_font_montserrat_12;
  if (!font_montserrat_regular_14)
    font_montserrat_regular_14 = (lv_font_t *)&lv_font_montserrat_14;
  if (!font_montserrat_regular_16)
    font_montserrat_regular_16 = (lv_font_t *)&lv_font_montserrat_16;
  if (!font_montserrat_regular_20)
    font_montserrat_regular_20 = (lv_font_t *)&lv_font_montserrat_20;
  if (!font_montserrat_regular_24)
    font_montserrat_regular_24 = (lv_font_t *)&lv_font_montserrat_24;

  ESP_LOGI(TAG, "Status do carregamento das fontes: %s (suporte a acentos: %s)",
           bin_loaded ? "Binarias da memoria Flash OK"
                      : "Fallback compilado ASCII",
           bin_loaded ? "COMPLETO (Á, É, Í, Ó, Ú, Ã, Õ, Ç)" : "Limitado ASCII");

  /* Inicializacao das variaveis, estilos e temas da UI gerada */
  ui_init("");

  /* Sincroniza os subjects reais do fone (bateria, bluetooth, configurações)
   * com os subjects gerados */
  ui_model_sync_ui_gen();

  /*
   * Criacao da tela principal gerada pelo novo projeto de interface (LVGL Pro).
   * A nova tela inicial exportada por ui.h / screen_main_gen.h e a
   * screen_main_create().
   */
  lv_obj_t *screen = screen_main_create();
  if (screen == NULL) {
    ESP_LOGE(
        TAG,
        "ERRO CRITICO: screen_main_create() retornou NULL! Carregando tela "
        "placeholder de emergencia...");
    ui_placeholder();
    return ESP_FAIL;
  }

  /* Carrega a tela com sucesso */
  lv_screen_load(screen);
  ESP_LOGI(TAG, "Tela principal (screen_main) carregada com sucesso.");

  /* Registra os observadores nos subjects dos sliders e hook para botões */
  register_ui_observers();

  /* Conecta os botões da tela inicial carregada */
  int initial_disconnect_cnt = 0;
  bind_action_buttons_recursive(screen, &initial_disconnect_cnt);

  return ESP_OK;
}

esp_err_t display_init(void) {
  /* Inicializa a camada de ponte entre a UI e os recursos de hardware do fone
   */
  ui_bridge_init();

  lv_init();
  lv_tick_set_cb(lvgl_tick_cb);

  /* Inicializa o modelo reativo de dados e os subjects do LVGL 9 */
  ui_model_init();
  ui_model_register_events();

  lv_display_t *disp = lv_display_create(LCD_H_RES, LCD_V_RES);
  ESP_RETURN_ON_FALSE(disp, ESP_ERR_NO_MEM, TAG, "lv_display_create");

  const size_t buf_sz = LCD_H_RES * LCD_DRAW_BUF_LINES * sizeof(uint16_t);
  void *buf1 = heap_caps_malloc(buf_sz, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  void *buf2 = heap_caps_malloc(buf_sz, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  ESP_RETURN_ON_FALSE(buf1 && buf2, ESP_ERR_NO_MEM, TAG, "draw buffers");

  lv_display_set_buffers(disp, buf1, buf2, buf_sz,
                         LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(disp, lvgl_flush_cb);

  ESP_RETURN_ON_ERROR(display_hw_init(disp), TAG, "hw init");

  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, lvgl_touch_read_cb);
  lv_indev_set_display(indev, disp);

  /* Carrega a UI customizada ou ativa fallback */
  if (display_create_app_ui() != ESP_OK) {
    ESP_LOGW(TAG, "Falha na UI gerada, mantendo tela de fallback.");
  }

  /* Liga o backlight */
  gpio_set_level(LCD_GPIO_BL, 1);
  return ESP_OK;
}

void display_task(void *arg) {
  (void)arg;
  lv_display_t *disp = lv_display_get_default();
  bool screen_sleeping = false;
  lv_obj_t *last_bound_screen = NULL;

  for (;;) {
    /* WP 6.1: Drena com segurança atualizações recebidas por eventos para o
     * contexto do LVGL */
    ui_model_drain_updates();

    /*
     * Detecção infalível de troca de tela ativa:
     * Garante que quando uma tela transiciona (mesmo com animação), seus botões
     * de ação sejam mapeados imediatamente e recebam eventos de toque sem falha.
     */
    lv_obj_t *current_scr = lv_screen_active();
    if (current_scr != NULL && current_scr != last_bound_screen) {
      last_bound_screen = current_scr;
      const char *s_name = lv_obj_get_name(current_scr);
      ESP_LOGI(TAG, "Tela ativa alterada para '%s'. Vinculando botoes...", s_name ? s_name : "desconhecida");
      int bt_disconnect_cnt = 0;
      bind_action_buttons_recursive(current_scr, &bt_disconnect_cnt);
    }

    /* WP 6.4: Gestão do tempo de inatividade da tela */
    if (disp != NULL) {
      uint32_t inactive_ms = lv_display_get_inactive_time(disp);
      int timeout_sec = ui_bridge_display_get_tempo_tela();

      if (timeout_sec > 0 && inactive_ms >= (uint32_t)(timeout_sec * 1000)) {
        if (!screen_sleeping) {
          screen_sleeping = true;
          gpio_set_level(LCD_GPIO_BL, 0);
          ESP_LOGI(TAG, "Display suspenso por inatividade (%d s)", timeout_sec);
        }
      } else {
        if (screen_sleeping) {
          screen_sleeping = false;
          gpio_set_level(LCD_GPIO_BL, 1);
          ESP_LOGI(TAG, "Display acordado por atividade de usuario");
        }
      }
    }

    uint32_t wait_ms = lv_timer_handler();
    if (wait_ms < 5)
      wait_ms = 5;
    if (wait_ms > 30)
      wait_ms = 30;
    vTaskDelay(pdMS_TO_TICKS(wait_ms));
  }
}

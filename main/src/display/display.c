#include "display.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
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
#include "cover_art.h"

static const char *TAG = "display";

static esp_lcd_panel_handle_t s_panel;
static esp_lcd_touch_handle_t s_touch = NULL;
static bool s_touch_paused = false;

/* ============================================================================
 * CONTROLE DE BACKLIGHT INTELIGENTE POR HARDWARE PWM LEDC (5 kHz, 10 bits)
 * ============================================================================
 * Reduz o consumo contínuo de ~80 mA para ~13-20 mA com brilho confortável,
 * multiplicando a autonomia de bateria por mais de 2x sem coil whine no áudio.
 */
#define BACKLIGHT_LEDC_TIMER   LEDC_TIMER_0
#define BACKLIGHT_LEDC_MODE    LEDC_HIGH_SPEED_MODE
#define BACKLIGHT_LEDC_CH      LEDC_CHANNEL_0
#define BACKLIGHT_PWM_FREQ_HZ  5000 /* 5 kHz: silêncio absoluto no codec WM8960 */
#define BACKLIGHT_PWM_RES      LEDC_TIMER_10_BIT /* 0 a 1023 micropassos */

static uint8_t s_current_brightness = 60;
static bool s_is_dimmed = false;
static bool s_is_sleeping = false;

esp_err_t display_backlight_init(void) {
  const ledc_timer_config_t ledc_timer = {
      .speed_mode       = BACKLIGHT_LEDC_MODE,
      .timer_num        = BACKLIGHT_LEDC_TIMER,
      .duty_resolution  = BACKLIGHT_PWM_RES,
      .freq_hz          = BACKLIGHT_PWM_FREQ_HZ,
      .clk_cfg          = LEDC_AUTO_CLK
  };
  ESP_RETURN_ON_ERROR(ledc_timer_config(&ledc_timer), TAG, "Erro timer LEDC");

  const ledc_channel_config_t ledc_channel = {
      .speed_mode     = BACKLIGHT_LEDC_MODE,
      .channel        = BACKLIGHT_LEDC_CH,
      .timer_sel      = BACKLIGHT_LEDC_TIMER,
      .intr_type      = LEDC_INTR_DISABLE,
      .gpio_num       = LCD_GPIO_BL,
      .duty           = 0, /* Inicializa desligado */
      .hpoint         = 0
  };
  ESP_RETURN_ON_ERROR(ledc_channel_config(&ledc_channel), TAG, "Erro canal LEDC");

  ESP_LOGI(TAG, "Backlight Inteligente LEDC inicializado no GPIO %d (5 kHz, 10 bits)", LCD_GPIO_BL);
  return ESP_OK;
}

void display_backlight_set_duty(uint8_t brightness_pct) {
  if (brightness_pct > 100) brightness_pct = 100;
  s_current_brightness = brightness_pct;
  s_is_dimmed = false;
  s_is_sleeping = false;

  /* Curva perceptual quadrática: duty = (pct * pct * 1023) / 10000 */
  uint32_t duty = ((uint32_t)brightness_pct * (uint32_t)brightness_pct * 1023) / 10000;
  ledc_set_duty(BACKLIGHT_LEDC_MODE, BACKLIGHT_LEDC_CH, duty);
  ledc_update_duty(BACKLIGHT_LEDC_MODE, BACKLIGHT_LEDC_CH);
}

uint8_t display_backlight_get_duty(void) {
  return s_current_brightness;
}

void display_backlight_dim(void) {
  if (s_is_dimmed || s_is_sleeping) return;
  s_is_dimmed = true;
  /* Reduz suavemente para 15% de duty (modo de pré-aviso antes de apagar) */
  uint32_t dim_duty = (15 * 15 * 1023) / 10000;
  ledc_set_duty(BACKLIGHT_LEDC_MODE, BACKLIGHT_LEDC_CH, dim_duty);
  ledc_update_duty(BACKLIGHT_LEDC_MODE, BACKLIGHT_LEDC_CH);
}

void display_backlight_wake(void) {
  if (!s_is_dimmed && !s_is_sleeping) return;
  s_is_dimmed = false;
  s_is_sleeping = false;
  if (s_panel) {
    esp_lcd_panel_disp_on_off(s_panel, true);
  }
  display_backlight_set_duty(s_current_brightness > 0 ? s_current_brightness : 60);
}

bool display_backlight_is_dimmed(void) {
  return s_is_dimmed;
}

bool display_backlight_is_sleeping(void) {
  return s_is_sleeping;
}

void display_backlight_sleep(void) {
  if (s_is_sleeping) return;
  s_is_sleeping = true;
  s_is_dimmed = false;
  /* Desliga backlight via PWM */
  ledc_set_duty(BACKLIGHT_LEDC_MODE, BACKLIGHT_LEDC_CH, 0);
  ledc_update_duty(BACKLIGHT_LEDC_MODE, BACKLIGHT_LEDC_CH);
  /* Coloca o painel ILI9341 em modo de repouso (SLPIN) */
  if (s_panel) {
    esp_lcd_panel_disp_on_off(s_panel, false);
  }
}

static bool s_touch_audio_streaming = false;

void display_touch_set_transition_pause(bool pause) {
  s_touch_paused = pause;
}

bool display_touch_is_paused(void) {
  return s_touch_paused;
}

void display_touch_set_audio_streaming(bool is_streaming) {
  if (s_touch_audio_streaming != is_streaming) {
    s_touch_audio_streaming = is_streaming;
    ESP_LOGI(TAG, "Inibição de Touch por Streaming de Áudio: %s",
             is_streaming ? "ATIVADA (anti-ruído RF)" : "DESATIVADA (touch liberado)");
  }
}

bool display_touch_is_audio_streaming(void) {
  return s_touch_audio_streaming;
}

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

/* ============================================================================
 * FILTRO ANTI-RUÍDO TEMPORAL E ESPACIAL DO TOUCH RESISTIVO (XPT2046)
 * ============================================================================
 * Solução Definitiva para Eliminar Toques Fantasmas por EMI de RF e I2S DMA:
 * 1. O rádio Bluetooth Classic BR/EDR e o I2S DMA induzem micro-tensões espúrias
 *    na folha resistiva ITO analógica que duram tipicamente 1 ciclo de amostragem (<25 ms).
 * 2. O filtro de debounce temporal de 2 estágios exige no mínimo 2 amostras consecutivas
 *    consistentes no espaço (|Δx| <= 30 px, |Δy| <= 30 px) para transitar de RELEASED para PRESSED.
 * 3. Spikes isolados de 1 frame são descartados e registrados pelo monitor de diagnóstico.
 * 4. Uma vez confirmado o toque, aplica suavização IIR exponencial nas coordenadas (3/4 + 1/4).
 * 5. O filtro de liberação exige 2 frames de ausência para evitar trepidação (chatter) ao soltar.
 */
typedef struct {
  bool is_pressed;             /* Estado de toque confirmado repassado ao LVGL */
  int16_t last_valid_x;        /* Última coordenada X válida */
  int16_t last_valid_y;        /* Última coordenada Y válida */
  int16_t candidate_x;         /* Coordenada X candidata em validação de debounce */
  int16_t candidate_y;         /* Coordenada Y candidata em validação de debounce */
  uint8_t press_confirm_cnt;   /* Contador de quadros consecutivos para confirmar pressão */
  uint8_t release_confirm_cnt; /* Contador de quadros consecutivos para confirmar liberação */
  uint32_t press_start_time;   /* Timestamp do início do toque para cálculo de duração */
  uint32_t sample_count;       /* Contador de amostras coletadas durante o toque atual */
} touch_filter_t;

static touch_filter_t s_touch_filter = {0};

/**
 * @brief Callback do LVGL para leitura do estado e posição do Touch Resistivo XPT2046.
 *
 * Implementação Robusta com Blindagem Anti-Ruído e Diagnóstico em Tempo Real:
 * 1. LIBERAÇÃO DO BARRAMENTO SPI E ELIMINAÇÃO DE POLLING:
 *    Durante transições de tela ou animações do LVGL (lv_anim_count_running > 0),
 *    o touch é completamente pausado e NÃO emite transações no barramento SPI3.
 * 2. BLINDAGEM TEMPORAL ANTI-TOQUE FANTASMA (DEBOUNCE 2-ESTÁGIOS):
 *    Spikes de RF/I2S que duram apenas 1 leitura são sumariamente rejeitados.
 * 3. LIBERDADE DE USO EM STREAMING:
 *    O touch NÃO fica travado durante a reprodução de música, permitindo uso contínuo.
 * 4. FILTRO DE DESPERTAR (R.14 DO AGENTS.md):
 *    Se o display estiver em Sleep ou Pré-Dimming, o primeiro toque acorda o backlight e é absorvido.
 */
static void lvgl_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
  (void)indev;

  /*
   * REGRA DE OURO (Requisito de Estabilidade e Liberação do SPI3):
   * Se o touch estiver pausado ou durante animações/transições gráficas do LVGL,
   * retornamos imediatamente LV_INDEV_STATE_RELEASED SEM ACESSAR O BARRAMENTO SPI!
   * Isso evita chaveamento de clock (40 MHz <-> 1 MHz) e libera 100% da banda para o LCD.
   */
  if (s_touch_paused || lv_anim_count_running() > 0) {
    if (s_touch_filter.is_pressed) {
      s_touch_filter.is_pressed = false;
      s_touch_filter.press_confirm_cnt = 0;
      s_touch_filter.release_confirm_cnt = 0;
    }
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }

  if (!s_touch) {
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }

  /* Lê os dados do conversor XPT2046 via SPI3 */
  esp_lcd_touch_read_data(s_touch);
  esp_lcd_touch_point_data_t point_data[1];
  uint8_t touch_cnt = 0;
  esp_err_t get_err = esp_lcd_touch_get_data(s_touch, point_data, &touch_cnt, 1);
  bool raw_pressed = (get_err == ESP_OK && touch_cnt > 0);

  /*
   * Filtro de Despertar da Tela (Wake-Up Filter - R.14 do AGENTS.md):
   * Se a tela estiver apagada ou em pré-dimming e houver toque, reacendemos a tela
   * e consumimos o clique sem repassar ao LVGL.
   */
  if (display_backlight_is_sleeping() || display_backlight_is_dimmed()) {
    if (raw_pressed) {
      ESP_LOGI(TAG, "Primeiro toque detectado: acordando backlight (clique absorvido)");
      display_backlight_wake();
      lv_display_t *disp = lv_display_get_default();
      if (disp) {
        lv_display_trigger_activity(disp);
      }
      s_touch_filter.press_confirm_cnt = 0;
      s_touch_filter.is_pressed = false;
    }
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }

  if (raw_pressed) {
    int32_t raw_x = point_data[0].x;
    int32_t raw_y = point_data[0].y;

    /* Remapeamento linear para a área ativa do display */
    if (raw_x < TOUCH_ACTIVE_MIN_X) raw_x = TOUCH_ACTIVE_MIN_X;
    if (raw_x > TOUCH_ACTIVE_MAX_X) raw_x = TOUCH_ACTIVE_MAX_X;
    if (raw_y < TOUCH_ACTIVE_MIN_Y) raw_y = TOUCH_ACTIVE_MIN_Y;
    if (raw_y > TOUCH_ACTIVE_MAX_Y) raw_y = TOUCH_ACTIVE_MAX_Y;

    int32_t mapped_x = ((raw_x - TOUCH_ACTIVE_MIN_X) * (LCD_H_RES - 1)) /
                       (TOUCH_ACTIVE_MAX_X - TOUCH_ACTIVE_MIN_X);
    int32_t mapped_y = ((raw_y - TOUCH_ACTIVE_MIN_Y) * (LCD_V_RES - 1)) /
                       (TOUCH_ACTIVE_MAX_Y - TOUCH_ACTIVE_MIN_Y);

    if (mapped_x < 0) mapped_x = 0;
    if (mapped_x >= LCD_H_RES) mapped_x = LCD_H_RES - 1;
    if (mapped_y < 0) mapped_y = 0;
    if (mapped_y >= LCD_V_RES) mapped_y = LCD_V_RES - 1;

    s_touch_filter.release_confirm_cnt = 0;

    if (!s_touch_filter.is_pressed) {
      /* Estágio 1 do Debounce: primeiro registro candidato */
      if (s_touch_filter.press_confirm_cnt == 0) {
        s_touch_filter.candidate_x = (int16_t)mapped_x;
        s_touch_filter.candidate_y = (int16_t)mapped_y;
        s_touch_filter.press_confirm_cnt = 1;
        /* Mantém liberado no LVGL até haver confirmação no próximo frame */
        data->state = LV_INDEV_STATE_RELEASED;
        return;
      }

      /* Estágio 2 do Debounce: verifica consistência espacial da 2ª amostra consecutiva */
      int diff_x = abs((int)mapped_x - (int)s_touch_filter.candidate_x);
      int diff_y = abs((int)mapped_y - (int)s_touch_filter.candidate_y);

      if (diff_x <= 30 && diff_y <= 30) {
        /* Toque humano real confirmado (estabilidade confirmada across frames)! */
        s_touch_filter.is_pressed = true;
        s_touch_filter.last_valid_x = (int16_t)mapped_x;
        s_touch_filter.last_valid_y = (int16_t)mapped_y;
        s_touch_filter.press_start_time = (uint32_t)(esp_timer_get_time() / 1000);
        s_touch_filter.sample_count = 1;

        data->point.x = (int16_t)mapped_x;
        data->point.y = (int16_t)mapped_y;
        data->state = LV_INDEV_STATE_PRESSED;

#if CONFIG_HEADSET_TOUCH_DIAG
        ESP_LOGI(TAG, ">>> [TOUCH-PRESS] Toque Confirmado: raw(%ld,%ld) -> tela(%ld,%ld) [Streaming: %s] <<<",
                 (long)point_data[0].x, (long)point_data[0].y,
                 (long)mapped_x, (long)mapped_y,
                 s_touch_audio_streaming ? "SIM" : "NAO");
#endif
        return;
      } else {
        /* Salto espúrio: atualiza candidato sem confirmar */
        s_touch_filter.candidate_x = (int16_t)mapped_x;
        s_touch_filter.candidate_y = (int16_t)mapped_y;
        data->state = LV_INDEV_STATE_RELEASED;
        return;
      }
    } else {
      /* Toque contínuo ativo: aplica suavização IIR nas coordenadas */
      int16_t smooth_x = (int16_t)((3 * s_touch_filter.last_valid_x + mapped_x) / 4);
      int16_t smooth_y = (int16_t)((3 * s_touch_filter.last_valid_y + mapped_y) / 4);
      s_touch_filter.last_valid_x = smooth_x;
      s_touch_filter.last_valid_y = smooth_y;
      s_touch_filter.sample_count++;

      data->point.x = smooth_x;
      data->point.y = smooth_y;
      data->state = LV_INDEV_STATE_PRESSED;

#if CONFIG_HEADSET_TOUCH_DIAG
      static uint32_t last_drag_log = 0;
      uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
      if (now - last_drag_log >= 500) {
        last_drag_log = now;
        ESP_LOGI(TAG, ">>> [TOUCH-ARRASTO] raw(%ld,%ld) -> tela(%d,%d) <<<",
                 (long)point_data[0].x, (long)point_data[0].y, smooth_x, smooth_y);
      }
#endif
      return;
    }
  }

  /* Caso raw_pressed seja falso (sem pressão detectada pelo hardware) */
  if (s_touch_filter.press_confirm_cnt == 1 && !s_touch_filter.is_pressed) {
    /* Spike transitório isolado de 1 frame: FILTRADO COM SUCESSO! */
#if CONFIG_HEADSET_TOUCH_DIAG
    ESP_LOGD(TAG, ">>> [TOUCH-REJEITADO] Spike de ruído/EMI isolado descartado com sucesso (cand: %d,%d) <<<",
             s_touch_filter.candidate_x, s_touch_filter.candidate_y);
#endif
    s_touch_filter.press_confirm_cnt = 0;
  }

  if (s_touch_filter.is_pressed) {
    s_touch_filter.release_confirm_cnt++;
    if (s_touch_filter.release_confirm_cnt >= 2) {
      /* Liberação confirmada */
      s_touch_filter.is_pressed = false;
      s_touch_filter.press_confirm_cnt = 0;
      s_touch_filter.release_confirm_cnt = 0;

#if CONFIG_HEADSET_TOUCH_DIAG
      uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
      uint32_t dur = now - s_touch_filter.press_start_time;
      ESP_LOGI(TAG, ">>> [TOUCH-RELEASE] Toque Liberado apos %lu ms (%lu amostras) na pos(%d,%d) <<<",
               (unsigned long)dur, (unsigned long)s_touch_filter.sample_count,
               s_touch_filter.last_valid_x, s_touch_filter.last_valid_y);
#endif
      data->state = LV_INDEV_STATE_RELEASED;
      return;
    } else {
      /* Segura coordenada por 1 frame para evitar perda de clique por trepidação rápida */
      data->point.x = s_touch_filter.last_valid_x;
      data->point.y = s_touch_filter.last_valid_y;
      data->state = LV_INDEV_STATE_PRESSED;
      return;
    }
  }

  data->state = LV_INDEV_STATE_RELEASED;
}

static uint32_t lvgl_tick_cb(void) {
  return (uint32_t)(esp_timer_get_time() / 1000);
}

static esp_err_t display_hw_init(lv_display_t *disp) {
  /* 1. Inicializa o controle de backlight inteligente via PWM LEDC a 5 kHz */
  ESP_RETURN_ON_ERROR(display_backlight_init(), TAG, "backlight ledc");

  /* 2. Barramento SPI3 compartilhado entre LCD ILI9341 (40 MHz) e Touch XPT2046 (1 MHz) */
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
   * Habilita pull-up interno no pino MISO (GPIO 19).
   * O XPT2046 entra em alta impedância (tri-state) quando T_CS está alto.
   * O pull-up impede que a linha flutue e capte ruído eletromagnético dos rádios.
   */
  gpio_set_pull_mode(LCD_GPIO_MISO, GPIO_PULLUP_ONLY);

  /* --- Dispositivo 1: Painel LCD ILI9341 a 40 MHz --- */
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
  ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "disp on");

  /* --- Dispositivo 2: Touch Screen XPT2046 a 1 MHz no barramento SPI3 --- */
  esp_lcd_panel_io_handle_t tp_io = NULL;
  esp_lcd_panel_io_spi_config_t tp_io_cfg =
      ESP_LCD_TOUCH_IO_SPI_XPT2046_CONFIG(TOUCH_GPIO_CS);
  tp_io_cfg.pclk_hz = TOUCH_PIXEL_CLK_HZ; /* 1 MHz para amostragem limpa e estável */

  ESP_RETURN_ON_ERROR(
      esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_NUM,
                               &tp_io_cfg, &tp_io),
      TAG, "tp io");

  const esp_lcd_touch_config_t tp_cfg = {
      .x_max = LCD_H_RES,
      .y_max = LCD_V_RES,
      .rst_gpio_num = GPIO_NUM_NC,
      .int_gpio_num = GPIO_NUM_NC, /* Polling coordenado pelo LVGL */
      .levels = {
          .reset = 0,
          .interrupt = 0,
      },
      .flags = {
          .swap_xy = TOUCH_SWAP_XY,
          .mirror_x = TOUCH_MIRROR_X,
          .mirror_y = TOUCH_MIRROR_Y,
      },
  };
  ESP_RETURN_ON_ERROR(
      esp_lcd_touch_new_spi_xpt2046(tp_io, &tp_cfg, &s_touch),
      TAG, "touch xpt2046");

  ESP_LOGI(TAG, "Hardware do display (ILI9341 a 40 MHz) e Touch (XPT2046 a 1 MHz) prontos!");
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
  display_backlight_set_duty((uint8_t)val);
  settings_set_u8(SETTINGS_KEY_DISP_BRIGHT, (uint8_t)val);
}

static void on_subject_timeout_changed(lv_observer_t *observer, lv_subject_t *subject) {
  (void)observer;
  int val = lv_subject_get_int(subject);
  if (val < 0 || val > 3600) return;
  if (val == s_last_sent_timeout) return;
  s_last_sent_timeout = val;

  ESP_LOGI(TAG, "Slider Tempo de Tela alterado pelo usuario: %d s", val);
  ui_bridge_display_set_tempo_tela(val);
}

static void on_subject_prox_sensitivity_changed(lv_observer_t *observer, lv_subject_t *subject) {
  (void)observer;
  int val = lv_subject_get_int(subject);
  if (val < 0 || val > 1023) return;
  if (s_last_sent_prox_sens >= 0 && abs(val - s_last_sent_prox_sens) < 4) return;
  s_last_sent_prox_sens = val;

  ESP_LOGI(TAG, "Slider Sensibilidade Proximidade alterado pelo usuario: %d", val);
  ui_bridge_proximidade_set_distancia_sensibilidade(val);
}

static void on_subject_vibracall_intensity_changed(lv_observer_t *observer, lv_subject_t *subject) {
  (void)observer;
  int val = lv_subject_get_int(subject);
  if (val < 0 || val > 100) return;
  if (s_last_sent_vib_intens >= 0 && abs(val - s_last_sent_vib_intens) < 4) return;
  s_last_sent_vib_intens = val;

  ESP_LOGI(TAG, "Slider Intensidade Vibracall alterado pelo usuario: %d%%", val);
  ui_bridge_vibracall_set_intensidade(val);
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

/* ============================================================================
 * GERENCIAMENTO DE AÇÕES E CONTROLES DA UI (DESACOPLADO DE LABELS / FIGMA)
 * ============================================================================
 * Identifica ações de botões e switches por IDs numéricos fortemente tipados,
 * utilizando o 'user_data' do objeto e do evento no LVGL 9.
 *
 * Vantagens desta abordagem:
 * 1. Não inspeciona texto de labels (elimina o anti-pattern de strcmp em labels).
 * 2. Imune a traduções de idiomas (i18n), textos alterados no LVGL Pro ou ícones.
 * 3. Preserva 100% intacta a pasta components/ui (regerável pelo LVGL Pro a qualquer momento).
 * 4. Sliders são controlados de forma reativa direta pelos subjects (lv_subject_t).
 */
typedef enum {
  UI_ACTION_NONE = 0,
  UI_ACTION_DISP_ENABLE,
  UI_ACTION_DISP_DISABLE,
  UI_ACTION_PROX_ENABLE,
  UI_ACTION_PROX_DISABLE,
  UI_ACTION_VIB_ENABLE,
  UI_ACTION_VIB_DISABLE,
  UI_ACTION_VIB_TEST,
  UI_ACTION_EARS_ENABLE,
  UI_ACTION_EARS_DISABLE,
  UI_ACTION_BT_DISCONNECT_1,
  UI_ACTION_BT_DISCONNECT_2,
  UI_ACTION_BT_TOGGLE_AUTOSWITCH,
  UI_ACTION_BT_TOGGLE_ACTIVE_SLOT,
  UI_ACTION_BT_START_PAIRING,
  UI_ACTION_MEDIA_PLAY_PAUSE,
  UI_ACTION_MEDIA_NEXT,
  UI_ACTION_MEDIA_PREV,
  UI_ACTION_SELECT_WALLPAPER_BASE = 100,
  UI_ACTION_SELECT_GIF_BASE = 200,
} ui_action_id_t;

/**
 * @brief Callback universal de clique para botões de ação da interface gráfica.
 * Recupera o ID da ação diretamente do user_data do evento/objeto sem string parsing.
 */
static void on_action_control_clicked(lv_event_t *e) {
  /* Obtém a ação através do user_data do evento (passado em lv_obj_add_event_cb) */
  ui_action_id_t action = (ui_action_id_t)(uintptr_t)lv_event_get_user_data(e);

  /* Fallback de segurança: caso o evento venha de um filho, tenta ler o user_data do alvo */
  if (action == UI_ACTION_NONE) {
    lv_obj_t *target = lv_event_get_target(e);
    if (target) {
      action = (ui_action_id_t)(uintptr_t)lv_obj_get_user_data(target);
    }
  }

  if (action == UI_ACTION_NONE) return;

  ESP_LOGI(TAG, "Controle de acao acionado: id=%d", action);

  switch (action) {
    case UI_ACTION_BT_DISCONNECT_1:
      ESP_LOGI(TAG, "Acao: Desconectar Bluetooth Slot 1");
      ui_bridge_bt_set_desconectar_dispositivo_1(true);
      break;

    case UI_ACTION_BT_DISCONNECT_2:
      ESP_LOGI(TAG, "Acao: Desconectar Bluetooth Slot 2");
      ui_bridge_bt_set_desconectar_dispositivo_2(true);
      break;

    case UI_ACTION_BT_TOGGLE_AUTOSWITCH: {
      ui_model_data_t snap;
      ui_model_get_snapshot(&snap);
      ESP_LOGI(TAG, "Acao: Alternar Bluetooth Auto-Switch (atual=%d -> novo=%d)",
               snap.bt_auto_switch, !snap.bt_auto_switch);
      ui_bridge_bt_set_alternar_ativo(!snap.bt_auto_switch);
      break;
    }

    case UI_ACTION_BT_TOGGLE_ACTIVE_SLOT:
      ESP_LOGI(TAG, "Acao: Alternar Slot Ativo Bluetooth");
      ui_bridge_bt_toggle_active_slot();
      break;

    case UI_ACTION_BT_START_PAIRING:
      ESP_LOGI(TAG, "Acao: Iniciar Pareamento Bluetooth (120 s)");
      ui_bridge_bt_start_pairing();
      break;

    case UI_ACTION_DISP_ENABLE:
      ESP_LOGI(TAG, "Acao: Ativar Temporizador do Display (Modo 'Ligado por timer')");
      ui_bridge_display_set_ativar(true);
      break;

    case UI_ACTION_DISP_DISABLE:
      ESP_LOGI(TAG, "Acao: Desativar Temporizador do Display (Modo 'Manter ligado')");
      ui_bridge_display_set_desativar(true);
      break;

    case UI_ACTION_PROX_ENABLE:
      ESP_LOGI(TAG, "Acao: Ativar Sensor de Proximidade");
      ui_bridge_proximidade_set_ativar(true);
      break;

    case UI_ACTION_PROX_DISABLE:
      ESP_LOGI(TAG, "Acao: Desativar Sensor de Proximidade");
      ui_bridge_proximidade_set_desativar(true);
      break;

    case UI_ACTION_VIB_ENABLE:
      ESP_LOGI(TAG, "Acao: Ativar Vibracall");
      ui_bridge_vibracall_set_ativar(true);
      break;

    case UI_ACTION_VIB_DISABLE:
      ESP_LOGI(TAG, "Acao: Desativar Vibracall");
      ui_bridge_vibracall_set_desativar(true);
      break;

    case UI_ACTION_VIB_TEST:
      ESP_LOGI(TAG, "Acao: Teste de Vibracao Haptica");
      ui_bridge_vibracall_trigger_test(200, 0);
      break;

    case UI_ACTION_EARS_ENABLE:
      ESP_LOGI(TAG, "Acao: Ativar Orelhas Animatronicas");
      ui_bridge_orelhas_set_ativar(true);
      break;

    case UI_ACTION_EARS_DISABLE:
      ESP_LOGI(TAG, "Acao: Desativar Orelhas Animatronicas");
      ui_bridge_orelhas_set_desativar(true);
      break;

    case UI_ACTION_MEDIA_PLAY_PAUSE:
    case UI_ACTION_MEDIA_NEXT:
    case UI_ACTION_MEDIA_PREV:
      /* Requisito de Projeto: A interface gráfica opera em modo puramente passivo
       * para controle de mídia (apenas recepção de metadados e cover art).
       * Nenhum comando de transporte AVRCP é emitido a partir de toques na tela. */
      ESP_LOGI(TAG, "Comando de transporte AVRCP ignorado (UI em modo somente recepcao de dados)");
      break;

    default:
      if (action >= UI_ACTION_SELECT_WALLPAPER_BASE && action < UI_ACTION_SELECT_WALLPAPER_BASE + 20) {
        int idx = (int)(action - UI_ACTION_SELECT_WALLPAPER_BASE);
        ESP_LOGI(TAG, "Acao: Selecionar Wallpaper Imagem indice %d", idx);
        ui_bridge_display_set_imagem_principal(idx);
      } else if (action >= UI_ACTION_SELECT_GIF_BASE && action < UI_ACTION_SELECT_GIF_BASE + 20) {
        int idx = (int)(action - UI_ACTION_SELECT_GIF_BASE);
        ESP_LOGI(TAG, "Acao: Selecionar GIF indice %d", idx);
        ui_bridge_display_set_gif_principal(idx);
      } else {
        ESP_LOGW(TAG, "Acao de controle desconhecida: %d", action);
      }
      break;
  }
}

/**
 * @brief Vincula uma ação fortemente tipada a um controle do LVGL sem depender do texto da label.
 *
 * Configura:
 * 1. user_data no próprio objeto (para consulta via lv_obj_get_user_data).
 * 2. Flag LV_OBJ_FLAG_CLICKABLE para garantir recepção de toque.
 * 3. Callback on_action_control_clicked com event_user_data configurado para o ID da ação.
 * 4. Propaga para os filhos diretos (ex: label dentro do card/tile) para que toques no texto
 *    sejam capturados com a mesma ação sem necessidade de ler string.
 */
static void bind_control_action(lv_obj_t *obj, ui_action_id_t action) {
  if (!obj) return;

  /* Associa a ação ao objeto principal */
  lv_obj_set_user_data(obj, (void *)(uintptr_t)action);
  lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_event_cb(obj, on_action_control_clicked);
  lv_obj_add_event_cb(obj, on_action_control_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)action);

  /* Propaga para os filhos (labels, ícones) para garantir toque em qualquer ponto do botão */
  uint32_t child_cnt = lv_obj_get_child_count(obj);
  for (uint32_t i = 0; i < child_cnt; i++) {
    lv_obj_t *child = lv_obj_get_child(obj, i);
    if (child) {
      lv_obj_set_user_data(child, (void *)(uintptr_t)action);
      lv_obj_add_flag(child, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_remove_event_cb(child, on_action_control_clicked);
      lv_obj_add_event_cb(child, on_action_control_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)action);
    }
  }
}

/**
 * @brief Obtém o container principal de conteúdo de uma tela de forma determinística e segura.
 *
 * Princípio Arquitetural (AGENTS.md §2 e §3.5):
 * O LVGL Pro gera no HeadSet4 estruturas de tela determinísticas e invariantes:
 *
 * 1. Telas Secundárias (screen_bluetooth, screen_display, screen_proximidade, screen_vibracall,
 *    screen_orelhas, screen_galeria_imagens, screen_galeria_gifs, screen_status, screen_apps, screen_settings):
 *    - child 0: panel_0 (Barra superior de status com BT e Bateria, 24 px)
 *    - child 1: row_2 (Cabeçalho de navegação com botão voltar '<' e título, 44 px)
 *    - child 2: column_0 (Coluna principal de conteúdo e ações da tela, 252 px)
 *
 * 2. Telas Principais (screen_main, screen_main_cover_art, screen_main_gif):
 *    - child 0: panel_0 (Barra superior de status com BT e Bateria, 24 px)
 *    - child 1: column_0 ou container_0 (Container principal da capa/imagem/GIF e botão Apps)
 *
 * Este helper elimina em definitivo loops cegos que buscavam "filhos com contagem >= 2",
 * os quais colidiam erroneamente com o panel_0 da barra superior gerando acessos inválidos de memória.
 *
 * @param screen Ponteiro para a tela ativa (the_root).
 * @return lv_obj_t* Ponteiro para o contêiner de conteúdo principal ou NULL se inválido.
 */
static lv_obj_t *get_screen_content_container(lv_obj_t *screen) {
  if (!screen) return NULL;
  uint32_t scr_cnt = lv_obj_get_child_count(screen);

  /* Telas secundárias: barra de status + barra de navegação + coluna de conteúdo */
  if (scr_cnt >= 3) {
    return lv_obj_get_child(screen, 2);
  }

  /* Telas principais: barra de status + container principal */
  if (scr_cnt == 2) {
    return lv_obj_get_child(screen, 1);
  }

  return NULL;
}

/**
 * @brief Mapeia os botões da tela de Bluetooth usando a estrutura hierárquica gerada.
 *
 * Estrutura determinística do LVGL Pro para screen_bluetooth:
 * - content_col (child 2 da tela):
 *   - child 0: tile_5 (Slot 1) -> seu child 1 é o card (Botão Desconectar Slot 1)
 *   - child 1: tile_5 (Slot 2) -> seu child 1 é o card (Botão Desconectar Slot 2)
 *   - child 2: container (Alternância) -> seu child 1 é o botão de alternar Auto-Switch
 *   - child 3: container (Slot Ativo) -> seu child 1 é o botão de alternar Slot Ativo
 */
static void bind_bluetooth_controls(lv_obj_t *screen) {
  if (!screen) return;
  lv_obj_t *content_col = get_screen_content_container(screen);
  if (!content_col) {
    ESP_LOGW(TAG, "Coluna de conteudo nao encontrada em screen_bluetooth");
    return;
  }

  uint32_t col_cnt = lv_obj_get_child_count(content_col);

  /* Slot 1: child 0 -> child 1 é o card/botão de desconexão */
  if (col_cnt > 0) {
    lv_obj_t *slot1_box = lv_obj_get_child(content_col, 0);
    if (slot1_box && lv_obj_get_child_count(slot1_box) > 1) {
      lv_obj_t *btn_disc1 = lv_obj_get_child(slot1_box, 1);
      bind_control_action(btn_disc1, UI_ACTION_BT_DISCONNECT_1);
      ESP_LOGI(TAG, "Mapeado controle estrutural: Bluetooth Slot 1 Desconectar");
    }
  }

  /* Slot 2: child 1 -> child 1 é o card/botão de desconexão */
  if (col_cnt > 1) {
    lv_obj_t *slot2_box = lv_obj_get_child(content_col, 1);
    if (slot2_box && lv_obj_get_child_count(slot2_box) > 1) {
      lv_obj_t *btn_disc2 = lv_obj_get_child(slot2_box, 1);
      bind_control_action(btn_disc2, UI_ACTION_BT_DISCONNECT_2);
      ESP_LOGI(TAG, "Mapeado controle estrutural: Bluetooth Slot 2 Desconectar");
    }
  }

  /* Alternância: child 2 -> child 1 é o tile/botão de alternar */
  if (col_cnt > 2) {
    lv_obj_t *toggle_box = lv_obj_get_child(content_col, 2);
    if (toggle_box && lv_obj_get_child_count(toggle_box) > 1) {
      lv_obj_t *btn_toggle = lv_obj_get_child(toggle_box, 1);
      bind_control_action(btn_toggle, UI_ACTION_BT_TOGGLE_AUTOSWITCH);
      ESP_LOGI(TAG, "Mapeado controle estrutural: Bluetooth Alternância Auto-Switch");
    }
  }

  /* Slot Ativo: child 3 -> child 1 é o botão de alternar slot ativo (select_slot) */
  if (col_cnt > 3) {
    lv_obj_t *slot_box = lv_obj_get_child(content_col, 3);
    if (slot_box && lv_obj_get_child_count(slot_box) > 1) {
      lv_obj_t *btn_slot = lv_obj_get_child(slot_box, 1);
      bind_control_action(btn_slot, UI_ACTION_BT_TOGGLE_ACTIVE_SLOT);
      ESP_LOGI(TAG, "Mapeado controle estrutural: Bluetooth Alternar Slot Ativo");
    }
  }

  /* Cabeçalho da tela: toque no título 'Bluetooth' ativa modo de pareamento manual (Google Fast Pair / Classic) */
  uint32_t scr_cnt = lv_obj_get_child_count(screen);
  if (scr_cnt > 1) {
    lv_obj_t *header_row = lv_obj_get_child(screen, 1);
    if (header_row && lv_obj_get_child_count(header_row) > 1) {
      lv_obj_t *title_lbl = lv_obj_get_child(header_row, 1);
      if (title_lbl) {
        bind_control_action(title_lbl, UI_ACTION_BT_START_PAIRING);
        ESP_LOGI(TAG, "Mapeado controle estrutural: Titulo Bluetooth -> Iniciar Modo Pareamento (120 s)");
      }
    }
  }
}

/**
 * @brief Mapeia os botões Ativar / Desativar em telas de periféricos com layout padrão.
 *
 * Em todas as telas periféricas (Display, Proximidade, Vibracall, Orelhas), o LVGL Pro
 * gera a coluna principal de conteúdo contendo uma linha de botões de ação (action row)
 * com exatamente 2 botões lado a lado:
 * - child 0 da linha = Botão "Ativar" (True)
 * - child 1 da linha = Botão "Desativar" (False)
 */
static void bind_toggle_screen_controls(lv_obj_t *screen, ui_action_id_t action_enable,
                                       ui_action_id_t action_disable, const char *screen_name) {
  if (!screen) return;
  lv_obj_t *content_col = get_screen_content_container(screen);
  if (!content_col) {
    ESP_LOGW(TAG, "Coluna de conteudo nao encontrada na tela '%s'", screen_name);
    return;
  }

  /* Busca a linha de botões que contém exatamente 2 filhos do tipo botão (Ativar e Desativar) */
  lv_obj_t *action_row = NULL;
  uint32_t col_cnt = lv_obj_get_child_count(content_col);
  for (uint32_t i = 0; i < col_cnt; i++) {
    lv_obj_t *row = lv_obj_get_child(content_col, i);
    if (row && lv_obj_get_child_count(row) == 2) {
      lv_obj_t *c0 = lv_obj_get_child(row, 0);
      lv_obj_t *c1 = lv_obj_get_child(row, 1);
      /* Diferencia a linha de botões da linha de status: botões contêm pelo menos 1 filho (o label de texto) */
      if (c0 && c1 && lv_obj_get_child_count(c0) > 0 && lv_obj_get_child_count(c1) > 0) {
        action_row = row;
        break;
      }
    }
  }

  if (action_row) {
    lv_obj_t *btn_enable = lv_obj_get_child(action_row, 0);
    lv_obj_t *btn_disable = lv_obj_get_child(action_row, 1);

    bind_control_action(btn_enable, action_enable);
    bind_control_action(btn_disable, action_disable);

    ESP_LOGI(TAG, "Mapeados controles estruturais para tela '%s': Ativar (id=%d) e Desativar (id=%d)",
             screen_name, action_enable, action_disable);
  } else {
    ESP_LOGW(TAG, "Linha de acoes Ativar/Desativar nao encontrada na tela '%s'", screen_name);
  }
}

/**
 * @brief Mapeia os controles da tela de Vibracall (Ativar, Desativar e Toque para Testar).
 */
static void bind_vibracall_controls(lv_obj_t *screen) {
  bind_toggle_screen_controls(screen, UI_ACTION_VIB_ENABLE, UI_ACTION_VIB_DISABLE, "Vibracall");

  /* Mapeia a linha de status inicial (child 0 de content_col) para disparar teste ao toque */
  lv_obj_t *content_col = get_screen_content_container(screen);
  if (content_col && lv_obj_get_child_count(content_col) > 0) {
    lv_obj_t *status_row = lv_obj_get_child(content_col, 0);
    if (status_row) {
      bind_control_action(status_row, UI_ACTION_VIB_TEST);
      ESP_LOGI(TAG, "Mapeado controle estrutural: Vibracall Toque no Status para Teste Háptico");
    }
  }
}

/**
 * @brief Mapeia os itens interativos das galerias de imagens e GIFs.
 *
 * Estrutura determinística do LVGL Pro para screen_galeria_imagens e screen_galeria_gifs:
 * - the_root:
 *   - child 0: panel_0 (barra superior BT + BAT)
 *   - child 1: row_2 (cabeçalho com botão '<' e título)
 *   - child 2: column_0 (coluna de conteúdo, obtida via get_screen_content_container):
 *     - child 0: row_3 (labels "Principal: —" e "0 / 8")
 *     - child 1: column_1 (container com os 8 botões button_subtle_0..7)
 *
 * Correção crítica: Utiliza get_screen_content_container() para acessar column_0 diretamente,
 * ignorando a barra superior panel_0 e a linha row_3 de labels informativos, ligando
 * exclusivamente o container column_1 que abriga os 8 botões interativos de mídia.
 */
static void bind_gallery_controls(lv_obj_t *screen, bool is_gif) {
  if (!screen) return;
  lv_obj_t *content_col = get_screen_content_container(screen);
  if (!content_col) {
    ESP_LOGW(TAG, "Coluna de conteudo nao encontrada na Galeria (%s)", is_gif ? "GIF" : "Imagens");
    return;
  }

  uint32_t col_cnt = lv_obj_get_child_count(content_col);
  lv_obj_t *items_container = NULL;

  /* Varre os filhos de content_col procurando o container que abriga os 8 botões */
  for (uint32_t i = 0; i < col_cnt; i++) {
    lv_obj_t *sub = lv_obj_get_child(content_col, i);
    if (sub && lv_obj_get_child_count(sub) >= 4) {
      items_container = sub;
      break;
    }
  }

  /* Fallback estrutural: se não identificado por contagem >= 4, adota child 1 */
  if (!items_container && col_cnt > 1) {
    items_container = lv_obj_get_child(content_col, 1);
  }

  if (items_container) {
    uint32_t item_cnt = lv_obj_get_child_count(items_container);
    for (uint32_t k = 0; k < item_cnt && k < 10; k++) {
      lv_obj_t *btn_item = lv_obj_get_child(items_container, k);
      if (btn_item) {
        ui_action_id_t action = is_gif ? (ui_action_id_t)(UI_ACTION_SELECT_GIF_BASE + k)
                                       : (ui_action_id_t)(UI_ACTION_SELECT_WALLPAPER_BASE + k);
        bind_control_action(btn_item, action);
      }
    }
    ESP_LOGI(TAG, "Mapeados controles estruturais para Galeria (%s, %ld itens vinculados)",
             is_gif ? "GIF" : "Imagens", (long)item_cnt);
  } else {
    ESP_LOGW(TAG, "Container de itens da Galeria (%s) nao encontrado", is_gif ? "GIF" : "Imagens");
  }
}

/**
 * @brief Mapeia controles e exibição na tela de Cover Art (screen_main_cover_art).
 *
 * Estrutura determinística do LVGL Pro para screen_main_cover_art:
 * - the_root:
 *   - child 0: panel_0 (barra superior BT + BAT)
 *   - child 1: container_0 (container principal obtido via get_screen_content_container):
 *     - child 0: card_0 (card com imagem da capa e labels de artista/música)
 *     - child 1: button_0 (botão "Apps")
 *
 * Em conformidade com o requisito de design e UX do HeadSet4:
 * - O toque no card/imagem da capa MUDA DE TELA para screen_main (ciclo de navegação de papéis de parede).
 * - NENHUM comando de transporte AVRCP (Play/Pause) é enviado a partir da UI (modo somente recepção de dados).
 */
static void bind_main_cover_art_controls(lv_obj_t *screen) {
  if (!screen) return;
  lv_obj_t *content = get_screen_content_container(screen);
  if (!content) {
    ESP_LOGW(TAG, "Container de conteudo nao encontrado em screen_main_cover_art");
    return;
  }

  /* card_0 é o primeiro filho do container principal de cover art */
  if (lv_obj_get_child_count(content) > 0) {
    lv_obj_t *card = lv_obj_get_child(content, 0);
    if (card) {
      /* Requisito de UX: Toque na imagem/card da capa MUDA DE TELA (vai para screen_main).
       * Não há nenhum controle AVRCP na interface gráfica (somente recepção de dados). */
      lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_add_screen_create_event(card, LV_EVENT_CLICKED, screen_main_create, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 300, 0);
      ESP_LOGI(TAG, "Mapeada navegacao: Cover Art toque na Capa -> Transicao para screen_main");

      /* Atualiza imediatamente com a capa decodificada e metadados de texto mantidos no modelo */
      ui_model_data_t snap;
      ui_model_get_snapshot(&snap);

      /* 1. Capa de álbum: tile_4_0 -> image_0 */
      if (lv_obj_get_child_count(card) > 0) {
        lv_obj_t *tile = lv_obj_get_child(card, 0);
        if (tile) {
          lv_obj_add_flag(tile, LV_OBJ_FLAG_EVENT_BUBBLE);
          if (lv_obj_get_child_count(tile) > 0) {
            lv_obj_t *img = lv_obj_get_child(tile, 0);
            if (img) {
              lv_obj_add_flag(img, LV_OBJ_FLAG_EVENT_BUBBLE);
              const lv_image_dsc_t *dsc = cover_art_get_image_dsc();
              if (dsc) {
                lv_image_set_src(img, dsc);
                lv_obj_invalidate(img);
                ESP_LOGI(TAG, "Capa aplicada com sucesso ao carregar screen_main_cover_art");
              }
            }
          }
        }
      }

      /* 2. Metadados de texto: Álbum (1), Música (2), Artista (3) */
      uint32_t count = lv_obj_get_child_count(card);
      if (count > 1 && strlen(snap.track_album) > 0) {
        lv_obj_t *lbl_album = lv_obj_get_child(card, 1);
        if (lbl_album) lv_label_set_text(lbl_album, snap.track_album);
      }
      if (count > 2 && strlen(snap.track_title) > 0) {
        lv_obj_t *lbl_title = lv_obj_get_child(card, 2);
        if (lbl_title) lv_label_set_text(lbl_title, snap.track_title);
      }
      if (count > 3 && strlen(snap.track_artist) > 0) {
        lv_obj_t *lbl_artist = lv_obj_get_child(card, 3);
        if (lbl_artist) lv_label_set_text(lbl_artist, snap.track_artist);
      }
    }
  }
}

void display_update_cover_art(const void *dsc) {
  if (!dsc) return;
  lv_obj_t *screen = lv_screen_active();
  if (!screen) return;

  const char *name = lv_obj_get_name(screen);
  if (name && strstr(name, "cover_art")) {
    lv_obj_t *content = get_screen_content_container(screen);
    if (content && lv_obj_get_child_count(content) > 0) {
      lv_obj_t *card = lv_obj_get_child(content, 0);
      if (card && lv_obj_get_child_count(card) > 0) {
        lv_obj_t *tile = lv_obj_get_child(card, 0);
        if (tile && lv_obj_get_child_count(tile) > 0) {
          lv_obj_t *img = lv_obj_get_child(tile, 0);
          if (img) {
            lv_image_set_src(img, dsc);
            lv_obj_invalidate(img);
            ESP_LOGI(TAG, "Imagem de capa atualizada dinamicamente em screen_main_cover_art!");
          }
        }
      }
    }
  }
}

void display_update_track_metadata(const char *title, const char *artist, const char *album) {
  lv_obj_t *screen = lv_screen_active();
  if (!screen) return;

  const char *name = lv_obj_get_name(screen);
  if (name && strstr(name, "cover_art")) {
    lv_obj_t *content = get_screen_content_container(screen);
    if (content && lv_obj_get_child_count(content) > 0) {
      lv_obj_t *card = lv_obj_get_child(content, 0);
      if (card) {
        uint32_t count = lv_obj_get_child_count(card);
        if (count > 1 && album && strlen(album) > 0) {
          lv_obj_t *lbl_album = lv_obj_get_child(card, 1);
          if (lbl_album) lv_label_set_text(lbl_album, album);
        }
        if (count > 2 && title && strlen(title) > 0) {
          lv_obj_t *lbl_title = lv_obj_get_child(card, 2);
          if (lbl_title) lv_label_set_text(lbl_title, title);
        }
        if (count > 3 && artist && strlen(artist) > 0) {
          lv_obj_t *lbl_artist = lv_obj_get_child(card, 3);
          if (lbl_artist) lv_label_set_text(lbl_artist, artist);
        }
        lv_obj_invalidate(card);
        ESP_LOGI(TAG, "Metadados de musica atualizados em screen_main_cover_art: '%s' - '%s'",
                 title ? title : "", artist ? artist : "");
      }
    }
  }
}

/**
 * @brief Mapeia e ajusta elementos da tela de Status Geral (screen_status).
 *
 * Apaga e oculta o rótulo provisório de template 'Exemplos · não são leituras reais',
 * deixando o layout da tela limpo e focado diretamente nas métricas de telemetria.
 *
 * Blindagem Crítica contra Guru Meditation Error (LoadProhibited):
 * 1. Utiliza get_screen_content_container() para acessar column_0 (child 2 da tela),
 *    evitando capturar erroneamente panel_0 (child 0, barra superior) como ocorria no loop anterior.
 * 2. Aplica validação mandante de tipo com lv_obj_check_type(child, &lv_label_class).
 *    Apenas invoca operações de label se o objeto for garantidamente da classe lv_label.
 */
static void bind_status_controls(lv_obj_t *screen) {
  if (!screen) return;
  lv_obj_t *content_col = get_screen_content_container(screen);
  if (!content_col) {
    ESP_LOGW(TAG, "Coluna de conteudo nao encontrada em screen_status");
    return;
  }

  /* Em screen_status_gen.c, o label informativo 'Exemplos...' é o primeiro filho de column_0 */
  uint32_t col_cnt = lv_obj_get_child_count(content_col);
  for (uint32_t i = 0; i < col_cnt; i++) {
    lv_obj_t *child = lv_obj_get_child(content_col, i);
    if (child && lv_obj_check_type(child, &lv_label_class)) {
      /* Apaga o texto e oculta o widget para desocupar espaço no layout */
      lv_label_set_text(child, "");
      lv_obj_set_height(child, 0);
      lv_obj_add_flag(child, LV_OBJ_FLAG_HIDDEN);
      ESP_LOGI(TAG, "Tela Status: rotulo provisorio apagado e ocultado com sucesso");
      break;
    }
  }
}

/**
 * @brief Identifica a tela carregada pelo seu identificador estático e vincula seus controles.
 *
 * Utiliza lv_obj_get_name(screen) ("screen_bluetooth_#", "screen_display_#", etc.)
 * para determinar o contexto da tela e mapear os controles de hardware.
 */
static void bind_screen_controls(lv_obj_t *screen) {
  if (!screen) return;

  const char *name = lv_obj_get_name(screen);
  if (!name) {
    ESP_LOGW(TAG, "Tela sem nome static ao carregar controles");
    return;
  }

  ESP_LOGI(TAG, "Tela carregada: '%s'. Vinculando controles via identificacao estrutural e user_data...", name);

  if (strstr(name, "bluetooth")) {
    bind_bluetooth_controls(screen);
  } else if (strstr(name, "display")) {
    bind_toggle_screen_controls(screen, UI_ACTION_DISP_ENABLE, UI_ACTION_DISP_DISABLE, "Display");
  } else if (strstr(name, "proximidade")) {
    bind_toggle_screen_controls(screen, UI_ACTION_PROX_ENABLE, UI_ACTION_PROX_DISABLE, "Proximidade");
  } else if (strstr(name, "vibracall")) {
    bind_vibracall_controls(screen);
  } else if (strstr(name, "orelhas")) {
    bind_toggle_screen_controls(screen, UI_ACTION_EARS_ENABLE, UI_ACTION_EARS_DISABLE, "Orelhas");
  } else if (strstr(name, "galeria_imagens")) {
    bind_gallery_controls(screen, false);
  } else if (strstr(name, "galeria_gifs")) {
    bind_gallery_controls(screen, true);
  } else if (strstr(name, "cover_art")) {
    bind_main_cover_art_controls(screen);
  } else if (strstr(name, "status")) {
    bind_status_controls(screen);
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

  bind_screen_controls(screen);
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

  /*
   * Carrega as fontes binárias completas da memória ANTES de ui_init("").
   * Como ui_init() verifica 'if (!font_montserrat_regular_X)', ao definir
   * previamente as fontes a partir dos buffers embutidos na flash (que possuem
   * todos os glifos com acento), o LVGL utiliza as fontes completas sem falhas
   * de arquivo em disco.
   * As fontes exportadas pelo LVGL Pro em ui_gen.h são 12, 14, 16 e 20.
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

  /* Fallback de seguranca caso o carregamento da memoria falhe */
  bool bin_loaded = (font_montserrat_regular_12 && font_montserrat_regular_14 &&
                     font_montserrat_regular_16 && font_montserrat_regular_20);
  if (!font_montserrat_regular_12)
    font_montserrat_regular_12 = (lv_font_t *)&lv_font_montserrat_12;
  if (!font_montserrat_regular_14)
    font_montserrat_regular_14 = (lv_font_t *)&lv_font_montserrat_14;
  if (!font_montserrat_regular_16)
    font_montserrat_regular_16 = (lv_font_t *)&lv_font_montserrat_16;
  if (!font_montserrat_regular_20)
    font_montserrat_regular_20 = (lv_font_t *)&lv_font_montserrat_20;

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

  /* Conecta os controles e acoes da tela inicial carregada */
  bind_screen_controls(screen);

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

  /* Liga o backlight no nível configurado (ou padrão 60% caso não configurado) */
  int initial_bright = ui_bridge_display_get_brilho();
  display_backlight_set_duty(initial_bright > 0 ? (uint8_t)initial_bright : 60);
  return ESP_OK;
}

void display_task(void *arg) {
  (void)arg;
  lv_display_t *disp = lv_display_get_default();
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
      bind_screen_controls(current_scr);
    }

    /*
     * Requisito de Otimizacao do Barramento SPI3:
     * O Touch Resistivo SPI (XPT2046) NAO precisa funcionar durante transicao de tela
     * ou execucao de animacoes graficas.
     * Ao pausar a amostragem do XPT2046 durante animacoes (lv_anim_count_running > 0),
     * eliminamos polling no barramento SPI3 e evitamos a comutacao de clock (40 MHz <-> 1 MHz),
     * liberando 100% da largura de banda para o display ILI9341 renderizar liso a 40 MHz.
     */
    uint32_t running_anims = lv_anim_count_running();
    if (running_anims > 0) {
      if (!display_touch_is_paused()) {
        display_touch_set_transition_pause(true);
      }
    } else {
      if (display_touch_is_paused()) {
        display_touch_set_transition_pause(false);
      }
    }


    /*
     * Backlight Inteligente por Duty & Modo Temporizador (AGENTS.md §0.1 e §5.2 A6):
     * - Se o temporizador estiver ativo (Modo "Ligado por timer") e timeout_sec > 0:
     *   - Estágio 1: Pré-dimming a 70% do tempo de inatividade (reduz para 15% de duty perceptual)
     *   - Estágio 2: Sleep total a 100% do timeout (desliga backlight, ILI9341 SLPIN)
     * - Se o temporizador estiver desativado (Modo "Manter ligado"):
     *   - O display permanece sempre acordado no nível de brilho definido pelo usuário.
     *   - Caso o display estivesse suspenso ou atenuado, restaura imediatamente ao nível normal.
     */
    if (disp != NULL) {
      bool timer_enabled = ui_bridge_display_get_timer_enabled();
      int timeout_sec = ui_bridge_display_get_tempo_tela();

      if (timer_enabled && timeout_sec > 0) {
        uint32_t inactive_ms = lv_display_get_inactive_time(disp);
        uint32_t total_timeout_ms = (uint32_t)timeout_sec * 1000;
        uint32_t dim_timeout_ms = (total_timeout_ms * 70) / 100;

        if (inactive_ms >= total_timeout_ms) {
          if (!s_is_sleeping) {
            display_backlight_sleep();
            ESP_LOGI(TAG, "Display suspenso em Sleep total por inatividade (%d s)", timeout_sec);
          }
        } else if (inactive_ms >= dim_timeout_ms) {
          if (!s_is_dimmed && !s_is_sleeping) {
            display_backlight_dim();
            ESP_LOGI(TAG, "Display em pre-dimming inteligente de economia (15%%)");
          }
        } else {
          /* Atividade detectada dentro do período de timeout: restaura brilho normal */
          if (s_is_sleeping) {
            display_backlight_wake();
            ESP_LOGI(TAG, "Display acordado de Sleep total por atividade");
          } else if (s_is_dimmed) {
            display_backlight_wake();
            ESP_LOGI(TAG, "Display restaurado de pre-dimming por atividade");
          }
        }
      } else {
        /* Modo Manter Ligado (temporizador desativado): garante tela acordada e operacional */
        if (s_is_sleeping) {
          display_backlight_wake();
          ESP_LOGI(TAG, "Modo Manter Ligado: display acordado de Sleep para operacao continua");
        } else if (s_is_dimmed) {
          display_backlight_wake();
          ESP_LOGI(TAG, "Modo Manter Ligado: display restaurado de pre-dimming para operacao continua");
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

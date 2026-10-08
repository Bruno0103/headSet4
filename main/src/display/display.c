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
#include "ui.h"
#include "ui_bridge.h"

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
 * O controlador resistivo XPT2046 realiza a conversão analógica nos canais X/Y/Z.
 * 
 * Implementação correta e robusta:
 * 1. Invoca esp_lcd_touch_read_data(s_touch) para amostrar os eixos e a pressão.
 * 2. Verifica se houve coordenadas válidas retornadas por esp_lcd_touch_get_data.
 * 3. Repassa as coordenadas (X, Y) e o estado (LV_INDEV_STATE_PRESSED/RELEASED).
 * 4. Mantém diagnóstico no log serial para inspecionar exatamente o que o hardware responde.
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
      data->point.x = pt_data.x;
      data->point.y = pt_data.y;
      data->state = LV_INDEV_STATE_PRESSED;

      /* Log serial rate-limited (a cada 100 ms enquanto pressionado) para depuração */
      static uint32_t last_log_time = 0;
      uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
      if (now - last_log_time > 100) {
        last_log_time = now;
        ESP_LOGI(TAG, ">>> TOUCH ATIVO: X=%d, Y=%d (força Z=%d) <<<", pt_data.x, pt_data.y, pt_data.strength);
      }
      return;
    }
  }

  /*
   * Diagnóstico periódico de hardware:
   * Testa leitura direta dos registradores brutos do XPT2046 para saber se o pino MISO
   * está respondendo ou se o barramento SPI está recebendo apenas 0x0000 ou 0xFFFF.
   */
  static uint32_t last_diag_time = 0;
  uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
  if (now - last_diag_time > 2000) {
    last_diag_time = now;
    uint8_t raw_z1[2] = {0}, raw_z2[2] = {0}, raw_x[2] = {0};
    esp_err_t r1 = esp_lcd_panel_io_rx_param(s_touch->io, 0xB0 | 0x01, raw_z1, 2);
    esp_err_t r2 = esp_lcd_panel_io_rx_param(s_touch->io, 0xC0 | 0x01, raw_z2, 2);
    esp_err_t r3 = esp_lcd_panel_io_rx_param(s_touch->io, 0xD0 | 0x01, raw_x, 2);
    uint16_t val_z1 = (raw_z1[0] << 8) | raw_z1[1];
    uint16_t val_z2 = (raw_z2[0] << 8) | raw_z2[1];
    uint16_t val_x  = (raw_x[0] << 8) | raw_x[1];
    uint16_t calc_z = (val_z1 >> 3) + (4096 - (val_z2 >> 3));
    ESP_LOGI(TAG, "[DIAG TOUCH] SPI status: r=(%d,%d,%d) | Z1=0x%04X Z2=0x%04X X=0x%04X | calc_z=%u",
             r1, r2, r3, val_z1, val_z2, val_x, calc_z);
  }

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
   * Habilita pull-up interno no pino MISO (T_DO).
   * No GPIO 26 o ESP32 possui resistor de pull-up interno de ~45k,
   * evitando que a linha flutue para nível indeterminado quando o chip XPT2046
   * entra em modo tri-state entre transmissões SPI.
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

  /* --- Touch XPT2046 (mesmo barramento SPI, CS dedicado em TOUCH_GPIO_CS) --- */
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
   * que ruídos no pino de interrupção T_IRQ mascarem ou impeçam leituras válidas do SPI.
   * A resolução x_max/y_max é alinhada com as dimensões do display ILI9341 (240x320).
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

  ESP_LOGI(TAG, "Driver Touch XPT2046 inicializado com sucesso (CS=%d, SCLK=%d, MOSI=%d, MISO=%d)",
           TOUCH_GPIO_CS, LCD_GPIO_SCLK, LCD_GPIO_MOSI, LCD_GPIO_MISO);

  ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "disp on");
  return ESP_OK;
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

  /* Inicializacao das variaveis, estilos e fontes */
  ui_init("");

  /* 
   * Criacao da tela principal gerada pelo novo projeto de interface (LVGL Pro).
   * A nova tela inicial exportada por ui.h / screen_main_gen.h e a screen_main_create().
   */
  lv_obj_t *screen = screen_main_create();
  if (screen == NULL) {
    ESP_LOGE(TAG, "ERRO CRITICO: screen_main_create() retornou NULL! Carregando tela "
                  "placeholder de emergencia...");
    ui_placeholder();
    return ESP_FAIL;
  }

  /* Carrega a tela com sucesso */
  lv_screen_load(screen);
  ESP_LOGI(TAG, "Tela principal (screen_main) carregada com sucesso.");
  return ESP_OK;
}

esp_err_t display_init(void) {
  /* Inicializa a camada de ponte entre a UI e os recursos de hardware do fone
   */
  ui_bridge_init();

  lv_init();
  lv_tick_set_cb(lvgl_tick_cb);

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
  for (;;) {
    uint32_t wait_ms = lv_timer_handler();
    if (wait_ms < 5)
      wait_ms = 5;
    if (wait_ms > 30)
      wait_ms = 30;
    vTaskDelay(pdMS_TO_TICKS(wait_ms));
  }
}

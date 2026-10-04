#include "display.h"

#include "app_gen.h"
#include "testeFinal_gen.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7735.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

static esp_lcd_panel_handle_t panel_handle = NULL;

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area,
                          uint8_t *px_map)
{
    int pixels = (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1);

    // Inverte os bytes do RGB565 para o formato esperado pelo controlador SPI
    lv_draw_sw_rgb565_swap(px_map, pixels);

    esp_lcd_panel_draw_bitmap(panel_handle, area->x1, area->y1, area->x2 + 1,
                              area->y2 + 1, px_map);
    lv_display_flush_ready(disp);
}

static void lvgl_tick_cb(void *arg) { lv_tick_inc(LVGL_TICK_PERIOD_MS); }

// Inicializa o hardware do display: backlight, barramento SPI e painel ST7735
static void display_hw_init(void)
{
    // 1. Configurar e ligar o Backlight
    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << LCD_GPIO_BL,
    };
    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
    gpio_set_level(LCD_GPIO_BL, 1);

    // 2. SPI bus
    const spi_bus_config_t buscfg = {
        .sclk_io_num = LCD_GPIO_SCLK,
        .mosi_io_num = LCD_GPIO_MOSI,
        .miso_io_num = GPIO_NUM_NC,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = LCD_H_RES * LCD_V_RES * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_NUM, &buscfg, SPI_DMA_CH_AUTO));

    // 3. Panel IO
    esp_lcd_panel_io_handle_t lcd_io = NULL;
    const esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = LCD_GPIO_DC,
        .cs_gpio_num = LCD_GPIO_CS,
        .pclk_hz = LCD_PIXEL_CLK_HZ,
        .lcd_cmd_bits = LCD_CMD_BITS,
        .lcd_param_bits = LCD_PARAM_BITS,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)LCD_SPI_NUM, &io_config, &lcd_io));

    // 4. Driver ST7735
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_GPIO_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(
        esp_lcd_new_panel_st7735(lcd_io, &panel_config, &panel_handle));

    // 5. Init display
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_handle, 0, 0));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, false));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
}

void display_init(void)
{
    // Hardware do LCD (backlight, SPI, painel)
    display_hw_init();

    // 6. Inicializar LVGL
    lv_init();

    const esp_timer_create_args_t tick_args = {
        .callback = lvgl_tick_cb,
        .name = "lvgl_tick",
    };
    esp_timer_handle_t tick_timer;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(
        esp_timer_start_periodic(tick_timer, LVGL_TICK_PERIOD_MS * 1000));

    // Buffer parcial para renderização do LVGL
    size_t draw_buffer_sz = LCD_H_RES * 16 * sizeof(uint16_t);
    uint8_t *buf =
        heap_caps_malloc(draw_buffer_sz, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    assert(buf != NULL);

    lv_display_t *display = lv_display_create(LCD_H_RES, LCD_V_RES);
    lv_display_set_buffers(display, buf, NULL, draw_buffer_sz,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(display, lvgl_flush_cb);

    // Inicialização da interface gráfica gerada
    testeFinal_init_gen("");
    lv_obj_t *screen = app_create();
    lv_screen_load(screen);
}

void display_task(void *arg)
{
    while (true)
    {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

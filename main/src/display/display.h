#pragma once

#include "driver/spi_master.h"

// Definições do LCD em pinos livres para não conflitar com o WM8960.
#define LCD_GPIO_SCLK      19
#define LCD_GPIO_MOSI      21
#define LCD_GPIO_DC         2
#define LCD_GPIO_CS         5
#define LCD_GPIO_RST        4
#define LCD_GPIO_BL        15

#define LCD_SPI_NUM         SPI2_HOST
#define LCD_H_RES           128
#define LCD_V_RES           160
#define LCD_PIXEL_CLK_HZ    (50 * 1000 * 1000)
#define LCD_CMD_BITS        8
#define LCD_PARAM_BITS      8
#define LVGL_TICK_PERIOD_MS 2

// Inicializa hardware do LCD, LVGL, buffers e a tela gerada.
// Chame uma única vez a partir da main.c, antes de criar a task.
void display_init(void);

// Loop da task do LVGL (lv_timer_handler + delay).
// A main.c e quem cria a task do FreeRTOS com esta funcao.
void display_task(void *arg);
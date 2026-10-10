#pragma once

#include "esp_err.h"
#include "driver/spi_master.h"

#include "pinout.h"

#define LCD_SPI_NUM         SPI3_HOST
#define LCD_H_RES           240
#define LCD_V_RES           320
#define LCD_PIXEL_CLK_HZ    (40 * 1000 * 1000)
#define LCD_CMD_BITS        8
#define LCD_PARAM_BITS      8
#define LCD_DRAW_BUF_LINES  16      /* linhas por buffer parcial do LVGL (x2 buffers, DMA/RAM interna) */
// Clock SPI recomendado pelo fabricante do XPT2046 para evitar reflexão ou atraso no MISO: 1 MHz
#define TOUCH_PIXEL_CLK_HZ  (1 * 1000 * 1000)

/* Orientacao: o ILI9341 e nativamente retrato 240x320.
 * Ajuste aqui se a imagem sair espelhada/girada ou as cores invertidas. */
#define LCD_SWAP_XY         0
#define LCD_MIRROR_X        1
#define LCD_MIRROR_Y        0
#define LCD_INVERT_COLOR    0
#define LCD_COLOR_BGR       1       /* a maioria dos modulos ILI9341 e BGR */

/* Orientacao e calibracao do Touch Screen XPT2046:
 * Painel resistivo frequentemente possui eixos ou orientacoes fisicas independentes do LCD.
 * Deixamos flags dedicadas para calibrar e inverter eixos do touch sem alterar o display. */
#define TOUCH_SWAP_XY       0
#define TOUCH_MIRROR_X      1
#define TOUCH_MIRROR_Y      0

/* Inicializa SPI, ILI9341, XPT2046 e o LVGL base (drivers e buffers).
 * Chame uma unica vez a partir da main.c. */
esp_err_t display_init(void);

/* Inicializa a interface grafica gerada (ui) e carrega a tela screen_main.
 * Possui tratativa de erro e fallback caso screen_main_create falhe. */
esp_err_t display_create_app_ui(void);

/* Loop da task do LVGL (lv_timer_handler + delay). A main.c cria a task do FreeRTOS. */
void display_task(void *arg);


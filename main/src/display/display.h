#pragma once

#include "esp_err.h"
#include "driver/spi_master.h"

#include "pinout.h"

#define LCD_SPI_NUM         SPI2_HOST
#define LCD_H_RES           240
#define LCD_V_RES           320
#define LCD_PIXEL_CLK_HZ    (40 * 1000 * 1000)
#define LCD_CMD_BITS        8
#define LCD_PARAM_BITS      8
#define LCD_DRAW_BUF_LINES  16      /* linhas por buffer parcial do LVGL (x2 buffers, DMA/RAM interna) */

#define TOUCH_PIXEL_CLK_HZ  (2 * 1000 * 1000)

/* Orientacao: o ILI9341 e nativamente retrato 240x320. Painel e touch usam os mesmos flags;
 * ajuste aqui se a imagem sair espelhada/girada ou o toque ficar invertido. Se girar (swap_xy),
 * troque tambem LCD_H_RES/LCD_V_RES. */
#define LCD_SWAP_XY         0
#define LCD_MIRROR_X        1
#define LCD_MIRROR_Y        0
#define LCD_INVERT_COLOR    0
#define LCD_COLOR_BGR       1       /* a maioria dos modulos ILI9341 e BGR */

/* Inicializa SPI, ILI9341, XPT2046, LVGL (display + ponteiro) e a tela inicial.
 * Chame uma unica vez a partir da main.c, antes de criar a task. */
esp_err_t display_init(void);

/* Loop da task do LVGL (lv_timer_handler + delay). A main.c cria a task do FreeRTOS. */
void display_task(void *arg);

#pragma once

#include "driver/spi_master.h"
#include "esp_err.h"

#include "pinout.h"

#define LCD_SPI_NUM SPI3_HOST
#define LCD_H_RES 240
#define LCD_V_RES 320
#define LCD_PIXEL_CLK_HZ (40 * 1000 * 1000)
#define LCD_CMD_BITS 8
#define LCD_PARAM_BITS 8
#define LCD_DRAW_BUF_LINES                                                     \
  16 /* linhas por buffer parcial do LVGL (x2 buffers, DMA/RAM interna) */
// Clock SPI recomendado pelo fabricante do XPT2046 para evitar reflexão ou
// atraso no MISO: 1 MHz
#define TOUCH_PIXEL_CLK_HZ (1 * 1000 * 1000)

/* Orientacao: o ILI9341 e nativamente retrato 240x320.
 * Ajuste aqui se a imagem sair espelhada/girada ou as cores invertidas. */
#define LCD_SWAP_XY 0
#define LCD_MIRROR_X 1
#define LCD_MIRROR_Y 0
#define LCD_INVERT_COLOR 0
#define LCD_COLOR_BGR 1 /* a maioria dos modulos ILI9341 e BGR */

/* Orientacao e calibracao do Touch Screen XPT2046:
 * Painel resistivo frequentemente possui eixos ou orientacoes fisicas
 * independentes do LCD. Deixamos flags dedicadas para calibrar e inverter eixos
 * do touch sem alterar o display. */
#define TOUCH_SWAP_XY 0
#define TOUCH_MIRROR_X 1
#define TOUCH_MIRROR_Y 0

/*
 * Calibração e limitação de área ativa do Touch Screen:
 * O vidro/painel resistivo físico do touch é maior (a borda cinza externa cobre
 * além da tela LCD visível). O driver padrão esp_lcd_touch_xpt2046 divide a
 * leitura do ADC (0..4095) por toda a área do vidro gerando coordenadas de 0 a
 * 240 e 0 a 320 em toda a extensão do vidro cinza.
 *
 * Como a parte ACESA do LCD (240x320) fica apenas dentro da janela interna:
 * Definimos os limites (em coordenadas brutas do sensor) onde a tela útil
 * começa e termina:
 * - TOUCH_ACTIVE_MIN_X / TOUCH_ACTIVE_MAX_X (margens horizontais
 * esquerda/direita)
 * - TOUCH_ACTIVE_MIN_Y / TOUCH_ACTIVE_MAX_Y (margens verticais
 * superior/inferior)
 *
 * As leituras dentro desse retângulo são remapeadas proporcionalmente para
 * 0..(LCD_H_RES - 1) e 0..(LCD_V_RES - 1). Qualquer toque fora da tela ativa
 * (na borda cinza) é ignorado ou limitado.
 */
#define TOUCH_ACTIVE_MIN_X 30
#define TOUCH_ACTIVE_MAX_X 209
#define TOUCH_ACTIVE_MIN_Y 16
#define TOUCH_ACTIVE_MAX_Y 286

/* Inicializa SPI, ILI9341, Touch CST816S e o LVGL base (drivers e buffers).
 * Chame uma unica vez a partir da main.c. */
esp_err_t display_init(void);

/* Inicializa a interface grafica gerada (ui) e carrega a tela screen_main.
 * Possui tratativa de erro e fallback caso screen_main_create falhe. */
esp_err_t display_create_app_ui(void);

/* Loop da task do LVGL (lv_timer_handler + delay). A main.c cria a task do FreeRTOS. */
void display_task(void *arg);

/* ============================================================================
 * CONTROLE DE BACKLIGHT INTELIGENTE POR HARDWARE PWM LEDC (5 kHz, 10 bits)
 * ============================================================================ */

/** @brief Inicializa o driver LEDC no GPIO 13 para controle fino de brilho. */
esp_err_t display_backlight_init(void);

/** @brief Ajusta o brilho nominal do display de 0 a 100% com curva perceptual. */
void display_backlight_set_duty(uint8_t brightness_pct);

/** @brief Retorna o percentual atual de brilho configurado (0 a 100%). */
uint8_t display_backlight_get_duty(void);

/** @brief Ativa o modo de pré-dimming de inatividade (redução suave para 15% de duty). */
void display_backlight_dim(void);

/** @brief Acorda o backlight restaurando o brilho nominal configurado. */
void display_backlight_wake(void);

/** @brief Retorna se o backlight está atualmente no modo pré-dimming de economia. */
bool display_backlight_is_dimmed(void);

/** @brief Retorna se a tela está suspensa com backlight desligado (Sleep). */
bool display_backlight_is_sleeping(void);

/** @brief Coloca o display e backlight em modo Sleep total (0% duty, LCD desligado, touch em sleep). */
void display_backlight_sleep(void);


/** @brief Pausa ou retoma a amostragem de toque durante transições de tela ou animações. */
void display_touch_set_transition_pause(bool pause);

/** @brief Retorna se a amostragem de toque está pausada. */
bool display_touch_is_paused(void);



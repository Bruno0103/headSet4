#pragma once

#include "driver/gpio.h" // Necessário para macros GPIO_NUM_*

/**
 * @file pinout.h
 * @brief Pinagem do projeto HeadSet - ESP32-WROVER-E (revisada).
 *
 * Regras seguidas:
 *  - GPIO6-11  : flash SPI interna  -> NAO usar
 *  - GPIO16/17 : PSRAM do WROVER    -> NAO usar
 *  - GPIO12    : strapping (tensao da flash) -> NAO usar
 *  - GPIO15/2/5: strapping -> evitados em sinais criticos
 *  - GPIO1/3   : UART0 (log/programacao) -> reservados
 *  - GPIO34-39 : somente entrada, SEM pull interno
 *  - ADC2 (0,2,4,12-15,25-27) nao funciona com Wi-Fi/BT -> bateria no ADC1
 *
 * Livres/reserva: GPIO2, GPIO12 (nao recomendado), GPIO16, GPIO17 (PSRAM - nao
 * usar)
 */
#pragma once

// ============================================================================
// SPI (VSPI) compartilhado: Display LCD ILI9341 240x320 + touch XPT2046
// ============================================================================
#define LCD_GPIO_SCLK 18 // LCD SCK  + T_CLK
#define LCD_GPIO_MOSI 23 // LCD SDI  + T_DIN
#define LCD_GPIO_MISO 19 // T_DO (XPT2046). Veja nota abaixo sobre o SDO do LCD

#define LCD_GPIO_CS 5  // CS do LCD
#define LCD_GPIO_DC 27 // DC/RS do LCD (antes no GPIO2, pino de strapping)
#define LCD_GPIO_RST 4 // RST do LCD
#define LCD_GPIO_BL 13 // Backlight (antes no GPIO15, pino de strapping)

#define TOUCH_GPIO_CS 26  // T_CS
#define TOUCH_GPIO_IRQ 39 // T_IRQ -> entrada pura (antes no GPIO12, strapping)
/* NOTA IMPORTANTE (causa comum de touch morto):
 * Em muitos modulos ILI9341 o pino SDO/MISO do LCD NAO vai a alta impedancia
 * quando o CS do LCD esta em nivel alto, e disputa a linha MISO com o T_DO.
 * Como o LCD so recebe dados, NAO ligue o SDO do LCD ao ESP32: ligue apenas
 * o T_DO ao GPIO19. Rode o touch a ~2 MHz (clock proprio do device SPI). */

// ============================================================================
// I2C - Controle do Codec de Audio (WM8960) + APDS-9930
// ============================================================================
#define BOARD_I2C_SDA 21
#define BOARD_I2C_SCL 22
#define BOARD_I2C_HZ 100000 /* 100 kHz: poupa ruido no audio */

// ============================================================================
// I2S - Audio (WM8960)
// ============================================================================
// O WM8960 precisa de MCLK (a PLL interna tambem usa MCLK como entrada).
// No ESP32 o MCLK so pode sair em GPIO0, GPIO1 ou GPIO3 -> usamos o GPIO0.
#define BOARD_I2S_MCLK 0  /* -> MCLK do WM8960 (CLK_OUT1) */
#define BOARD_I2S_BCLK 32 /* -> BCLK */
#define BOARD_I2S_WS 33   /* -> DACLRC *e* ADCLRC (ligados juntos) */
#define BOARD_I2S_DOUT 25 /* ESP32 -> DACDAT (musica / voz) */
#define BOARD_I2S_DIN 35  /* ADCDAT -> ESP32 (microfone); pino so entrada */

// ============================================================================
// Medidor de Bateria
// ============================================================================
#define BAT_ADC_PIN GPIO_NUM_34  /* ADC1_CH6, compativel com Wi-Fi/BT */
#define BAT_CTRL_PIN GPIO_NUM_14 /* habilita divisor resistivo */

// ============================================================================
// Botao de troca de dispositivo / pareamento
// ============================================================================
// O GPIO0 agora e o MCLK, entao o botao BOOT da placa NAO pode mais ser usado.
// Botao externo entre GPIO2 e GND (pull-up interno ativado em software).
// (GPIO2 em nivel baixo no boot e inofensivo; so entra em modo download se
//  GPIO0 tambem estiver baixo.)
#define BOARD_BUTTON_SWITCH_GPIO GPIO_NUM_2

// ============================================================================
// Sensor de proximidade APDS-9930 (I2C compartilhado com o codec)
// ============================================================================
#define BOARD_APDS9930_I2C_ADDR 0x39
#define BOARD_APDS9930_PWR_GPIO                                                \
  15 /* alimenta VDD do sensor; VL fixo em 3,3 V */
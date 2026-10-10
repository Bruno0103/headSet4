#pragma once

#include "driver/gpio.h" /* macros GPIO_NUM_* */

/**
 * @file pinout.h
 * @brief Pinagem do projeto HeadSet - ESP32-WROVER-E (rev. 3).
 *
 * Prioridade de projeto: 1) WM8960 (I2S + I2C)  2) ILI9341 + XPT2046  3)
 * demais.
 *
 * Proibidos:
 *  - GPIO6-11  : flash SPI interna
 *  - GPIO16/17 : PSRAM do WROVER
 *  - GPIO12    : strapping (tensao da flash) - se alto no boot, o chip nao sobe
 *  - GPIO0     : strapping/BOOT - reservado ao botao de gravacao, sem fio
 * externo
 *  - GPIO1/3   : UART0 (log/programacao)
 *  Evitar em sinais criticos: GPIO2/5/15 (strapping).
 *  Somente entrada e sem pull interno: GPIO34-39.
 *  ADC2 (0,2,4,12-15,25-27) nao funciona com BT ligado -> bateria no ADC1.
 */

/* ============================================================================
 * SPI3_HOST (VSPI) compartilhado: ILI9341 240x320 + XPT2046
 * 18/23/19/5 sao os pinos NATIVOS (IOMUX) do VSPI: sem matriz de GPIO,
 * clock de 40 MHz estavel no LCD e MISO confiavel para o touch.
 * Em display.h use: #define LCD_SPI_NUM SPI3_HOST
 * ========================================================================== */
#define LCD_GPIO_SCLK 18 /* LCD SCK  + T_CLK */
#define LCD_GPIO_MOSI 23 /* LCD SDI  + T_DIN */
#define LCD_GPIO_MISO 19 /* SOMENTE T_DO. NAO ligar LCD SDO nem SD_MISO */
#define LCD_GPIO_CS 5 /* CS do LCD (VSPI CS0 nativo; strapping, tem pull-up externo no modulo) */
#define LCD_GPIO_DC 27 /* DC/RS */
#define LCD_GPIO_RST 4 /* RST do LCD */
#define LCD_GPIO_BL 13 /* Backlight (via transistor/MOSFET se o modulo nao tiver) */

#define TOUCH_GPIO_CS 22 /* T_CS */
#define TOUCH_GPIO_IRQ (-1) /* T_IRQ: entrada pura; o modulo ja tem pull-up. Nao usado (polling) */

/* Checklist de fiacao do touch (causa mais comum de touch morto):
 *  - LCD SDO, SD_CS, SD_MISO, SD_MOSI, SD_SCK: SEM LIGACAO.
 *  - T_DO -> GPIO19 (fio curto, longe do BCLK/WS do audio).
 *  - Touch a 1-2 MHz em device proprio (ja configurado em display.h).
 *  - 3,3 V e GND do modulo no mesmo ponto de GND do codec (estrela), nao em
 * serie. */

/* ============================================================================
 * I2C0 - WM8960 (0x1A) + APDS-9930 (0x39). Pull-ups ja existem no breakout.
 * ========================================================================== */
#define BOARD_I2C_SDA 21
#define BOARD_I2C_SCL 26
#define BOARD_I2C_HZ 100000

/* ============================================================================
 * I2S0 - WM8960. O ESP32 e o mestre de BCLK/WS.
 * MCLK: o breakout gera o proprio (cristal de 24 MHz), que e o que o
 * audio_codec.c assume no PLL. O ESP32 NAO envia MCLK: nao ligue nada ao
 * GPIO0 (o MCLK do breakout so vai para o codec).
 * ========================================================================== */
#define BOARD_I2S_MCLK (-1) /* I2S_GPIO_UNUSED */
#define BOARD_I2S_BCLK 32   /* -> BCLK */
#define BOARD_I2S_WS 33     /* -> DACLRC e ADCLRC (ligados juntos) */
#define BOARD_I2S_DOUT 25   /* ESP32 -> DACDAT */
#define BOARD_I2S_DIN 35    /* ADCDAT -> ESP32 (entrada pura) */

/* ============================================================================
 * Bateria
 * ========================================================================== */
#define BAT_ADC_PIN GPIO_NUM_34 /* ADC1_CH6 */
#define BAT_CTRL_PIN GPIO_NUM_14 /* habilita o divisor via MOSFET (pino emite sinal curto no boot: inofensivo) */

/* ============================================================================
 * Botao de troca de dispositivo / pareamento (GPIO2 -> GND, pull-up interno).
 * Baixo no boot so importa se o GPIO0 tambem estiver baixo.
 * ========================================================================== */
#define BOARD_BUTTON_SWITCH_GPIO GPIO_NUM_2

/* ============================================================================
 * APDS-9930 (I2C compartilhado)
 * VDD ligado direto em 3,3 V (sem GPIO de energia): evita alimentar o sensor
 * pelos pull-ups do I2C e tira um pino de strapping do circuito.
 * O driver usa PON/PEN por registrador quando CONFIG_HEADSET_APDS_DUTY_CYCLE=y.
 * ========================================================================== */
#define BOARD_APDS9930_I2C_ADDR 0x39
#define BOARD_APDS9930_PWR_GPIO (-1) /* -1 = sem controle de energia por GPIO */

/* ============================================================================
 * Reserva
 *  Livres/uso restrito: GPIO0 (BOOT, deixar livre), GPIO15 (strapping, so saida
 *  com pull-down externo), GPIO36 (somente entrada).
 *  Todos os pinos de saida "normais" estao ocupados. Para vibracall e servos
 *  use um PCA9685 no I2C ja existente (16 canais PWM, endereco 0x40), em vez
 *  de gastar GPIO.
 * ========================================================================== */
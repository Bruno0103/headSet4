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
 * SPI3_HOST (VSPI): ILI9341 240x320 (40 MHz) + Touch XPT2046 (1 MHz)
 * 18/23/19/5 sao os pinos NATIVOS (IOMUX) do VSPI no silicio do ESP32.
 * Esta rota direta evita a GPIO Matrix, garantindo integridade de sinal a 40 MHz.
 * O Touch XPT2046 e o LCD ILI9341 compartilham SCLK (18) e MOSI (23).
 * MISO (19) e exclusivo do Touch T_DO (o SDO do LCD NAO deve ser ligado).
 * Durante transicoes de tela e animacoes graficas, a amostragem do XPT2046 e
 * suspensa temporariamente para liberar o barramento SPI3 e eliminar polling.
 * ========================================================================== */
#define LCD_GPIO_SCLK 18 /* LCD SCK / Touch T_CLK nativo VSPI */
#define LCD_GPIO_MOSI 23 /* LCD SDI / Touch T_DIN nativo VSPI */
#define LCD_GPIO_MISO 19 /* Touch T_DO exclusivo (pull-up interno contra flutuacao) */
#define LCD_GPIO_CS 5    /* CS do LCD (VSPI CS0 nativo; strapping, pull-up de 10k) */
#define LCD_GPIO_DC 27   /* DC/RS do LCD */
#define LCD_GPIO_RST 4   /* RST do LCD */
#define LCD_GPIO_BL 13   /* Backlight (LEDC Hardware PWM a 5 kHz, resolucao 10 bits) */

#define TOUCH_GPIO_CS  22 /* Chip Select do Touch XPT2046 */
#define TOUCH_GPIO_IRQ (-1) /* Sem IRQ por pino: polling coordenado com suspensao em transicao */


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
 * Botao de troca de dispositivo / pareamento: Botão BOOT on-board (GPIO0 -> GND).
 * Possui pull-up externo de 10k e capacitor na placa ESP32 DevKit/WROVER.
 * ========================================================================== */
#define BOARD_BUTTON_SWITCH_GPIO GPIO_NUM_0

/* ============================================================================
 * APDS-9930 (I2C compartilhado)
 * VDD ligado direto em 3,3 V (sem GPIO de energia): evita alimentar o sensor
 * pelos pull-ups do I2C e tira um pino de strapping do circuito.
 * O driver usa PON/PEN por registrador quando CONFIG_HEADSET_APDS_DUTY_CYCLE=y.
 * ========================================================================== */
#define BOARD_APDS9930_I2C_ADDR 0x39
#define BOARD_APDS9930_PWR_GPIO (-1) /* -1 = sem controle de energia por GPIO */

/* ============================================================================
 * Atuadores / Vibracall (Motor Háptico)
 * GPIO 15 aprovado pelo usuário (MTDO strapping com pull-down/livre pós-boot)
 * ========================================================================== */
#define BOARD_VIBRACALL_GPIO     15

/* ============================================================================
 * Reserva
 *  Livres/uso restrito: GPIO0 (BOOT, deixar livre), GPIO36 (somente entrada).
 *  Para servos use um PCA9685 no I2C ja existente (16 canais PWM, endereco 0x40).
 * ========================================================================== */
#pragma once

#include "driver/gpio.h" // Necessário para macros GPIO_NUM_*

/**
 * @file pinout.h
 * @brief Definições de todos os pinos do ESP32 WROVER do projeto HeadSet.
 *        Arquivo criado para centralizar a configuração e evitar conflitos.
 */

// ============================================================================
// Display LCD (ST7735/SPI)
// ============================================================================
// Utilizando os pinos padrão do VSPI do ESP32
#define LCD_GPIO_SCLK      18   // SCK padrão (VSPI_SCK)
#define LCD_GPIO_MOSI      23   // MOSI padrão (VSPI_MOSI)
#define LCD_GPIO_DC         2
#define LCD_GPIO_CS         5   // CS padrão (VSPI_SS)
#define LCD_GPIO_RST        4
#define LCD_GPIO_BL        15

// ============================================================================
// I2C - Controle do Codec de Áudio (WM8960)
// ============================================================================
// Utilizando os pinos padrão do I2C do ESP32
#define BOARD_I2C_SDA          21
#define BOARD_I2C_SCL          22

// ============================================================================
// I2S - Interface de Áudio (WM8960)
// ============================================================================
#define BOARD_I2S_BCLK         32
#define BOARD_I2S_WS           13    /* Ligar DACLRC *e* ADCLRC neste pino */
#define BOARD_I2S_DOUT         25    /* ESP32 -> DACDAT (música / voz) - Alterado de 14 para 25 (DAC_1) */
#define BOARD_I2S_DIN          27    /* ADCDAT -> ESP32 (microfone, só em chamadas) */

// ============================================================================
// Medidor de Bateria
// ============================================================================
#define BAT_ADC_PIN         GPIO_NUM_34
#define BAT_CTRL_PIN        GPIO_NUM_14   /* Mantido no 14, agora livre */

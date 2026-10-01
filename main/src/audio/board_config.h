#pragma once
/* Pinos e constantes da placa - ajuste tudo aqui, em um so lugar.
 * Confira conflito com os pinos do display (ST7735/SPI). */

/* I2C: controle do WM8960 (Qwiic do SparkFun Thing Plus ESP32) */
#define BOARD_I2C_SDA          21
#define BOARD_I2C_SCL          22
#define BOARD_I2C_HZ           100000
#define WM8960_I2C_ADDR        0x1A

/* I2S: mesmos pinos do sketch Super Headphones (Thing Plus C) */
#define BOARD_I2S_BCLK         32
#define BOARD_I2S_WS           13    /* ligar DACLRC *e* ADCLRC neste pino */
#define BOARD_I2S_DOUT         14    /* ESP32 -> DACDAT (musica / voz do interlocutor) */
#define BOARD_I2S_DIN          27    /* ADCDAT -> ESP32 (microfone, so em chamadas) */

/* Volume inicial na escala do AVRCP (0..127) */
#define CODEC_DEFAULT_VOLUME   40

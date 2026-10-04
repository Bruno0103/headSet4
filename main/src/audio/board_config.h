#pragma once
/* Pinos e constantes da placa */

#include "pinout.h"

/* I2C: controle do WM8960 (Qwiic do SparkFun Thing Plus ESP32) */
#define BOARD_I2C_HZ 100000
#define WM8960_I2C_ADDR 0x1A

/* I2S: mesmos pinos do sketch Super Headphones (Thing Plus C) */

/* Volume inicial na escala do AVRCP (0..127) */
#define CODEC_DEFAULT_VOLUME 40

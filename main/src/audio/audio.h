#pragma once
#include "esp_err.h"

/** Sobe o codec WM8960 (I2C) e o caminho de dados (I2S). Chamar uma vez, antes do Bluetooth. */
esp_err_t audio_init(void);

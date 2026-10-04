#pragma once

#include <stdint.h>

#include <esp_err.h>

/** Sobe a pilha Bluetooth Classic e inicializa os perfis. */
esp_err_t bluetooth_init(void);

#pragma once
/* Driver minimo do WM8960: so escrita I2C + copia local dos registradores.
 * O chip e write-only, por isso a copia local e necessaria para alterar campos. */
#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

/* Registradores usados (datasheet WM8960 v4.2) */
#define WM8960_R_LINVOL      0x00
#define WM8960_R_RINVOL      0x01
#define WM8960_R_LOUT1       0x02
#define WM8960_R_ROUT1       0x03
#define WM8960_R_CLOCK1      0x04
#define WM8960_R_DACCTL1     0x05
#define WM8960_R_IFACE1      0x07
#define WM8960_R_CLOCK2      0x08
#define WM8960_R_RESET       0x0F
#define WM8960_R_3D          0x10
#define WM8960_R_ALC1        0x11
#define WM8960_R_ALC2        0x12
#define WM8960_R_ALC3        0x13
#define WM8960_R_NOISEGATE   0x14
#define WM8960_R_PWR1        0x19
#define WM8960_R_PWR2        0x1A
#define WM8960_R_LINPATH     0x20
#define WM8960_R_RINPATH     0x21
#define WM8960_R_LOUTMIX     0x22
#define WM8960_R_ROUTMIX     0x25
#define WM8960_R_BYPASS1     0x2D
#define WM8960_R_BYPASS2     0x2E
#define WM8960_R_PWR3        0x2F
#define WM8960_R_PLL1        0x34
#define WM8960_R_PLL2        0x35
#define WM8960_R_PLL3        0x36
#define WM8960_R_PLL4        0x37

/** Detecta o chip no barramento (equivale ao codec.begin() do sketch). */
esp_err_t wm8960_attach(i2c_master_bus_handle_t bus);

/** Reset por software. Invalida a copia local. */
esp_err_t wm8960_reset(void);

/** Escreve a palavra inteira (9 bits) e atualiza a copia local. */
esp_err_t wm8960_write(uint8_t reg, uint16_t value);

/** Altera so os bits de 'mask'. Exige que o registrador ja tenha sido escrito por inteiro. */
esp_err_t wm8960_update(uint8_t reg, uint16_t mask, uint16_t value);

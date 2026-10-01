#include "wm8960.h"

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "board_config.h"

#define REG_COUNT 56

static const char *TAG = "wm8960";

static i2c_master_dev_handle_t s_dev;
static SemaphoreHandle_t       s_lock;
static uint16_t                s_shadow[REG_COUNT];
static bool                    s_known[REG_COUNT];

esp_err_t wm8960_attach(i2c_master_bus_handle_t bus)
{
    ESP_RETURN_ON_ERROR(i2c_master_probe(bus, WM8960_I2C_ADDR, 100), TAG,
                        "WM8960 nao respondeu em 0x%02X - confira a fiacao", WM8960_I2C_ADDR);

    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = WM8960_I2C_ADDR,
        .scl_speed_hz    = BOARD_I2C_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &cfg, &s_dev), TAG, "add device");

    s_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_NO_MEM, TAG, "mutex");
    return ESP_OK;
}

/* Registrador de 7 bits + dado de 9 bits em 2 bytes: [reg<<1 | bit8] [bits 7:0] */
static esp_err_t write_locked(uint8_t reg, uint16_t value)
{
    uint8_t buf[2] = { (uint8_t)((reg << 1) | ((value >> 8) & 1)), (uint8_t)(value & 0xFF) };
    esp_err_t err = i2c_master_transmit(s_dev, buf, sizeof buf, 100);
    if (err == ESP_OK) {
        s_shadow[reg] = value & 0x1FF;
        s_known[reg]  = true;
    }
    return err;
}

esp_err_t wm8960_write(uint8_t reg, uint16_t value)
{
    ESP_RETURN_ON_FALSE(reg < REG_COUNT, ESP_ERR_INVALID_ARG, TAG, "reg 0x%02X invalido", reg);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = write_locked(reg, value);
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t wm8960_update(uint8_t reg, uint16_t mask, uint16_t value)
{
    ESP_RETURN_ON_FALSE(reg < REG_COUNT, ESP_ERR_INVALID_ARG, TAG, "reg 0x%02X invalido", reg);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (s_known[reg]) {
        err = write_locked(reg, (s_shadow[reg] & ~mask) | (value & mask));
    } else {
        ESP_LOGE(TAG, "update em reg 0x%02X nunca escrito por inteiro", reg);
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t wm8960_reset(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = write_locked(WM8960_R_RESET, 0);
    for (int i = 0; i < REG_COUNT; i++) s_known[i] = false;
    xSemaphoreGive(s_lock);
    vTaskDelay(pdMS_TO_TICKS(10));
    return err;
}

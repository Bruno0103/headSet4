#include "board_i2c.h"

#include "esp_log.h"
#include "pinout.h"

static const char *TAG = "board_i2c";

static i2c_master_bus_handle_t s_bus;

esp_err_t board_i2c_init(void)
{
    if (s_bus) {
        return ESP_OK;
    }

    const i2c_master_bus_config_t cfg = {
        .i2c_port          = I2C_NUM_0,
        .sda_io_num        = BOARD_I2C_SDA,
        .scl_io_num        = BOARD_I2C_SCL,
        .clk_source        = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        /* O breakout do WM8960 ja tem pull-ups de 2.2k; os internos ajudam se o APDS estiver sozinho */
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&cfg, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao criar o barramento I2C: %s", esp_err_to_name(err));
        s_bus = NULL;
        return err;
    }
    ESP_LOGI(TAG, "I2C0 pronto (SDA=%d SCL=%d)", BOARD_I2C_SDA, BOARD_I2C_SCL);
    return ESP_OK;
}

i2c_master_bus_handle_t board_i2c_get_bus(void)
{
    return s_bus;
}

esp_err_t board_i2c_add_device(uint8_t addr, uint32_t scl_hz, i2c_master_dev_handle_t *out)
{
    if (!s_bus || !out) {
        return ESP_ERR_INVALID_STATE;
    }
    const i2c_device_config_t dev = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = addr,
        .scl_speed_hz    = scl_hz,
    };
    esp_err_t err = i2c_master_bus_add_device(s_bus, &dev, out);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao adicionar dispositivo 0x%02X: %s", addr, esp_err_to_name(err));
    }
    return err;
}

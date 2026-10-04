#include "apds9930.h"
#include "bt_a2dp.h"
#include "bt_avrcp.h"
#include "bt_ble.h"
#include "bt_gap.h"
#include "esp_gap_bt_api.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "audio_codec.h" // Para pegar o bus I2C
#include "driver/i2c_master.h"

#define APDS9930_I2C_ADDR 0x39
#define APDS9930_REG_ENABLE 0x80 // Command bit (0x80) | Enable (0x00)
#define APDS9930_REG_PDATAL                                                    \
  0xB8 // Auto-Increment bit (0x20) | Command bit (0x80) | PDATAL (0x18)

#define PROXIMITY_THRESHOLD 100 // Valor de exemplo

static const char *TAG = "apds9930";
static bool s_headset_on_head = true;

static i2c_master_dev_handle_t s_apds_dev = NULL;

static esp_err_t apds9930_write_reg(uint8_t reg, uint8_t data) {
  if (!s_apds_dev)
    return ESP_FAIL;
  uint8_t buf[2] = {reg, data};
  return i2c_master_transmit(s_apds_dev, buf, 2, -1);
}

static uint16_t read_proximity(void) {
  if (!s_apds_dev)
    return 0;

  uint8_t reg = APDS9930_REG_PDATAL;
  uint8_t data[2] = {0, 0};

  esp_err_t ret = i2c_master_transmit_receive(s_apds_dev, &reg, 1, data, 2, -1);
  if (ret == ESP_OK) {
    return (data[1] << 8) | data[0];
  }

  return 0;
}

void apds9930_task(void *pvParameters) {
  ESP_LOGI(TAG, "Task do sensor de proximidade APDS-9930 iniciada");

  while (1) {
    uint16_t prox_data = read_proximity();
    bool current_status = (prox_data > PROXIMITY_THRESHOLD);

    if (current_status != s_headset_on_head) {
      s_headset_on_head = current_status;

      if (!s_headset_on_head) {
        ESP_LOGW(TAG, "========================================");
        ESP_LOGW(TAG, "--- MUDANCA DE STATUS: APDS-9930 ---");
        ESP_LOGW(TAG, "GATILHO: Fone RETIRADO da cabeca");
        ESP_LOGW(TAG, "========================================");

        // Hardware Master Control: Pausa a musica se estiver tocando
        // Isso envia um AVRCP Pause para o celular/PC
        if (bt_a2dp_is_streaming()) {
          bt_avrcp_send_pause();
        }

      } else {
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "--- MUDANCA DE STATUS: APDS-9930 ---");
        ESP_LOGI(TAG, "GATILHO: Fone COLOCADO na cabeca");
        ESP_LOGI(TAG, "========================================");
      }
    }
    vTaskDelay(pdMS_TO_TICKS(500)); // Polling a cada 500ms
  }
}

esp_err_t apds9930_init(void) {

  i2c_device_config_t dev_cfg = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = APDS9930_I2C_ADDR,
      .scl_speed_hz = 100000,
  };

  esp_err_t ret = i2c_master_bus_add_device(bus, &dev_cfg, &s_apds_dev);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Falha ao adicionar dispositivo I2C APDS9930: %s",
             esp_err_to_name(ret));
    return ret;
  }

  // Configura APDS9930 para ligar o Power (PON=1) e a Proximidade (PEN=1)
  // Register ENABLE (0x00) -> Valor: 0x05
  ret = apds9930_write_reg(APDS9930_REG_ENABLE, 0x05);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Falha ao escrever no ENABLE do APDS9930: %s",
             esp_err_to_name(ret));
    return ret;
  }

  ESP_LOGI(TAG, "Sensor APDS9930 inicializado com sucesso");
  return ESP_OK;
}

bool apds9930_is_headset_on(void) { return s_headset_on_head; }

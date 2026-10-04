#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "apds9930.h"
#include "audio.h"
#include "battery.h"
#include "board_button.h"
#include "bt_core.h"
#include "headset_events.h"

static const char *TAG = "main";

static esp_err_t nvs_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS corrompida/versao nova; apagando e recriando");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_init());
    ESP_ERROR_CHECK(headset_events_init());

    /* Codec/I2S antes do Bluetooth: o stack ja pode entregar PCM assim que conectar */
    ESP_ERROR_CHECK(audio_init());

    if (bt_core_init() != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao iniciar o Bluetooth");
    }

    /* Sensores/botao so publicam eventos; o bt_link_mgr consulta o ultimo estado ao iniciar */
    if (board_button_init() != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao iniciar o botao");
    }
    if (apds9930_start() != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao iniciar o APDS-9930");
    }
    xTaskCreate(battery_task, "battery_task", 4096, NULL, 4, NULL);
}

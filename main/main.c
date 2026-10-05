#include "esp_err.h"
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
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
#include "sfx.h"

static const char *TAG = "main";

static esp_err_t nvs_init(void)
{
    /* 
     * Apaga a partição NVS inteira sempre que o ESP32 é reiniciado.
     * Isso garante que nenhum dado anterior (como pareamentos Bluetooth) permaneça armazenado.
     */
    ESP_ERROR_CHECK(nvs_flash_erase());
    
    // Inicializa a partição NVS padrão
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // Se houver algum problema de versão ou falta de espaço, tenta apagar novamente
        ESP_LOGW(TAG, "NVS corrompida/versao nova; apagando e recriando");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

static void log_memory(const char *when)
{
    ESP_LOGI(TAG, "[%s] heap interno livre=%u (min=%u, maior bloco=%u) | PSRAM livre=%u", when,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

static void memory_task(void *arg)
{
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(30000));
        log_memory("30s");
    }
}

void app_main(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    ESP_LOGI(TAG, "Chip ESP32 rev v%d.%d", chip.revision / 100, chip.revision % 100);
    ESP_ERROR_CHECK(nvs_init());
    ESP_ERROR_CHECK(headset_events_init());

    /* Codec/I2S antes do Bluetooth: o stack ja pode entregar PCM assim que conectar */
    ESP_ERROR_CHECK(audio_init());
    if (sfx_init() != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao iniciar os efeitos sonoros");
    }

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

    log_memory("boot");
    xTaskCreate(memory_task, "mem_diag", 2048, NULL, 1, NULL);
}

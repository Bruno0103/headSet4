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
#include "display.h"
#include "headset_events.h"
#include "sfx.h"

static const char *TAG = "main";

/**
 * @brief Inicializa a partição de armazenamento não-volátil (NVS).
 * 
 * Em conformidade com o WP 0.1 da arquitetura (Agente A2 - Settings/NVS):
 * - O NVS NÃO deve ser apagado incondicionalmente a cada boot, permitindo a persistência
 *   de slots Bluetooth, calibrações, equalizador (EQ) e volumes.
 * - Caso a partição esteja sem páginas livres (ESP_ERR_NVS_NO_FREE_PAGES) ou ocorra
 *   mudança de versão incompatível (ESP_ERR_NVS_NEW_VERSION_FOUND), a partição é
 *   apagada e reinicializada de forma recuperativa.
 * - Suporta a opção de Factory Reset via Kconfig (CONFIG_HEADSET_FACTORY_RESET_ON_BOOT)
 *   para testes manuais controlados.
 * 
 * @return esp_err_t ESP_OK em caso de sucesso, ou código de erro do ESP-IDF.
 */
static esp_err_t nvs_init(void)
{
#if CONFIG_HEADSET_FACTORY_RESET_ON_BOOT
    /* 
     * Factory Reset forçado via Kconfig (CONFIG_HEADSET_FACTORY_RESET_ON_BOOT):
     * Útil para testes limpos quando solicitado explicitamente pelo desenvolvedor.
     */
    ESP_LOGW(TAG, "CONFIG_HEADSET_FACTORY_RESET_ON_BOOT ativado: executando nvs_flash_erase()");
    esp_err_t erase_err = nvs_flash_erase();
    if (erase_err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao apagar NVS durante Factory Reset: %s", esp_err_to_name(erase_err));
        return erase_err;
    }
#endif

    // Tenta inicializar a partição padrão NVS
    esp_err_t err = nvs_flash_init();

    // Se não houver páginas livres ou a estrutura da partição for de nova versão incompatível, apaga e recria
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Partição NVS sem páginas livres ou versão incompatível (%s). Apagando e recriando...",
                 esp_err_to_name(err));
        
        err = nvs_flash_erase();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Falha ao apagar a partição NVS corrompida: %s", esp_err_to_name(err));
            return err;
        }

        // Reinicializa o NVS após apagar
        err = nvs_flash_init();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Falha ao reinicializar a partição NVS após formatação: %s", esp_err_to_name(err));
            return err;
        }

        ESP_LOGI(TAG, "Partição NVS recuperada e reinicializada com sucesso.");
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha inesperada ao inicializar NVS: %s", esp_err_to_name(err));
        return err;
    } else {
        ESP_LOGI(TAG, "Partição NVS inicializada com sucesso (dados persistidos preservados).");
    }

    return ESP_OK;
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

    /* Inicializa o módulo de bateria (hardware, GPIO do MOSFET e ADC1) antes de criar a task */
    if (battery_init() != ESP_OK) {
        ESP_LOGE(TAG, "Falha na inicialização do hardware de bateria; task tentará recuperação resiliente");
    }
    xTaskCreate(battery_task, "battery_task", 4096, NULL, 4, NULL);



    /* 
     * Inicializacao do Display e Interface Grafica (LVGL 9 + FreeRTOS):
     * display_init() prepara o hardware (ILI9341, XPT2046) e carrega a interface de ui.
     * Em seguida, uma tarefa dedicada no FreeRTOS (lvgl_task) com 6 KB de stack cuida do loop de desenho.
     */
    if (display_init() == ESP_OK) {
        BaseType_t task_ret = xTaskCreate(display_task, "lvgl_task", 6144, NULL, 3, NULL);
        if (task_ret != pdPASS) {
            ESP_LOGE(TAG, "Falha ao criar a tarefa FreeRTOS do LVGL (lvgl_task)");
        } else {
            ESP_LOGI(TAG, "Tarefa FreeRTOS do LVGL iniciada com sucesso");
        }
    } else {
        ESP_LOGE(TAG, "Falha ao inicializar o display/touch/UI");
    }

    log_memory("boot");
    xTaskCreate(memory_task, "mem_diag", 2048, NULL, 1, NULL);
}

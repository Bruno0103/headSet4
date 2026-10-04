#include "board_button.h"

#include <stdbool.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "headset_events.h"
#include "pinout.h"

#define DEBOUNCE_MS        25
#define SHORT_PRESS_MAX_MS 1500
#define LONG_PRESS_MS      3000

static const char *TAG = "board_button";

static QueueHandle_t s_edge_q;

/* A ISR so acorda a task; o nivel real e lido depois do debounce. */
static void IRAM_ATTR button_isr(void *arg)
{
    (void)arg;
    BaseType_t woken = pdFALSE;
    uint8_t token = 0;
    xQueueSendFromISR(s_edge_q, &token, &woken);
    if (woken) {
        portYIELD_FROM_ISR();
    }
}

static void button_task(void *arg)
{
    (void)arg;
    bool pressed = false;
    bool long_fired = false;
    int64_t t_press_us = 0;
    uint8_t token;

    for (;;) {
        TickType_t wait = portMAX_DELAY;
        if (pressed && !long_fired) {
            int64_t left_ms = LONG_PRESS_MS - (esp_timer_get_time() - t_press_us) / 1000;
            wait = left_ms > 0 ? pdMS_TO_TICKS(left_ms) : 0;
        }

        if (xQueueReceive(s_edge_q, &token, wait) == pdTRUE) {
            /* debounce: espera estabilizar, descarta bordas intermediarias e le o nivel */
            vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));
            xQueueReset(s_edge_q);
            bool low = gpio_get_level(BOARD_BUTTON_SWITCH_GPIO) == 0;

            if (low && !pressed) {
                pressed = true;
                long_fired = false;
                t_press_us = esp_timer_get_time();
            } else if (!low && pressed) {
                pressed = false;
                int64_t held_ms = (esp_timer_get_time() - t_press_us) / 1000;
                if (!long_fired && held_ms < SHORT_PRESS_MAX_MS) {
                    ESP_LOGI(TAG, "Clique curto (%lld ms) -> BUTTON_SWITCH", (long long)held_ms);
                    headset_event_post(HEADSET_EVT_BUTTON_SWITCH, NULL, 0);
                } else if (!long_fired) {
                    ESP_LOGI(TAG, "Pressao de %lld ms ignorada (entre clique e pressao longa)", (long long)held_ms);
                }
            }
        } else if (pressed && !long_fired) {
            long_fired = true;
            ESP_LOGI(TAG, "Pressao longa -> BUTTON_PAIRING");
            headset_event_post(HEADSET_EVT_BUTTON_PAIRING, NULL, 0);
        }
    }
}

esp_err_t board_button_init(void)
{
    s_edge_q = xQueueCreate(8, sizeof(uint8_t));
    if (!s_edge_q) {
        return ESP_ERR_NO_MEM;
    }

    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BOARD_BUTTON_SWITCH_GPIO,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_ANYEDGE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config: %s", esp_err_to_name(err));
        return err;
    }

    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {   /* INVALID_STATE = ja instalado */
        ESP_LOGE(TAG, "gpio_install_isr_service: %s", esp_err_to_name(err));
        return err;
    }
    err = gpio_isr_handler_add(BOARD_BUTTON_SWITCH_GPIO, button_isr, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_isr_handler_add: %s", esp_err_to_name(err));
        return err;
    }

    if (xTaskCreatePinnedToCore(button_task, "btn", 3072, NULL, 4, NULL, 0) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Botao no GPIO%d pronto", (int)BOARD_BUTTON_SWITCH_GPIO);
    return ESP_OK;
}

/*
 * board_button.c — Driver do botão físico (GPIO com pull-up, ativo em nível 0).
 *
 * Implementação resiliente por polling periódico (20 ms):
 * - Não depende de interrupção (ISR) em pino de strapping (GPIO 2 no WROVER).
 * - Debouncing preciso por integração temporal.
 * - Detecta clique curto (< 1500 ms) e clique longo (>= 3000 ms).
 * - Despacha eventos SENSOR_EVT_BUTTON_SHORT / LONG para o barramento hs_events.
 */

#include "board_button.h"

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "hs_events.h"
#include "pinout.h"

#define BUTTON_POLL_INTERVAL_MS 20
#define SHORT_PRESS_MAX_MS      1500
#define LONG_PRESS_MS           3000

#define BUTTON_TASK_PRIO        4
#define BUTTON_TASK_CORE        0
#define BUTTON_TASK_STACK       3072

static const char *TAG = "board_button";

/* Contador de eventos descartados/falhas de post */
static volatile uint32_t s_post_fail = 0;

/* Lê o nível bruto: true = pressionado (nível lógico 0 devido ao pull-up) */
static inline bool is_button_pressed(void) {
  return gpio_get_level(BOARD_BUTTON_SWITCH_GPIO) == 0;
}

/* Publica evento do botão no barramento central */
static void post_button_event(int32_t id, uint32_t duration_ms) {
  sensor_button_evt_t ev = {
      .button_id = 0,
      .duration_ms = (uint16_t)(duration_ms > UINT16_MAX ? UINT16_MAX : duration_ms),
  };
  esp_err_t err = hs_event_post(SENSOR_EVT, id, &ev, sizeof(ev));
  if (err != ESP_OK) {
    s_post_fail++;
    ESP_LOGW(TAG, "Falha ao publicar evento do botao (%s), total falhas=%lu",
             esp_err_to_name(err), (unsigned long)s_post_fail);
  }
}

static void button_task(void *arg) {
  (void)arg;

  bool was_pressed = false;
  bool long_fired = false;
  int64_t press_start_us = 0;

  ESP_LOGI(TAG, "Task de leitura do botao iniciada (Polling a cada %d ms). Pino GPIO%d nivel inicial=%d",
           BUTTON_POLL_INTERVAL_MS, (int)BOARD_BUTTON_SWITCH_GPIO, gpio_get_level(BOARD_BUTTON_SWITCH_GPIO));

  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(BUTTON_POLL_INTERVAL_MS));

    bool pressed = is_button_pressed();

    if (pressed) {
      if (!was_pressed) {
        /* Borda de descida (pressionamento inicial) */
        was_pressed = true;
        long_fired = false;
        press_start_us = esp_timer_get_time();
        ESP_LOGD(TAG, "Botao pressionado");
      } else {
        /* Continua pressionado: verifica se atingiu tempo de pressão longa */
        if (!long_fired) {
          uint32_t held_ms = (uint32_t)((esp_timer_get_time() - press_start_us) / 1000);
          if (held_ms >= LONG_PRESS_MS) {
            long_fired = true;
            ESP_LOGI(TAG, "Pressao longa detectada (%lu ms) -> BUTTON_LONG", (unsigned long)held_ms);
            post_button_event(SENSOR_EVT_BUTTON_LONG, LONG_PRESS_MS);
          }
        }
      }
    } else {
      if (was_pressed) {
        /* Borda de subida (botão solto) */
        was_pressed = false;
        uint32_t held_ms = (uint32_t)((esp_timer_get_time() - press_start_us) / 1000);

        if (long_fired) {
          /* Já tratou como botão longo enquanto estava pressionado */
          ESP_LOGD(TAG, "Botao solto apos pressao longa (%lu ms)", (unsigned long)held_ms);
        } else if (held_ms >= 40 && held_ms < SHORT_PRESS_MAX_MS) {
          /* Filtro anti-ruído mínimo de 40 ms para debounce */
          ESP_LOGI(TAG, "Clique curto detectado (%lu ms) -> BUTTON_SHORT", (unsigned long)held_ms);
          post_button_event(SENSOR_EVT_BUTTON_SHORT, held_ms);
        } else {
          ESP_LOGI(TAG, "Pressao de %lu ms ignorada", (unsigned long)held_ms);
        }
      }
    }
  }
}

esp_err_t board_button_init(void) {
  const gpio_config_t cfg = {
      .pin_bit_mask = 1ULL << BOARD_BUTTON_SWITCH_GPIO,
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE, /* Desabilita ISR para evitar travamentos em strapping pin */
  };
  esp_err_t err = gpio_config(&cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "gpio_config: %s", esp_err_to_name(err));
    return err;
  }

  if (xTaskCreatePinnedToCore(button_task, "btn", BUTTON_TASK_STACK, NULL,
                              BUTTON_TASK_PRIO, NULL,
                              BUTTON_TASK_CORE) != pdPASS) {
    ESP_LOGE(TAG, "Sem memoria para a task do botao");
    return ESP_ERR_NO_MEM;
  }

  ESP_LOGI(TAG, "Botao no GPIO%d pronto (Core %d, prio %d, polling resiliente %d ms)",
           (int)BOARD_BUTTON_SWITCH_GPIO, BUTTON_TASK_CORE, BUTTON_TASK_PRIO,
           BUTTON_POLL_INTERVAL_MS);
  return ESP_OK;
}

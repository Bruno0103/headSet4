/*
 * board_button.c — Driver do botão físico (GPIO com pull-up, ativo em nível 0).
 *
 * ============================================================================
 * POR QUE ESTE ARQUIVO FOI REESCRITO (correção do "botão travado")
 * ============================================================================
 * A versão anterior era 100% orientada a bordas:
 *   ISR -> fila -> vTaskDelay(25 ms) -> xQueueReset() -> lê o nível UMA vez.
 *
 * Problema: se o contato ainda estava quicando no instante da leitura
 * (quique > 25 ms é comum em chaves táteis gastas), a task lia "pressionado"
 * e o xQueueReset() APAGAVA a última borda real de soltura. Sem nova borda,
 * a task ficava eternamente em `pressed = true`:
 *   - após 3 s disparava um BUTTON_LONG fantasma (abria pareamento);
 *   - os cliques seguintes eram ignorados ("Pressao de N ms ignorada"),
 *     dando a impressão de que o botão tinha travado.
 *
 * Solução: debounce por AMOSTRAGEM (integrador/contador de estabilidade).
 *   - A ISR apenas acorda a task (Direct Task Notification, sem fila).
 *   - Enquanto o botão não estiver estável e solto, a task amostra o nível
 *     a cada BUTTON_SAMPLE_MS. Um novo estado só é aceito após
 *     BUTTON_STABLE_SAMPLES leituras iguais consecutivas.
 *   - O nível real do pino é a fonte da verdade — nenhuma borda perdida
 *     consegue deixar o estado inconsistente.
 *   - Em repouso (solto e estável) a task bloqueia indefinidamente na
 *     notificação: custo zero de CPU e não impede o light sleep futuro.
 * ============================================================================
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

/* Período de amostragem durante a atividade do botão. */
#define BUTTON_SAMPLE_MS 5
/* Leituras iguais consecutivas para aceitar mudança: 6 x 5 ms = 30 ms estáveis. */
#define BUTTON_STABLE_SAMPLES 6
/* Clique curto: pressão (já filtrada) menor que este valor. */
#define SHORT_PRESS_MAX_MS 1500
/* Pressão longa: dispara assim que atinge este tempo (sem esperar soltar). */
#define LONG_PRESS_MS 3000

/* Prioridade 5: acima das tasks de controle de fundo (settings/apds),
 * abaixo do BtAppTask (9). Prio 10 era excessiva para um evento humano. */
#define BUTTON_TASK_PRIO 5
#define BUTTON_TASK_CORE 0
#define BUTTON_TASK_STACK 3072

static const char *TAG = "board_button";

/* Handle da task — usado pela ISR para notificação direta. */
static TaskHandle_t s_btn_task;

/* Contador de eventos não entregues ao barramento (regra §3.7: nada de
 * descarte silencioso). Lido apenas para diagnóstico. */
static volatile uint32_t s_post_fail;

/* ---------------------------------------------------------------------------
 * ISR: apenas acorda a task. Não lê nível nem faz debounce aqui.
 * vTaskNotifyGiveFromISR é mais leve que fila e nunca "enche".
 * ------------------------------------------------------------------------- */
static void IRAM_ATTR button_isr(void *arg) {
  (void)arg;
  BaseType_t woken = pdFALSE;
  if (s_btn_task) {
    vTaskNotifyGiveFromISR(s_btn_task, &woken);
  }
  if (woken) {
    portYIELD_FROM_ISR();
  }
}

/* Lê o nível bruto: true = pressionado (pino em 0 graças ao pull-up). */
static inline bool raw_pressed(void) {
  return gpio_get_level(BOARD_BUTTON_SWITCH_GPIO) == 0;
}

/* Publica o evento do botão no barramento, contabilizando falhas. */
static void post_button(int32_t id, uint32_t duration_ms) {
  sensor_button_evt_t ev = {
      .button_id = 0,
      .duration_ms = (uint16_t)(duration_ms > UINT16_MAX ? UINT16_MAX
                                                         : duration_ms),
  };
  esp_err_t err = hs_event_post(SENSOR_EVT, id, &ev, sizeof(ev));
  if (err != ESP_OK) {
    s_post_fail++;
    ESP_LOGW(TAG, "Falha ao publicar evento do botao (%s), descartes=%lu",
             esp_err_to_name(err), (unsigned long)s_post_fail);
  }
}

static void button_task(void *arg) {
  (void)arg;

  bool stable = raw_pressed(); /* estado filtrado (aceito) */
  bool last_raw = stable;      /* última amostra bruta */
  uint8_t same_cnt = 0;        /* amostras iguais consecutivas */
  bool long_fired = false;
  int64_t t_press_us = stable ? esp_timer_get_time() : 0;

  for (;;) {
    /* Em repouso total (solto e estável) dorme até a ISR acordar.
     * Caso contrário, amostra a cada BUTTON_SAMPLE_MS. O timeout da
     * notificação faz o papel do timer de amostragem. */
    bool idle = !stable && same_cnt >= BUTTON_STABLE_SAMPLES;
    TickType_t wait = idle ? portMAX_DELAY : pdMS_TO_TICKS(BUTTON_SAMPLE_MS);
    if (wait == 0) {
      wait = 1; /* garante bloqueio mesmo com tick de 10 ms */
    }
    if (ulTaskNotifyTake(pdTRUE, wait) > 0 && idle) {
      /* Acordado por borda: reinicia a contagem de estabilidade. */
      same_cnt = 0;
    }

    /* --- integrador de estabilidade --- */
    bool r = raw_pressed();
    if (r == last_raw) {
      if (same_cnt < BUTTON_STABLE_SAMPLES) {
        same_cnt++;
      }
    } else {
      last_raw = r;
      same_cnt = 1;
    }

    /* Mudança aceita somente após N amostras iguais. */
    if (same_cnt >= BUTTON_STABLE_SAMPLES && r != stable) {
      stable = r;
      if (stable) {
        /* Borda filtrada de PRESSÃO */
        t_press_us = esp_timer_get_time();
        long_fired = false;
        ESP_LOGD(TAG, "Pressionado");
      } else {
        /* Borda filtrada de SOLTURA */
        uint32_t held_ms =
            (uint32_t)((esp_timer_get_time() - t_press_us) / 1000);
        if (long_fired) {
          /* Já reportado como longo: nada a fazer na soltura. */
        } else if (held_ms < SHORT_PRESS_MAX_MS) {
          ESP_LOGI(TAG, "Clique curto (%lu ms) -> BUTTON_SHORT",
                   (unsigned long)held_ms);
          post_button(SENSOR_EVT_BUTTON_SHORT, held_ms);
        } else {
          ESP_LOGI(TAG,
                   "Pressao de %lu ms ignorada (entre clique e pressao longa)",
                   (unsigned long)held_ms);
        }
      }
    }

    /* Pressão longa: dispara ao atingir o limiar, com o botão ainda preso. */
    if (stable && !long_fired) {
      uint32_t held_ms = (uint32_t)((esp_timer_get_time() - t_press_us) / 1000);
      if (held_ms >= LONG_PRESS_MS) {
        long_fired = true;
        ESP_LOGI(TAG, "Pressao longa -> BUTTON_LONG");
        post_button(SENSOR_EVT_BUTTON_LONG, LONG_PRESS_MS);
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
      .intr_type = GPIO_INTR_ANYEDGE,
  };
  esp_err_t err = gpio_config(&cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "gpio_config: %s", esp_err_to_name(err));
    return err;
  }

  /* A task é criada ANTES de registrar a ISR para que s_btn_task já seja
   * válido na primeira borda. */
  if (xTaskCreatePinnedToCore(button_task, "btn", BUTTON_TASK_STACK, NULL,
                              BUTTON_TASK_PRIO, &s_btn_task,
                              BUTTON_TASK_CORE) != pdPASS) {
    ESP_LOGE(TAG, "Sem memoria para a task do botao");
    return ESP_ERR_NO_MEM;
  }

  err = gpio_install_isr_service(0);
  if (err != ESP_OK &&
      err != ESP_ERR_INVALID_STATE) { /* INVALID_STATE = já instalado */
    ESP_LOGE(TAG, "gpio_install_isr_service: %s", esp_err_to_name(err));
    return err;
  }
  err = gpio_isr_handler_add(BOARD_BUTTON_SWITCH_GPIO, button_isr, NULL);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "gpio_isr_handler_add: %s", esp_err_to_name(err));
    return err;
  }

  ESP_LOGI(TAG,
           "Botao no GPIO%d pronto (Core %d, prio %d, debounce %d ms por "
           "amostragem)",
           (int)BOARD_BUTTON_SWITCH_GPIO, BUTTON_TASK_CORE, BUTTON_TASK_PRIO,
           BUTTON_SAMPLE_MS * BUTTON_STABLE_SAMPLES);
  return ESP_OK;
}

#include "bt_app_core.h"
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

static const char *TAG = "bt_app_core";

typedef struct {
    bt_app_cb_t p_cback;
    uint16_t event;
    void *p_params;
    int param_len;
    bt_app_copy_cb_t p_copy_cback;
    bt_app_free_cb_t p_free_cback;
} bt_app_msg_t;

static QueueHandle_t s_bt_app_task_queue = NULL;
static TaskHandle_t s_bt_app_task_handle = NULL;

static void bt_app_task_handler(void *arg) {
    bt_app_msg_t msg;
    for (;;) {
        if (pdTRUE == xQueueReceive(s_bt_app_task_queue, &msg, portMAX_DELAY)) {
            ESP_LOGD(TAG, "%s, sig 0x%x, 0x%x", __func__, msg.event, msg.event);
            if (msg.p_cback) {
                msg.p_cback(msg.event, msg.p_params);
            }
            if (msg.p_free_cback && msg.p_params) {
                msg.p_free_cback(msg.p_params);
            }
            if (msg.p_params) {
                free(msg.p_params);
            }
        }
    }
}

void bt_app_task_start_up(void) {
    if (s_bt_app_task_queue == NULL) {
        s_bt_app_task_queue = xQueueCreate(20, sizeof(bt_app_msg_t));
    }
    if (s_bt_app_task_handle == NULL) {
        /* Stack maior: os handlers de GAP/GATT/Fast Pair (PSA, ECDH) rodam aqui. Core 0 = core do BT. */
        xTaskCreatePinnedToCore(bt_app_task_handler, "BtAppTask", 8192, NULL, 10, &s_bt_app_task_handle, 0);
    }
}

void bt_app_task_shut_down(void) {
    if (s_bt_app_task_handle) {
        vTaskDelete(s_bt_app_task_handle);
        s_bt_app_task_handle = NULL;
    }
    if (s_bt_app_task_queue) {
        vQueueDelete(s_bt_app_task_queue);
        s_bt_app_task_queue = NULL;
    }
}

bool bt_app_work_dispatch(bt_app_cb_t p_cback, uint16_t event, void *p_params, int param_len, bt_app_copy_cb_t p_copy_cback, bt_app_free_cb_t p_free_cback) {
    ESP_LOGD(TAG, "%s event 0x%x, param len %d", __func__, event, param_len);

    bt_app_msg_t msg;
    msg.p_cback = p_cback;
    msg.event = event;
    msg.param_len = param_len;
    msg.p_copy_cback = p_copy_cback;
    msg.p_free_cback = p_free_cback;
    msg.p_params = NULL;

    if (param_len > 0) {
        msg.p_params = malloc(param_len);
        if (msg.p_params == NULL) {
            ESP_LOGE(TAG, "%s falha ao alocar memoria para os parametros", __func__);
            return false;
        }
        if (p_copy_cback) {
            p_copy_cback(msg.p_params, p_params, param_len);
        } else {
            memcpy(msg.p_params, p_params, param_len);
        }
    }

    if (xQueueSend(s_bt_app_task_queue, &msg, 10 / portTICK_PERIOD_MS) != pdTRUE) {
        ESP_LOGE(TAG, "%s falha ao enviar na fila", __func__);
        if (msg.p_params) {
            free(msg.p_params);
        }
        return false;
    }
    return true;
}

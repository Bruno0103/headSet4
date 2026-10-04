#include "headset_events.h"

#include "esp_log.h"

static const char *TAG = "hs_events";

ESP_EVENT_DEFINE_BASE(HEADSET_EVENT);

static esp_event_loop_handle_t s_loop;
static volatile int8_t s_worn = -1;   /* -1 desconhecido, 0 retirado, 1 colocado */

esp_err_t headset_events_init(void)
{
    if (s_loop) {
        return ESP_OK;
    }
    const esp_event_loop_args_t args = {
        .queue_size      = 32,
        .task_name       = "hs_evt",
        .task_priority   = 5,
        .task_stack_size = 4096,
        .task_core_id    = 0,    /* core do BT; o core 1 fica livre para o audio */
    };
    esp_err_t err = esp_event_loop_create(&args, &s_loop);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao criar o loop de eventos: %s", esp_err_to_name(err));
        s_loop = NULL;
    } else {
        ESP_LOGI(TAG, "Loop de eventos HEADSET_EVENT criado");
    }
    return err;
}

esp_err_t headset_event_post(headset_event_id_t id, const void *data, size_t size)
{
    if (id == HEADSET_EVT_WORN) {
        s_worn = 1;
    } else if (id == HEADSET_EVT_REMOVED) {
        s_worn = 0;
    }
    if (!s_loop) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = esp_event_post_to(s_loop, HEADSET_EVENT, id, data, size, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Evento %d descartado: %s", (int)id, esp_err_to_name(err));
    }
    return err;
}

bool headset_events_get_worn(bool *worn)
{
    if (s_worn < 0) {
        return false;
    }
    *worn = s_worn == 1;
    return true;
}

esp_err_t headset_event_register(headset_event_id_t id, esp_event_handler_t handler, void *arg)
{
    if (!s_loop) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_event_handler_register_with(s_loop, HEADSET_EVENT, id, handler, arg);
}

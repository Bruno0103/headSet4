/**
 * @file headset_events.c
 * @brief Shim de compatibilidade para o barramento legado HEADSET_EVENT.
 *
 * Utiliza internamente o loop central de hs_events (ctrl loop) e repassa os eventos
 * para que os consumidores antigos continuem funcionando de maneira transparente.
 */

#include "headset_events.h"
#include "hs_events.h"
#include "esp_log.h"

static const char *TAG = "hs_events_shim";

/* Mantém a definição da base legada para esp_event */
ESP_EVENT_DEFINE_BASE(HEADSET_EVENT);

static volatile int8_t s_worn = -1;   /* -1 desconhecido, 0 retirado, 1 colocado */

esp_err_t headset_events_init(void)
{
    /* Delega a inicialização para o novo barramento central hs_events */
    return hs_events_init();
}

esp_err_t headset_event_post(headset_event_id_t id, const void *data, size_t size)
{
    if (id == HEADSET_EVT_WORN) {
        s_worn = 1;
    } else if (id == HEADSET_EVT_REMOVED) {
        s_worn = 0;
    }

    esp_event_loop_handle_t loop = hs_events_get_loop();
    if (!loop) {
        ESP_LOGE(TAG, "headset_event_post chamado antes de headset_events_init()");
        return ESP_ERR_INVALID_STATE;
    }

    /* 1. Publica na base legada HEADSET_EVENT para assinantes antigos */
    esp_err_t err = esp_event_post_to(loop, HEADSET_EVENT, id, data, size, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Evento legado %d descartado: %s", (int)id, esp_err_to_name(err));
    }

    /* 2. Publica também na nova base correspondente para novos assinantes */
    switch (id) {
    case HEADSET_EVT_WORN:
        (void)hs_event_post(SENSOR_EVT, SENSOR_EVT_WORN, NULL, 0);
        break;
    case HEADSET_EVT_REMOVED:
        (void)hs_event_post(SENSOR_EVT, SENSOR_EVT_REMOVED, NULL, 0);
        break;
    case HEADSET_EVT_BUTTON_SWITCH: {
        sensor_button_evt_t btn_ev = { .button_id = 0, .duration_ms = 50 };
        (void)hs_event_post(SENSOR_EVT, SENSOR_EVT_BUTTON_SHORT, &btn_ev, sizeof(btn_ev));
        break;
    }
    case HEADSET_EVT_BUTTON_PAIRING: {
        sensor_button_evt_t btn_ev = { .button_id = 0, .duration_ms = 1500 };
        (void)hs_event_post(SENSOR_EVT, SENSOR_EVT_BUTTON_LONG, &btn_ev, sizeof(btn_ev));
        break;
    }
    case HEADSET_EVT_LINK_UP:
        if (data && size >= sizeof(headset_link_evt_t)) {
            const headset_link_evt_t *old_ev = (const headset_link_evt_t *)data;
            bt_link_evt_t new_ev = {
                .slot = 0,
                .profile = (bt_profile_mask_t)old_ev->profile,
            };
            memcpy(new_ev.bda, old_ev->bda, sizeof(esp_bd_addr_t));
            (void)hs_event_post(BT_EVT, BT_EVT_LINK_UP, &new_ev, sizeof(new_ev));
        }
        break;
    case HEADSET_EVT_LINK_DOWN:
        if (data && size >= sizeof(headset_link_evt_t)) {
            const headset_link_evt_t *old_ev = (const headset_link_evt_t *)data;
            bt_link_evt_t new_ev = {
                .slot = 0,
                .profile = (bt_profile_mask_t)old_ev->profile,
            };
            memcpy(new_ev.bda, old_ev->bda, sizeof(esp_bd_addr_t));
            (void)hs_event_post(BT_EVT, BT_EVT_LINK_DOWN, &new_ev, sizeof(new_ev));
        }
        break;
    case HEADSET_EVT_PAIRING_MODE:
        if (data && size >= sizeof(headset_pairing_evt_t)) {
            const headset_pairing_evt_t *old_ev = (const headset_pairing_evt_t *)data;
            bt_pairing_evt_t new_ev = {
                .active = old_ev->active,
                .timeout_sec = 120,
            };
            (void)hs_event_post(BT_EVT, BT_EVT_PAIRING_MODE, &new_ev, sizeof(new_ev));
        }
        break;
    case HEADSET_EVT_STREAMING:
        if (data && size >= sizeof(headset_streaming_evt_t)) {
            const headset_streaming_evt_t *old_ev = (const headset_streaming_evt_t *)data;
            bt_streaming_evt_t new_ev = {
                .slot = 0,
                .streaming = old_ev->streaming,
            };
            (void)hs_event_post(BT_EVT, BT_EVT_STREAMING, &new_ev, sizeof(new_ev));
        }
        break;
    case HEADSET_EVT_BATTERY:
        if (data && size >= sizeof(headset_battery_evt_t)) {
            const headset_battery_evt_t *old_ev = (const headset_battery_evt_t *)data;
            sensor_battery_evt_t new_ev = {
                .percent = old_ev->percent,
                .millivolts = old_ev->millivolts,
                .is_charging = false,
            };
            (void)hs_event_post(SENSOR_EVT, SENSOR_EVT_BATTERY, &new_ev, sizeof(new_ev));
        }
        break;
    default:
        break;
    }

    return err;
}

bool headset_events_get_worn(bool *worn)
{
    if (s_worn < 0) {
        return false;
    }
    *worn = (s_worn == 1);
    return true;
}

esp_err_t headset_event_register(headset_event_id_t id, esp_event_handler_t handler, void *arg)
{
    esp_event_loop_handle_t loop = hs_events_get_loop();
    if (!loop) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_event_handler_register_with(loop, HEADSET_EVENT, id, handler, arg);
}

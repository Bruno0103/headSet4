/**
 * @file hs_events.c
 * @brief Implementação do barramento central de eventos dedicado (ctrl loop) e bases de eventos.
 *
 * Em conformidade com AGENTS.md e ESP-IDF v6.x:
 * - Define as bases de eventos SENSOR_EVT, BT_EVT, AUDIO_EVT e CFG_EVT via ESP_EVENT_DEFINE_BASE.
 * - Mantém um laço dedicado com task no Core 0, prioridade 5, fila >= 48 itens.
 * - Fornece APIs seguras sem bloqueio indefinido (timeout 0 ticks).
 */

#include "hs_events.h"

#include "esp_log.h"

static const char *TAG = "hs_events";

/* ============================================================================
 * DEFINIÇÃO DAS BASES DE EVENTOS (ESP_EVENT_DEFINE_BASE)
 * ============================================================================ */

ESP_EVENT_DEFINE_BASE(SENSOR_EVT);
ESP_EVENT_DEFINE_BASE(BT_EVT);
ESP_EVENT_DEFINE_BASE(AUDIO_EVT);
ESP_EVENT_DEFINE_BASE(CFG_EVT);

/* Handle do loop de eventos central */
static esp_event_loop_handle_t s_ctrl_loop = NULL;

esp_err_t hs_events_init(void)
{
    if (s_ctrl_loop != NULL) {
        return ESP_OK; /* Idempotente */
    }

    const esp_event_loop_args_t args = {
        .queue_size      = 48,          /* Conforme AGENTS.md §4.1 (queue_size >= 48) */
        .task_name       = "hs_ctrl_evt",
        .task_priority   = 5,           /* Prioridade 5 */
        .task_stack_size = 4096,        /* 4 KB de stack */
        .task_core_id    = 0,           /* Core 0 (livre para BT/controle; áudio no Core 1) */
    };

    esp_err_t err = esp_event_loop_create(&args, &s_ctrl_loop);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao criar o loop de eventos central hs_ctrl_evt: %s", esp_err_to_name(err));
        s_ctrl_loop = NULL;
        return err;
    }

    ESP_LOGI(TAG, "Loop de eventos central inicializado com sucesso (Core 0, prio 5, queue 48)");
    return ESP_OK;
}

esp_event_loop_handle_t hs_events_get_loop(void)
{
    return s_ctrl_loop;
}

esp_err_t hs_event_post(esp_event_base_t base, int32_t id, const void *data, size_t size)
{
    if (!s_ctrl_loop) {
        ESP_LOGE(TAG, "hs_event_post chamado antes de hs_events_init()");
        return ESP_ERR_INVALID_STATE;
    }

    /* Regra do projeto: timeout 0 ticks (não bloqueia). Cópia gerenciada pelo esp_event. */
    esp_err_t err = esp_event_post_to(s_ctrl_loop, base, id, data, size, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Evento descartado [base=%s, id=%ld]: %s",
                 base ? base : "UNKNOWN", (long)id, esp_err_to_name(err));
    }
    return err;
}

esp_err_t hs_event_register(esp_event_base_t base, int32_t id, esp_event_handler_t handler, void *arg)
{
    if (!s_ctrl_loop) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_event_handler_register_with(s_ctrl_loop, base, id, handler, arg);
}

esp_err_t hs_event_unregister(esp_event_base_t base, int32_t id, esp_event_handler_t handler)
{
    if (!s_ctrl_loop) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_event_handler_unregister_with(s_ctrl_loop, base, id, handler);
}

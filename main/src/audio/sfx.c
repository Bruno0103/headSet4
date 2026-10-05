#include "sfx.h"

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "audio_io.h"

static const char *TAG = "sfx";

static const audio_tone_t k_worn[]         = { { 880, 70 }, { 0, 40 }, { 1175, 110 }, { 0, 100 } };
static const audio_tone_t k_removed[]      = { { 1175, 70 }, { 0, 40 }, { 880, 110 }, { 0, 100 } };
static const audio_tone_t k_connected[]    = { { 523, 80 }, { 659, 80 }, { 784, 80 }, { 1047, 160 }, { 0, 100 } };
static const audio_tone_t k_disconnected[] = { { 1047, 80 }, { 784, 80 }, { 659, 80 }, { 523, 160 }, { 0, 100 } };

static const struct {
    const audio_tone_t *seq;
    size_t              len;
} k_sfx[] = {
    [SFX_WORN]         = { k_worn,         sizeof k_worn / sizeof k_worn[0] },
    [SFX_REMOVED]      = { k_removed,      sizeof k_removed / sizeof k_removed[0] },
    [SFX_CONNECTED]    = { k_connected,    sizeof k_connected / sizeof k_connected[0] },
    [SFX_DISCONNECTED] = { k_disconnected, sizeof k_disconnected / sizeof k_disconnected[0] },
};

static QueueHandle_t s_q;

static void sfx_task(void *arg)
{
    (void)arg;
    sfx_id_t id;
    for (;;) {
        if (xQueueReceive(s_q, &id, portMAX_DELAY) != pdTRUE) continue;
        if (audio_io_play_tones(k_sfx[id].seq, k_sfx[id].len) != ESP_OK) {
            ESP_LOGW(TAG, "efeito %d nao pode tocar agora", id);
            continue;
        }
        /* Serializa: espera este efeito acabar (ou o audio ser parado) antes do proximo */
        for (int i = 0; i < 300 && audio_io_tones_busy(); i++) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

void sfx_play(sfx_id_t id)
{
    if (s_q) xQueueSend(s_q, &id, 0);
}

esp_err_t sfx_init(void)
{
    s_q = xQueueCreate(4, sizeof(sfx_id_t));
    ESP_RETURN_ON_FALSE(s_q, ESP_ERR_NO_MEM, TAG, "fila");
    ESP_RETURN_ON_FALSE(xTaskCreate(sfx_task, "sfx", 3072, NULL, 3, NULL) == pdPASS, ESP_ERR_NO_MEM, TAG, "task");
    return ESP_OK;
}

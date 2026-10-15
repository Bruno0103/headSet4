#include "sfx.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "audio_io.h"
#include "hs_events.h"

static const char *TAG = "sfx";

/* ============================================================================
 * TABELAS DE TONS E SEQUÊNCIAS MUSICAIS (UX SOUND DESIGN PREMIUM)
 *
 * Cada nota possui frequência em Hz e duração em milissegundos.
 * O sintetizador em audio_io.c processa síntese aditiva multi-harmônica com
 * decaimento percussivo orgânico (e^-1.8*t/T), produzindo sonoridade cristalina
 * de carrilhão de vidro / marimba fina sem aspereza robótica.
 *
 * As durações foram concebidas para serem extremamente rápidas (~100 a 180 ms),
 * garantindo respostas instantâneas que não travam o uso do fone.
 * ============================================================================ */

/* 1. Conexão Bluetooth (SFX_CONNECTED):
 * Intervalo harmônico ascendente cintilante (Quarta Justa: Sol 5 -> Dó 6).
 * Rápido, alegre e positivo! Duração total: ~180 ms. */
static const audio_tone_t k_connected[] = {
    { 784,  50 },  /* G5: 784 Hz, 50 ms (ataque inicial límpido) */
    { 1047, 110 }, /* C6: 1047 Hz, 110 ms (resolução harmônica cristalina) */
    { 0,    20 },  /* Micro-cauda de 20 ms para liberação suave do DMA */
};

/* 2. Desconexão Bluetooth (SFX_DISCONNECTED):
 * Intervalo harmônico descendente aveludado (Dó 6 -> Sol 5).
 * Discreto, rápido e elegante sem sobressaltos. Duração total: ~160 ms. */
static const audio_tone_t k_disconnected[] = {
    { 1047, 45 },  /* C6: 1047 Hz, 45 ms */
    { 784,  95 },  /* G5: 784 Hz, 95 ms (decaimento aveludado macio) */
    { 0,    20 },  /* Micro-cauda de flush DMA */
};

/* 3. Fone Colocado na Cabeça (SFX_WORN):
 * "Crystal Water Drop" / Detecção de presença na cabeça (Si 5 -> Mi 6).
 * Resposta sonora ultra-rápida (estilo AirPods). Duração total: ~130 ms. */
static const audio_tone_t k_worn[] = {
    { 988,  35 },  /* B5: 988 Hz, 35 ms */
    { 1319, 75 },  /* E6: 1319 Hz, 75 ms (gota d'água percussiva) */
    { 0,    20 },  /* Micro-cauda de flush DMA */
};

/* 4. Fone Retirado da Cabeça (SFX_REMOVED):
 * Tom aveludado e relaxante de repouso (Mi 5).
 * Duração total: ~90 ms. */
static const audio_tone_t k_removed[] = {
    { 659,  70 },  /* E5: 659 Hz, 70 ms (decaimento macio) */
    { 0,    20 },  /* Micro-cauda de flush DMA */
};

/* 5. Modo de Pareamento Ativado (SFX_PAIRING):
 * Tríade ascendente suave indicando busca (Ré 5 -> Sol 5 -> Si 5).
 * Duração total: ~160 ms. */
static const audio_tone_t k_pairing[] = {
    { 587,  30 },  /* D5: 587 Hz, 30 ms */
    { 784,  30 },  /* G5: 784 Hz, 30 ms */
    { 988,  80 },  /* B5: 988 Hz, 80 ms */
    { 0,    20 },  /* Micro-cauda de flush DMA */
};

/* 6. Alerta de Bateria Fraca (SFX_BATTERY_LOW):
 * Duplo aviso acústico discreto (Mi 5 -> Dó 5).
 * Duração total: ~135 ms. */
static const audio_tone_t k_battery_low[] = {
    { 659,  45 },  /* E5: 659 Hz, 45 ms */
    { 523,  70 },  /* C5: 523 Hz, 70 ms */
    { 0,    20 },  /* Micro-cauda de flush DMA */
};

/* 7. Limite de Volume Mínimo ou Máximo (SFX_VOLUME_LIMIT):
 * Micro-tick percussivo refinado (Mi 6).
 * Duração total: ~40 ms. */
static const audio_tone_t k_volume_limit[] = {
    { 1319, 25 },  /* E6: 1319 Hz, 25 ms */
    { 0,    15 },  /* Micro-cauda de flush DMA */
};

static const struct {
    const audio_tone_t *seq;
    size_t              len;
} k_sfx[SFX_MAX] = {
    [SFX_WORN]         = { k_worn,         sizeof k_worn / sizeof k_worn[0] },
    [SFX_REMOVED]      = { k_removed,      sizeof k_removed / sizeof k_removed[0] },
    [SFX_CONNECTED]    = { k_connected,    sizeof k_connected / sizeof k_connected[0] },
    [SFX_DISCONNECTED] = { k_disconnected, sizeof k_disconnected / sizeof k_disconnected[0] },
    [SFX_PAIRING]      = { k_pairing,      sizeof k_pairing / sizeof k_pairing[0] },
    [SFX_BATTERY_LOW]  = { k_battery_low,  sizeof k_battery_low / sizeof k_battery_low[0] },
    [SFX_VOLUME_LIMIT] = { k_volume_limit, sizeof k_volume_limit / sizeof k_volume_limit[0] },
};

static QueueHandle_t s_q;

/* ============================================================================
 * TAREFA DEDICADA DE SERIALIZAÇÃO DE EFEITOS SONOROS (Prioridade 3)
 * ============================================================================ */

static void sfx_task(void *arg)
{
    (void)arg;
    sfx_id_t id;
    for (;;) {
        if (xQueueReceive(s_q, &id, portMAX_DELAY) != pdTRUE) continue;

        if (id >= SFX_MAX || !k_sfx[id].seq) {
            ESP_LOGW(TAG, "ID de efeito invalido: %d", id);
            continue;
        }

        if (audio_io_play_tones(k_sfx[id].seq, k_sfx[id].len) != ESP_OK) {
            ESP_LOGW(TAG, "Efeito sonoro %d nao pode tocar agora", id);
            continue;
        }

        /* Serializa: aguarda o término do efeito atual com timeout defensivo de 1,5 s */
        for (int i = 0; i < 150 && audio_io_tones_busy(); i++) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

/* ============================================================================
 * TRATAMENTO DE EVENTOS DO BARRAMENTO CENTRAL (hs_events)
 *
 * Handler rápido (< 2 ms de execução, sem operações bloqueantes).
 * Inclui debounce temporal inteligente contra duplicações de eventos Bluetooth:
 * Quando um celular conecta, a pilha emite BT_EVT_LINK_UP para A2DP e HFP quase
 * simultaneamente. O debounce assegura um único toque polido e imediato.
 * ============================================================================ */

static void sfx_event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    (void)handler_args;

    if (base == SENSOR_EVT) {
        if (id == SENSOR_EVT_WORN) {
            sfx_play(SFX_WORN);
        } else if (id == SENSOR_EVT_REMOVED) {
            sfx_play(SFX_REMOVED);
        } else if (id == SENSOR_EVT_BATTERY && event_data) {
            const sensor_battery_evt_t *bat = (const sensor_battery_evt_t *)event_data;
            static bool s_low_bat_warned = false;
            /* Alerta de bateria fraca quando <= 15% e não estiver carregando */
            if (bat->percent <= 15 && !bat->is_charging) {
                if (!s_low_bat_warned) {
                    s_low_bat_warned = true;
                    sfx_play(SFX_BATTERY_LOW);
                }
            } else if (bat->percent > 20) {
                /* Histerese para rearme do aviso */
                s_low_bat_warned = false;
            }
        }
    } else if (base == BT_EVT) {
        static int64_t s_last_link_up_us = 0;
        static int64_t s_last_link_down_us = 0;
        int64_t now_us = esp_timer_get_time();

        if (id == BT_EVT_LINK_UP) {
            /* Debounce de 1200 ms: absorve eventos consecutivos de múltiplos perfis (A2DP + HFP + AVRCP) */
            if ((now_us - s_last_link_up_us) > 1200000LL) {
                s_last_link_up_us = now_us;
                sfx_play(SFX_CONNECTED);
            }
        } else if (id == BT_EVT_LINK_DOWN) {
            /* Debounce de 1200 ms para desconexões em cascata */
            if ((now_us - s_last_link_down_us) > 1200000LL) {
                s_last_link_down_us = now_us;
                sfx_play(SFX_DISCONNECTED);
            }
        } else if (id == BT_EVT_PAIRING_MODE && event_data) {
            const bt_pairing_evt_t *pairing = (const bt_pairing_evt_t *)event_data;
            if (pairing->active) {
                sfx_play(SFX_PAIRING);
            }
        }
    }
}

/* ============================================================================
 * INTERFACE PÚBLICA DO MÓDULO SFX
 * ============================================================================ */

void sfx_play(sfx_id_t id)
{
    if (s_q && id < SFX_MAX) {
        xQueueSend(s_q, &id, 0);
    }
}

esp_err_t sfx_init(void)
{
    s_q = xQueueCreate(6, sizeof(sfx_id_t));
    ESP_RETURN_ON_FALSE(s_q, ESP_ERR_NO_MEM, TAG, "Falha ao alocar fila de SFX");

    BaseType_t r = xTaskCreate(sfx_task, "sfx", 3072, NULL, 3, NULL);
    ESP_RETURN_ON_FALSE(r == pdPASS, ESP_ERR_NO_MEM, TAG, "Falha ao criar task sfx");

    /* Assinatura no barramento oficial hs_events com handlers não-bloqueantes */
    hs_event_register(SENSOR_EVT, SENSOR_EVT_WORN, sfx_event_handler, NULL);
    hs_event_register(SENSOR_EVT, SENSOR_EVT_REMOVED, sfx_event_handler, NULL);
    hs_event_register(SENSOR_EVT, SENSOR_EVT_BATTERY, sfx_event_handler, NULL);

    hs_event_register(BT_EVT, BT_EVT_LINK_UP, sfx_event_handler, NULL);
    hs_event_register(BT_EVT, BT_EVT_LINK_DOWN, sfx_event_handler, NULL);
    hs_event_register(BT_EVT, BT_EVT_PAIRING_MODE, sfx_event_handler, NULL);

    ESP_LOGI(TAG, "Modulo de efeitos sonoros (SFX) inicializado com sucesso (7 efeitos refinados)");
    return ESP_OK;
}


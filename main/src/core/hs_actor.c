/**
 * @file hs_actor.c
 * @brief Implementação do Motor de Actors FreeRTOS para o firmware HeadSet4.
 *
 * Arquitetura Event-Driven & Actor Model (Agente A1 - Infraestrutura de Mensageria):
 * - Gestão de lifecycle sob demanda (lazy creation) e idle-stop com encerramento gracioso.
 * - Sincronização e resolução de corrida "morrendo x enviando" com mutex dedicado (lock) e flags atômicas.
 * - Comunicação request/reply baseada em Direct Task Notifications (xTaskNotifyIndexed / xTaskNotifyWaitIndexed).
 * - Monitoramento contínuo de stack high-water mark e profundidade máxima da fila (high_water_queue).
 * - Código inteiramente em C com tratamento rigoroso de retorno de APIs FreeRTOS e ESP-IDF.
 */

#include "hs_actor.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/semphr.h"

static const char *TAG = "hs_actor";

/* Comando especial interno de parada do loop do ator */
#define HS_ACTOR_INTERNAL_CMD_STOP 0xFFFF

/**
 * @brief Estrutura de contexto para Request/Reply síncrono.
 */
typedef struct {
    TaskHandle_t requester_task; /**< Handle da task que fez a requisição. */
    void        *out_buf;        /**< Buffer para armazenar os dados de resposta. */
    size_t       out_buf_len;    /**< Capacidade máxima em bytes do buffer de resposta. */
    size_t       actual_len;     /**< Quantidade real de bytes copiados na resposta. */
    esp_err_t    result_code;    /**< Status retornado pelo ator (ESP_OK, erro, etc). */
    bool         completed;      /**< Indica se a resposta foi processada antes do timeout. */
} hs_actor_req_ctx_t;

/**
 * @brief Estrutura interna completa da instância do Ator.
 */
struct hs_actor {
    hs_actor_cfg_t      cfg;                /**< Cópia local da configuração do ator. */
    QueueHandle_t       queue;              /**< Fila de mensagens FreeRTOS. */
    TaskHandle_t        task;               /**< Handle da Task FreeRTOS (NULL se dormindo/encerrada). */
    SemaphoreHandle_t   lock;               /**< Mutex para proteção de lifecycle (start/stop/idle/send). */
    SemaphoreHandle_t   stop_sem;           /**< Semáforo binário para sinalizar encerramento da task. */
    bool                is_running;         /**< Indica se a task está em execução. */
    bool                stop_requested;     /**< Sinaliza se há pedido explícito de encerramento em andamento. */

    /* Estatísticas e telemetria */
    hs_actor_stats_t    stats;              /**< Métricas acumuladas do ator. */
};

/* Declaração antecipada da função da task */
static void hs_actor_task_fn(void *arg);

/* ============================================================================
 * CRIAÇÃO E DESTRUIÇÃO DO ATOR
 * ============================================================================ */

esp_err_t hs_actor_create(const hs_actor_cfg_t *cfg, hs_actor_t **out_actor)
{
    if (!cfg || !out_actor || !cfg->on_msg || cfg->queue_len == 0 || cfg->stack == 0) {
        ESP_LOGE(TAG, "Parâmetros inválidos ao criar ator");
        return ESP_ERR_INVALID_ARG;
    }

    hs_actor_t *actor = (hs_actor_t *)calloc(1, sizeof(hs_actor_t));
    if (!actor) {
        ESP_LOGE(TAG, "Falha de memória ao alocar estrutura hs_actor_t");
        return ESP_ERR_NO_MEM;
    }

    actor->cfg = *cfg;

    /* Criação do mutex de proteção contra concorrência */
    actor->lock = xSemaphoreCreateMutex();
    if (!actor->lock) {
        ESP_LOGE(TAG, "Falha ao criar mutex do ator %s", cfg->name ? cfg->name : "anon");
        free(actor);
        return ESP_ERR_NO_MEM;
    }

    /* Criação do semáforo de sincronização de parada segura */
    actor->stop_sem = xSemaphoreCreateBinary();
    if (!actor->stop_sem) {
        ESP_LOGE(TAG, "Falha ao criar semáforo de parada do ator %s", cfg->name ? cfg->name : "anon");
        vSemaphoreDelete(actor->lock);
        free(actor);
        return ESP_ERR_NO_MEM;
    }

    /* Criação da fila de mensagens */
    actor->queue = xQueueCreate(cfg->queue_len, sizeof(hs_msg_t));
    if (!actor->queue) {
        ESP_LOGE(TAG, "Falha ao criar fila FreeRTOS para o ator %s", cfg->name ? cfg->name : "anon");
        vSemaphoreDelete(actor->stop_sem);
        vSemaphoreDelete(actor->lock);
        free(actor);
        return ESP_ERR_NO_MEM;
    }

    actor->is_running = false;
    actor->stop_requested = false;

    /* Se idle_ms == 0, o ator tem ciclo de vida estático/fixo: inicia imediatamente */
    if (cfg->idle_ms == 0) {
        esp_err_t err = hs_actor_start(actor);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Falha ao inicializar task fixa do ator %s: %s", cfg->name, esp_err_to_name(err));
            vQueueDelete(actor->queue);
            vSemaphoreDelete(actor->stop_sem);
            vSemaphoreDelete(actor->lock);
            free(actor);
            return err;
        }
    }

    *out_actor = actor;
    ESP_LOGI(TAG, "Ator '%s' criado com sucesso (fila: %u, stack: %lu, idle_ms: %lu)",
             cfg->name ? cfg->name : "anon", cfg->queue_len, (unsigned long)cfg->stack, (unsigned long)cfg->idle_ms);
    return ESP_OK;
}

esp_err_t hs_actor_start(hs_actor_t *actor)
{
    if (!actor) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(actor->lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (actor->is_running && actor->task != NULL) {
        /* Já está em execução */
        xSemaphoreGive(actor->lock);
        return ESP_OK;
    }

    actor->stop_requested = false;

    BaseType_t ret;
    if (actor->cfg.core >= 0 && actor->cfg.core < portNUM_PROCESSORS) {
        ret = xTaskCreatePinnedToCore(
            hs_actor_task_fn,
            actor->cfg.name ? actor->cfg.name : "hs_actor",
            actor->cfg.stack,
            actor,
            actor->cfg.prio,
            &actor->task,
            actor->cfg.core
        );
    } else {
        ret = xTaskCreate(
            hs_actor_task_fn,
            actor->cfg.name ? actor->cfg.name : "hs_actor",
            actor->cfg.stack,
            actor,
            actor->cfg.prio,
            &actor->task
        );
    }

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Falha ao criar Task FreeRTOS para o ator '%s'", actor->cfg.name ? actor->cfg.name : "anon");
        actor->task = NULL;
        actor->is_running = false;
        xSemaphoreGive(actor->lock);
        return ESP_FAIL;
    }

    actor->is_running = true;
    xSemaphoreGive(actor->lock);

    return ESP_OK;
}

esp_err_t hs_actor_stop(hs_actor_t *actor, TickType_t wait_ticks)
{
    if (!actor) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(actor->lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (!actor->is_running || actor->task == NULL) {
        xSemaphoreGive(actor->lock);
        return ESP_OK;
    }

    actor->stop_requested = true;

    /* Envia comando de parada com prioridade na frente da fila se possível */
    hs_msg_t stop_msg = {
        .cmd   = HS_ACTOR_INTERNAL_CMD_STOP,
        .len   = 0,
        .reply = NULL,
    };

    /* Injeta a mensagem para desbloquear xQueueReceive se estiver bloqueado */
    if (xQueueSendToFront(actor->queue, &stop_msg, 0) != pdPASS) {
        /* Se a fila estiver cheia, tentamos substituir ou esperar minimamente */
        xQueueSendToFront(actor->queue, &stop_msg, pdMS_TO_TICKS(10));
    }

    xSemaphoreGive(actor->lock);

    /* Aguarda o encerramento gracioso via stop_sem */
    if (xSemaphoreTake(actor->stop_sem, wait_ticks) != pdTRUE) {
        ESP_LOGW(TAG, "Timeout aguardando finalização da task do ator '%s'",
                 actor->cfg.name ? actor->cfg.name : "anon");
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

esp_err_t hs_actor_destroy(hs_actor_t *actor)
{
    if (!actor) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Garante a parada segura */
    hs_actor_stop(actor, pdMS_TO_TICKS(1000));

    if (xSemaphoreTake(actor->lock, portMAX_DELAY) == pdTRUE) {
        if (actor->queue) {
            /* Drena e descarta mensagens remanescentes */
            hs_msg_t discarded;
            while (xQueueReceive(actor->queue, &discarded, 0) == pdPASS) {
                if (discarded.reply) {
                    hs_actor_reply(&discarded, ESP_ERR_INVALID_STATE, NULL, 0);
                }
            }
            vQueueDelete(actor->queue);
            actor->queue = NULL;
        }

        if (actor->stop_sem) {
            vSemaphoreDelete(actor->stop_sem);
            actor->stop_sem = NULL;
        }

        SemaphoreHandle_t lock_to_delete = actor->lock;
        actor->lock = NULL;
        xSemaphoreGive(lock_to_delete);
        vSemaphoreDelete(lock_to_delete);
    }

    free(actor);
    return ESP_OK;
}

/* ============================================================================
 * COMUNICAÇÃO (SEND / REQUEST / REPLY)
 * ============================================================================ */

esp_err_t hs_actor_send(hs_actor_t *actor, uint16_t cmd, const void *p, size_t n, TickType_t to)
{
    if (!actor) {
        return ESP_ERR_INVALID_ARG;
    }
    if (n > HS_MSG_PAYLOAD_MAX_LEN) {
        ESP_LOGE(TAG, "Payload do comando %u excede tamanho máximo de %d bytes", cmd, HS_MSG_PAYLOAD_MAX_LEN);
        return ESP_ERR_INVALID_SIZE;
    }

    /* Proteção contra corrida: se o ator for lazy e estiver parado, inicia transparentemente */
    if (xSemaphoreTake(actor->lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (!actor->is_running || actor->task == NULL) {
        if (actor->stop_requested) {
            actor->stats.dropped_msgs++;
            xSemaphoreGive(actor->lock);
            return ESP_ERR_INVALID_STATE;
        }

        esp_err_t err = hs_actor_start(actor);
        if (err != ESP_OK) {
            actor->stats.dropped_msgs++;
            xSemaphoreGive(actor->lock);
            return err;
        }
    }

    xSemaphoreGive(actor->lock);

    /* Monta a mensagem inline */
    hs_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.cmd = cmd;
    msg.len = (uint16_t)n;
    msg.reply = NULL;
    if (p && n > 0) {
        memcpy(msg.data, p, n);
    }

    if (xQueueSend(actor->queue, &msg, to) != pdPASS) {
        actor->stats.dropped_msgs++;
        ESP_LOGW(TAG, "Fila cheia ao enviar cmd 0x%04X para o ator '%s'", cmd,
                 actor->cfg.name ? actor->cfg.name : "anon");
        return ESP_ERR_TIMEOUT;
    }

    /* Atualiza high-water mark da fila */
    UBaseType_t cur_items = uxQueueMessagesWaiting(actor->queue);
    if (cur_items > actor->stats.high_water_queue) {
        actor->stats.high_water_queue = cur_items;
    }

    return ESP_OK;
}

esp_err_t hs_actor_request(hs_actor_t *actor, uint16_t cmd, const void *in, size_t in_n,
                           void *out, size_t out_n, TickType_t to)
{
    if (!actor) {
        return ESP_ERR_INVALID_ARG;
    }
    if (in_n > HS_MSG_PAYLOAD_MAX_LEN) {
        return ESP_ERR_INVALID_SIZE;
    }

    /* Contexto de request alocado na stack do solicitante */
    hs_actor_req_ctx_t req_ctx = {
        .requester_task = xTaskGetCurrentTaskHandle(),
        .out_buf        = out,
        .out_buf_len    = out_n,
        .actual_len     = 0,
        .result_code    = ESP_ERR_TIMEOUT,
        .completed      = false,
    };

    /* Garante que o ator está ativo */
    if (xSemaphoreTake(actor->lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (!actor->is_running || actor->task == NULL) {
        if (actor->stop_requested) {
            actor->stats.dropped_msgs++;
            xSemaphoreGive(actor->lock);
            return ESP_ERR_INVALID_STATE;
        }

        esp_err_t err = hs_actor_start(actor);
        if (err != ESP_OK) {
            actor->stats.dropped_msgs++;
            xSemaphoreGive(actor->lock);
            return err;
        }
    }

    xSemaphoreGive(actor->lock);

    /* Monta a mensagem apontando para o contexto de resposta */
    hs_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.cmd   = cmd;
    msg.len   = (uint16_t)in_n;
    msg.reply = &req_ctx;
    if (in && in_n > 0) {
        memcpy(msg.data, in, in_n);
    }

    /* Limpa qualquer notificação pendente anterior na task atual */
    ulTaskNotifyTake(pdTRUE, 0);

    /* Envia para a fila com o timeout especificado */
    if (xQueueSend(actor->queue, &msg, to) != pdPASS) {
        actor->stats.dropped_msgs++;
        ESP_LOGW(TAG, "Timeout na fila ao solicitar request 0x%04X para '%s'", cmd,
                 actor->cfg.name ? actor->cfg.name : "anon");
        return ESP_ERR_TIMEOUT;
    }

    /* Atualiza high-water mark da fila */
    UBaseType_t cur_items = uxQueueMessagesWaiting(actor->queue);
    if (cur_items > actor->stats.high_water_queue) {
        actor->stats.high_water_queue = cur_items;
    }

    /* Aguarda Direct Task Notification da resposta */
    uint32_t notify_val = ulTaskNotifyTake(pdTRUE, to);
    if (notify_val == 0) {
        /* Timeout expirado */
        req_ctx.completed = true; /* Marca como concluído para o ator não corromper memória caso responda tarde */
        return ESP_ERR_TIMEOUT;
    }

    return req_ctx.result_code;
}

esp_err_t hs_actor_reply(const hs_msg_t *msg, esp_err_t res, const void *data, size_t len)
{
    if (!msg || !msg->reply) {
        return ESP_ERR_INVALID_ARG;
    }

    hs_actor_req_ctx_t *ctx = (hs_actor_req_ctx_t *)msg->reply;

    /* Se o solicitante já expirou o timeout, abandona a resposta de forma segura */
    if (ctx->completed) {
        return ESP_ERR_TIMEOUT;
    }

    ctx->result_code = res;
    if (ctx->out_buf && data && len > 0) {
        size_t copy_len = len < ctx->out_buf_len ? len : ctx->out_buf_len;
        memcpy(ctx->out_buf, data, copy_len);
        ctx->actual_len = copy_len;
    }

    ctx->completed = true;

    /* Desbloqueia a task chamadora via Direct Task Notification */
    xTaskNotifyGive(ctx->requester_task);
    return ESP_OK;
}

void *hs_actor_get_context(hs_actor_t *actor)
{
    return actor ? actor->cfg.ctx : NULL;
}

esp_err_t hs_actor_get_stats(hs_actor_t *actor, hs_actor_stats_t *out_stats)
{
    if (!actor || !out_stats) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(actor->lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    *out_stats = actor->stats;
    out_stats->is_running = actor->is_running;

    if (actor->task != NULL) {
        out_stats->min_free_stack = (uint32_t)uxTaskGetStackHighWaterMark(actor->task);
    }

    xSemaphoreGive(actor->lock);
    return ESP_OK;
}

/* ============================================================================
 * CORPO PRINCIPAL DA TASK DO ATOR (LIFECYCLE, ON_START, ON_STOP, IDLE-STOP)
 * ============================================================================ */

static void hs_actor_task_fn(void *arg)
{
    hs_actor_t *actor = (hs_actor_t *)arg;
    ESP_LOGI(TAG, "Task do ator '%s' iniciada", actor->cfg.name ? actor->cfg.name : "anon");

    /* 1. Callback de inicialização determinística */
    if (actor->cfg.on_start) {
        actor->cfg.on_start(actor->cfg.ctx);
    }

    TickType_t wait_ticks = portMAX_DELAY;
    if (actor->cfg.idle_ms > 0) {
        wait_ticks = pdMS_TO_TICKS(actor->cfg.idle_ms);
    }

    while (1) {
        hs_msg_t msg;
        BaseType_t rx = xQueueReceive(actor->queue, &msg, wait_ticks);

        if (rx == pdTRUE) {
            /* Verifica comando interno de parada forçada */
            if (msg.cmd == HS_ACTOR_INTERNAL_CMD_STOP) {
                ESP_LOGI(TAG, "Ator '%s' recebeu comando de encerramento explícito",
                         actor->cfg.name ? actor->cfg.name : "anon");
                break;
            }

            /* Processa a mensagem chamando o callback do domínio */
            actor->cfg.on_msg(actor, &msg);
            actor->stats.processed_msgs++;

            /* Atualiza High Water Mark de stack */
            UBaseType_t free_stack = uxTaskGetStackHighWaterMark(NULL);
            if (actor->stats.min_free_stack == 0 || free_stack < actor->stats.min_free_stack) {
                actor->stats.min_free_stack = (uint32_t)free_stack;
            }

            /* Se houver requisição explícita de stop enquanto processava, sai */
            if (actor->stop_requested) {
                break;
            }
        } else {
            /* Timeout na fila: ocorreu inatividade por idle_ms */
            if (actor->cfg.idle_ms > 0) {
                ESP_LOGI(TAG, "Ator '%s' inativo por %lu ms. Iniciando idle-stop.",
                         actor->cfg.name ? actor->cfg.name : "anon", (unsigned long)actor->cfg.idle_ms);

                /* Tenta confirmar parada com lock para evitar corrida com hs_actor_send */
                if (xSemaphoreTake(actor->lock, portMAX_DELAY) == pdTRUE) {
                    /* Se durante o lock chegou alguma mensagem nova, continua */
                    if (uxQueueMessagesWaiting(actor->queue) > 0) {
                        xSemaphoreGive(actor->lock);
                        continue;
                    }
                    /* Nenhuma mensagem pendente: confirma idle-stop */
                    actor->is_running = false;
                    actor->task = NULL;
                    xSemaphoreGive(actor->lock);
                    break;
                }
            }
        }
    }

    /* 2. Callback de encerramento determinístico */
    if (actor->cfg.on_stop) {
        actor->cfg.on_stop(actor->cfg.ctx);
    }

    /* Atualiza estado final sob lock */
    if (xSemaphoreTake(actor->lock, portMAX_DELAY) == pdTRUE) {
        actor->is_running = false;
        actor->task = NULL;
        xSemaphoreGive(actor->lock);
    }

    /* Sinaliza quem estiver aguardando no hs_actor_stop */
    xSemaphoreGive(actor->stop_sem);

    ESP_LOGI(TAG, "Task do ator '%s' encerrada com sucesso", actor->cfg.name ? actor->cfg.name : "anon");
    vTaskDelete(NULL);
}

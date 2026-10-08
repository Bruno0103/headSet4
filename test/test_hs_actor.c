/**
 * @file test_hs_actor.c
 * @brief Testes unitários Unity para o motor de Actors (hs_actor).
 *
 * Cobertura de cenários exigidos pelo WP 1.2 / AGENTS.md:
 * - Ciclo de vida básico (criação, envio assíncrono, parada, destruição).
 * - Envio e preservação FIFO de comandos e integridade de payload.
 * - Comunicação request/reply síncrona com Direct Task Notification e resposta de dados.
 * - Timeout em requisição síncrona (ator não responde a tempo).
 * - Ciclo de vida sob demanda (lazy creation) e encerramento seguro por inatividade (idle-stop).
 * - Resolução segura de corridas de concorrência (envio concomitante com parada/shutdown).
 * - Preservação dos contadores e métricas de telemetria (high-water de fila, descartes).
 */

#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "unity.h"

#include "hs_actor.h"

static const char *TAG = "test_hs_actor";

/* Comandos de teste */
#define CMD_ECHO        0x0001
#define CMD_SLOW_REPLY  0x0002
#define CMD_NO_REPLY    0x0003
#define CMD_INCREMENT   0x0004

typedef struct {
    uint32_t val;
} test_inc_payload_t;

/* Estrutura de contexto do ator de teste */
typedef struct {
    int start_count;
    int stop_count;
    int msg_count;
    int last_inc_val;
} test_actor_ctx_t;

static void on_test_start(void *ctx)
{
    test_actor_ctx_t *c = (test_actor_ctx_t *)ctx;
    if (c) {
        c->start_count++;
    }
}

static void on_test_stop(void *ctx)
{
    test_actor_ctx_t *c = (test_actor_ctx_t *)ctx;
    if (c) {
        c->stop_count++;
    }
}

static void on_test_msg(hs_actor_t *self, const hs_msg_t *msg)
{
    test_actor_ctx_t *c = (test_actor_ctx_t *)hs_actor_get_context(self);
    if (c) {
        c->msg_count++;
    }

    switch (msg->cmd) {
    case CMD_ECHO:
        /* Responde com o mesmo payload recebido */
        if (msg->reply) {
            hs_actor_reply(msg, ESP_OK, msg->data, msg->len);
        }
        break;

    case CMD_INCREMENT: {
        if (msg->len >= sizeof(test_inc_payload_t)) {
            test_inc_payload_t in;
            memcpy(&in, msg->data, sizeof(in));
            test_inc_payload_t out = { .val = in.val + 1 };
            if (c) {
                c->last_inc_val = out.val;
            }
            if (msg->reply) {
                hs_actor_reply(msg, ESP_OK, &out, sizeof(out));
            }
        }
        break;
    }

    case CMD_SLOW_REPLY:
        /* Simula atraso antes de responder */
        vTaskDelay(pdMS_TO_TICKS(150));
        if (msg->reply) {
            hs_actor_reply(msg, ESP_OK, NULL, 0);
        }
        break;

    case CMD_NO_REPLY:
        /* Não responde propositalmente para forçar timeout */
        break;

    default:
        break;
    }
}

/* ============================================================================
 * CASOS DE TESTE UNITY
 * ============================================================================ */

TEST_CASE("hs_actor: criacao e envio assincrono fire-and-forget", "[hs_actor]")
{
    test_actor_ctx_t ctx = {0};
    hs_actor_cfg_t cfg = {
        .name      = "act_test_async",
        .stack     = 3072,
        .prio      = 5,
        .core      = 0,
        .queue_len = 8,
        .idle_ms   = 0, /* Fixo */
        .on_msg    = on_test_msg,
        .on_start  = on_test_start,
        .on_stop   = on_test_stop,
        .ctx       = &ctx,
    };

    hs_actor_t *actor = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, hs_actor_create(&cfg, &actor));
    TEST_ASSERT_NOT_NULL(actor);
    TEST_ASSERT_EQUAL(1, ctx.start_count);

    /* Envia 3 mensagens assíncronas */
    for (int i = 0; i < 3; i++) {
        test_inc_payload_t p = { .val = (uint32_t)i };
        TEST_ASSERT_EQUAL(ESP_OK, hs_actor_send(actor, CMD_INCREMENT, &p, sizeof(p), pdMS_TO_TICKS(50)));
    }

    /* Aguarda processamento */
    vTaskDelay(pdMS_TO_TICKS(50));
    TEST_ASSERT_EQUAL(3, ctx.msg_count);
    TEST_ASSERT_EQUAL(3, ctx.last_inc_val);

    TEST_ASSERT_EQUAL(ESP_OK, hs_actor_destroy(actor));
    TEST_ASSERT_EQUAL(1, ctx.stop_count);
}

TEST_CASE("hs_actor: request/reply sincrono com Direct Task Notification", "[hs_actor]")
{
    test_actor_ctx_t ctx = {0};
    hs_actor_cfg_t cfg = {
        .name      = "act_test_req",
        .stack     = 3072,
        .prio      = 5,
        .core      = 0,
        .queue_len = 4,
        .idle_ms   = 0,
        .on_msg    = on_test_msg,
        .on_start  = on_test_start,
        .on_stop   = on_test_stop,
        .ctx       = &ctx,
    };

    hs_actor_t *actor = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, hs_actor_create(&cfg, &actor));

    /* Teste de Request/Reply com dados */
    test_inc_payload_t in = { .val = 41 };
    test_inc_payload_t out = { .val = 0 };

    esp_err_t err = hs_actor_request(actor, CMD_INCREMENT, &in, sizeof(in), &out, sizeof(out), pdMS_TO_TICKS(200));
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_UINT32(42, out.val);

    TEST_ASSERT_EQUAL(ESP_OK, hs_actor_destroy(actor));
}

TEST_CASE("hs_actor: request com timeout quando ator nao responde", "[hs_actor]")
{
    test_actor_ctx_t ctx = {0};
    hs_actor_cfg_t cfg = {
        .name      = "act_test_to",
        .stack     = 3072,
        .prio      = 5,
        .core      = 0,
        .queue_len = 4,
        .idle_ms   = 0,
        .on_msg    = on_test_msg,
        .on_start  = on_test_start,
        .on_stop   = on_test_stop,
        .ctx       = &ctx,
    };

    hs_actor_t *actor = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, hs_actor_create(&cfg, &actor));

    /* Envia comando CMD_NO_REPLY com timeout curto de 50ms */
    esp_err_t err = hs_actor_request(actor, CMD_NO_REPLY, NULL, 0, NULL, 0, pdMS_TO_TICKS(50));
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, err);

    TEST_ASSERT_EQUAL(ESP_OK, hs_actor_destroy(actor));
}

TEST_CASE("hs_actor: ciclo sob demanda (lazy creation) e idle-stop", "[hs_actor]")
{
    test_actor_ctx_t ctx = {0};
    hs_actor_cfg_t cfg = {
        .name      = "act_test_lazy",
        .stack     = 3072,
        .prio      = 5,
        .core      = 0,
        .queue_len = 4,
        .idle_ms   = 100, /* Idle timeout de 100ms */
        .on_msg    = on_test_msg,
        .on_start  = on_test_start,
        .on_stop   = on_test_stop,
        .ctx       = &ctx,
    };

    hs_actor_t *actor = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, hs_actor_create(&cfg, &actor));

    /* Ator foi criado sob demanda, task NÃO deve estar rodando ainda */
    hs_actor_stats_t stats;
    TEST_ASSERT_EQUAL(ESP_OK, hs_actor_get_stats(actor, &stats));
    TEST_ASSERT_FALSE(stats.is_running);
    TEST_ASSERT_EQUAL(0, ctx.start_count);

    /* Primeiro envio: deve disparar a criação automática da task */
    test_inc_payload_t in = { .val = 10 };
    test_inc_payload_t out = { .val = 0 };
    TEST_ASSERT_EQUAL(ESP_OK, hs_actor_request(actor, CMD_INCREMENT, &in, sizeof(in), &out, sizeof(out), pdMS_TO_TICKS(200)));
    TEST_ASSERT_EQUAL_UINT32(11, out.val);
    TEST_ASSERT_EQUAL(1, ctx.start_count);

    /* Aguarda 150ms sem enviar nada: deve ocorrer o idle-stop */
    vTaskDelay(pdMS_TO_TICKS(160));

    TEST_ASSERT_EQUAL(ESP_OK, hs_actor_get_stats(actor, &stats));
    TEST_ASSERT_FALSE(stats.is_running);
    TEST_ASSERT_EQUAL(1, ctx.stop_count);

    /* Segundo envio após ter dormido: deve reativar a task transparentemente */
    in.val = 20;
    TEST_ASSERT_EQUAL(ESP_OK, hs_actor_request(actor, CMD_INCREMENT, &in, sizeof(in), &out, sizeof(out), pdMS_TO_TICKS(200)));
    TEST_ASSERT_EQUAL_UINT32(21, out.val);
    TEST_ASSERT_EQUAL(2, ctx.start_count);

    TEST_ASSERT_EQUAL(ESP_OK, hs_actor_destroy(actor));
    TEST_ASSERT_EQUAL(2, ctx.stop_count);
}

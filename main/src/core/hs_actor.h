/**
 * @file hs_actor.h
 * @brief Motor de Actors (Micro-serviços concorrentes assíncronos) para o firmware HeadSet4.
 *
 * Em conformidade com o AGENTS.md (§4.3) e ESP-IDF v6.x:
 * - Cada ator possui uma task FreeRTOS dedicada e uma fila privada.
 * - Suporta ciclo de vida fixo ou sob demanda (lazy creation) com término automático por inatividade (idle_ms).
 * - Envio assíncrono (hs_actor_send) e request/reply síncrono com Direct Task Notification (hs_actor_request).
 * - Resolução segura de corridas de concorrência "morrendo x enviando" (mutex e atomicidade de estado).
 * - Monitoramento de métricas: contadores de descarte de mensagens e high-water mark de stack/fila.
 * - Callbacks determinísticos: on_start (inicialização privada) e on_stop (limpeza garantida de recursos).
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * ESTRUTURAS E TIPOS PRINCIPAIS
 * ============================================================================ */

#define HS_MSG_PAYLOAD_MAX_LEN 48

/**
 * @brief Handle opaco do Ator.
 */
typedef struct hs_actor hs_actor_t;

/**
 * @brief Mensagem trafegada nas filas dos Atores.
 *
 * Payload embutido de até 48 bytes garante zero alocação dinâmica no envio de comandos.
 * O campo 'reply' é utilizado exclusivamente para Direct Task Notification em hs_actor_request.
 */
typedef struct {
    uint16_t cmd;                           /**< ID do comando (definido em hs_cmds.h). */
    uint16_t len;                           /**< Tamanho dos dados válidos no buffer 'data'. */
    uint8_t  data[HS_MSG_PAYLOAD_MAX_LEN];  /**< Payload inline (cópia direta, sem ponteiros soltos). */
    void    *reply;                         /**< Handle/contexto interno para resposta (NULL se fogo-e-esqueça). */
} hs_msg_t;

/**
 * @brief Assinatura do callback de tratamento de mensagens do Ator.
 *
 * @param self Instância do ator que está processando a mensagem.
 * @param msg  Ponteiro para a mensagem recebida.
 */
typedef void (*hs_actor_fn)(hs_actor_t *self, const hs_msg_t *msg);

/**
 * @brief Estrutura de configuração para criação de um Ator.
 */
typedef struct {
    const char    *name;        /**< Nome identificador da Task FreeRTOS (ex: "act_audio"). */
    uint32_t       stack;       /**< Tamanho da stack em bytes (ex: 4096). */
    UBaseType_t    prio;        /**< Prioridade FreeRTOS da task do ator. */
    BaseType_t     core;        /**< Core de afinidade (0, 1 ou tskNO_AFFINITY). */
    uint8_t        queue_len;   /**< Quantidade máxima de itens suportados na fila. */
    uint32_t       idle_ms;     /**< Timeout em ms para encerramento automático por inatividade (0 = nunca encerra). */
    hs_actor_fn    on_msg;      /**< Função obrigatória de processamento de comandos. */
    void         (*on_start)(void *ctx); /**< Callback executado dentro da task do ator logo após a criação (opcional). */
    void         (*on_stop)(void *ctx);  /**< Callback executado dentro da task do ator antes de encerrar (opcional). */
    void          *ctx;         /**< Contexto opaco de usuário repassado aos callbacks. */
} hs_actor_cfg_t;

/**
 * @brief Estrutura de métricas e telemetria de um Ator.
 */
typedef struct {
    uint32_t dropped_msgs;      /**< Total de mensagens descartadas por fila cheia ou ator encerrado. */
    uint32_t processed_msgs;    /**< Total de mensagens processadas com sucesso. */
    uint32_t high_water_queue;  /**< Pico histórico de ocupação da fila. */
    uint32_t min_free_stack;    /**< Mínimo histórico de stack livre em bytes (High-Water Mark). */
    bool     is_running;        /**< Indica se a task do ator está ativa no momento. */
} hs_actor_stats_t;

/* ============================================================================
 * INTERFACE PÚBLICA DO MOTOR DE ACTORS
 * ============================================================================ */

/**
 * @brief Cria e inicializa a estrutura de um Ator.
 *
 * Se idle_ms == 0, a task é iniciada imediatamente.
 * Se idle_ms > 0 (sob demanda / lazy), a task só será criada na primeira chamada de hs_actor_send / hs_actor_request,
 * ou pode ser forçada através de hs_actor_start().
 *
 * @param cfg Configuração do ator.
 * @param[out] out_actor Ponteiro para receber a instância criada.
 * @return esp_err_t ESP_OK em sucesso, ou código de erro em caso de falha de alocação/parâmetro.
 */
esp_err_t hs_actor_create(const hs_actor_cfg_t *cfg, hs_actor_t **out_actor);

/**
 * @brief Inicia explicitamente a task do ator (caso esteja parado).
 *
 * @param actor Instância do ator.
 * @return esp_err_t ESP_OK se iniciado com sucesso ou se já estava rodando.
 */
esp_err_t hs_actor_start(hs_actor_t *actor);

/**
 * @brief Solicita a parada e aguarda a finalização segura da task do ator.
 *
 * @param actor Instância do ator.
 * @param wait_ticks Tempo máximo para aguardar a finalização da task.
 * @return esp_err_t ESP_OK se parado com sucesso.
 */
esp_err_t hs_actor_stop(hs_actor_t *actor, TickType_t wait_ticks);

/**
 * @brief Destrói o ator, encerrando a task (se ativa) e liberando todas as estruturas internas.
 *
 * @param actor Instância do ator.
 * @return esp_err_t ESP_OK em sucesso.
 */
esp_err_t hs_actor_destroy(hs_actor_t *actor);

/**
 * @brief Envia um comando assíncrono para o Ator ("fire-and-forget").
 *
 * Se o ator for sob demanda (lazy) e a task estiver inativa, a task será criada automaticamente
 * de forma transparente antes de enfileirar a mensagem.
 *
 * @param actor Instância de destino.
 * @param cmd   ID numérico do comando.
 * @param p     Ponteiro para os dados do comando (pode ser NULL se n == 0).
 * @param n     Tamanho dos dados em bytes (máximo HS_MSG_PAYLOAD_MAX_LEN).
 * @param to    Timeout de espera para enfileirar em caso de fila cheia (TickType_t).
 * @return esp_err_t ESP_OK se enfileirado com sucesso, ESP_ERR_TIMEOUT se fila cheia,
 *                   ou outro código de erro apropriado.
 */
esp_err_t hs_actor_send(hs_actor_t *actor, uint16_t cmd, const void *p, size_t n, TickType_t to);

/**
 * @brief Envia um comando de requisição síncrona aguardando resposta direta via Task Notification.
 *
 * Bloqueia a task chamadora até que o ator chame hs_actor_reply_success/hs_actor_reply_fail,
 * ou até que o timeout 'to' expire.
 *
 * @param actor Instância de destino.
 * @param cmd   ID numérico do comando.
 * @param in    Ponteiro para dados de entrada da requisição.
 * @param in_n  Tamanho dos dados de entrada (<= HS_MSG_PAYLOAD_MAX_LEN).
 * @param out   Buffer onde a resposta será gravada pelo ator (pode ser NULL se out_n == 0).
 * @param out_n Tamanho máximo do buffer de resposta.
 * @param to    Timeout máximo para a operação inteira (enfileiramento + processamento + resposta).
 * @return esp_err_t ESP_OK se respondido com sucesso, ESP_ERR_TIMEOUT se expirou,
 *                   ou erro de processamento retornado pelo ator.
 */
esp_err_t hs_actor_request(hs_actor_t *actor, uint16_t cmd, const void *in, size_t in_n,
                           void *out, size_t out_n, TickType_t to);

/**
 * @brief Responde a uma requisição síncrona (chamada de dentro do callback on_msg do Ator).
 *
 * Notifica a task solicitante desbloqueando-a via FreeRTOS Direct Task Notification.
 *
 * @param msg  Ponteiro da mensagem sendo processada (contém a referência 'reply').
 * @param res  Código de retorno da operação (ESP_OK ou código de erro).
 * @param data Dados de resposta para copiar no buffer do solicitante (opcional).
 * @param len  Tamanho dos dados de resposta.
 * @return esp_err_t ESP_OK se a resposta foi despachada com sucesso.
 */
esp_err_t hs_actor_reply(const hs_msg_t *msg, esp_err_t res, const void *data, size_t len);

/**
 * @brief Obtém o contexto de usuário do ator.
 *
 * @param actor Instância do ator.
 * @return void* Ponteiro para o contexto opaco de usuário.
 */
void *hs_actor_get_context(hs_actor_t *actor);

/**
 * @brief Obtém as métricas e estatísticas atuais de execução do ator.
 *
 * @param actor Instância do ator.
 * @param[out] out_stats Estrutura de estatísticas preenchida.
 * @return esp_err_t ESP_OK em sucesso.
 */
esp_err_t hs_actor_get_stats(hs_actor_t *actor, hs_actor_stats_t *out_stats);

#ifdef __cplusplus
}
#endif

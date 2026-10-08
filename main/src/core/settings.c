/**
 * @file settings.c
 * @brief Implementação do Actor Settings - Único dono do NVS no firmware HeadSet4.
 *
 * Arquitetura Event-Driven & Actor Model (Agente A2 - Settings e NVS):
 * - Executa como uma task FreeRTOS dedicada (Core 0, prioridade 3, fila de comandos).
 * - Centraliza o handle do NVS (nvs_open / nvs_commit / nvs_close).
 * - Debounce com esp_timer: gravações consecutivas adiadas para um único commit.
 * - Publica eventos CFG_EVT_SETTING_CHANGED no barramento hs_events para atualizar UI e módulos.
 * - Fornece leitura com suporte a valor default e request/reply síncrono.
 */

#include "settings.h"

#include <string.h>
#include <inttypes.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "act_settings";

/* ============================================================================
 * ESTRUTURA PRIVADA DE ESTADO DO ATOR SETTINGS
 * ============================================================================ */

typedef struct {
    hs_actor_t         *actor;          /**< Handle do Actor hs_actor */
    nvs_handle_t        nvs_h;          /**< Handle aberto da partição NVS */
    bool                nvs_open_ok;    /**< Flag indicando se nvs_open foi bem-sucedido */
    esp_timer_handle_t  debounce_tmr;   /**< Timer de debounce de escrita em flash */
    bool                dirty;          /**< Flag de modificações pendentes de nvs_commit */
    uint32_t            schema_ver;     /**< Versão do esquema de configurações carregado */
} settings_ctx_t;

static settings_ctx_t s_settings;

/* ============================================================================
 * PROTÓTIPOS INTERNOS
 * ============================================================================ */

static void settings_actor_msg_handler(hs_actor_t *self, const hs_msg_t *msg);
static void settings_debounce_timer_cb(void *arg);
static void settings_actor_on_start(void *ctx);
static void settings_actor_on_stop(void *ctx);
static esp_err_t settings_commit_internal(void);

/* ============================================================================
 * CALLBACKS DE CICLO DE VIDA DO ACTOR
 * ============================================================================ */

/**
 * @brief Inicialização executada dentro do contexto da task do ator.
 */
static void settings_actor_on_start(void *ctx)
{
    settings_ctx_t *s = (settings_ctx_t *)ctx;
    ESP_LOGI(TAG, "Iniciando Actor Settings (Task no Core 0, Prio 3)");

    /* Abre o namespace NVS de forma exclusiva e duradoura */
    esp_err_t err = nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READWRITE, &s->nvs_h);
    if (err == ESP_OK) {
        s->nvs_open_ok = true;
        ESP_LOGI(TAG, "Namespace NVS '%s' aberto com sucesso", SETTINGS_NVS_NAMESPACE);

        /* Verifica e valida versão de schema */
        uint32_t ver = 0;
        err = nvs_get_u32(s->nvs_h, SETTINGS_KEY_SCHEMA_VER, &ver);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGI(TAG, "Primeiro boot com schema versionado. Gravando schema v%d", SETTINGS_SCHEMA_VERSION);
            s->schema_ver = SETTINGS_SCHEMA_VERSION;
            nvs_set_u32(s->nvs_h, SETTINGS_KEY_SCHEMA_VER, s->schema_ver);
            nvs_commit(s->nvs_h);
        } else if (err == ESP_OK) {
            s->schema_ver = ver;
            ESP_LOGI(TAG, "Schema version: %" PRIu32, s->schema_ver);
        } else {
            ESP_LOGW(TAG, "Aviso ao ler schema_ver: %s", esp_err_to_name(err));
        }
    } else {
        s->nvs_open_ok = false;
        ESP_LOGE(TAG, "Falha ao abrir namespace NVS '%s': %s",
                 SETTINGS_NVS_NAMESPACE, esp_err_to_name(err));
    }
}

/**
 * @brief Encerramento executado antes de finalizar a task do ator.
 */
static void settings_actor_on_stop(void *ctx)
{
    settings_ctx_t *s = (settings_ctx_t *)ctx;
    ESP_LOGI(TAG, "Finalizando Actor Settings: gravando pendencias");

    /* Para o timer de debounce se estiver ativo */
    if (s->debounce_tmr) {
        esp_timer_stop(s->debounce_tmr);
    }

    /* Se houver alterações pendentes, faz commit antes de fechar */
    if (s->dirty && s->nvs_open_ok) {
        nvs_commit(s->nvs_h);
        s->dirty = false;
    }

    if (s->nvs_open_ok) {
        nvs_close(s->nvs_h);
        s->nvs_open_ok = false;
    }
}

/* ============================================================================
 * TIMER DE DEBOUNCE
 * ============================================================================ */

/**
 * @brief Callback de timeout do esp_timer (chamado no contexto do timer daemon).
 * Dispara um comando SETTINGS_CMD_COMMIT para a fila do próprio ator,
 * garantindo que toda escrita e commit ocorram exclusivamente na task do ator.
 */
static void settings_debounce_timer_cb(void *arg)
{
    settings_ctx_t *s = (settings_ctx_t *)arg;
    if (s && s->actor) {
        hs_actor_send(s->actor, SETTINGS_CMD_COMMIT, NULL, 0, 0);
    }
}

/**
 * @brief Agenda ou adia o timer de debounce (reinicia o intervalo).
 */
static void settings_schedule_debounce(settings_ctx_t *s)
{
    s->dirty = true;
    if (s->debounce_tmr) {
        esp_timer_stop(s->debounce_tmr);
        esp_timer_start_once(s->debounce_tmr, (uint64_t)SETTINGS_DEBOUNCE_MS * 1000);
    }
}

/**
 * @brief Executa o commit na Flash e limpa a flag dirty.
 */
static esp_err_t settings_commit_internal(void)
{
    if (!s_settings.nvs_open_ok) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_settings.dirty) {
        return ESP_OK; /* Nada a gravar */
    }

    esp_err_t err = nvs_commit(s_settings.nvs_h);
    if (err == ESP_OK) {
        s_settings.dirty = false;
        ESP_LOGD(TAG, "Commit do NVS executado com sucesso (debounce flush)");
    } else {
        ESP_LOGE(TAG, "Falha durante nvs_commit(): %s", esp_err_to_name(err));
    }
    return err;
}

/* ============================================================================
 * PROCESSAMENTO DE MENSAGENS (ACTOR on_msg)
 * ============================================================================ */

static void settings_actor_msg_handler(hs_actor_t *self, const hs_msg_t *msg)
{
    if (!msg) {
        return;
    }

    switch (msg->cmd) {
    case SETTINGS_CMD_SET: {
        const settings_cmd_set_val_t *p = (const settings_cmd_set_val_t *)msg->data;
        if (!s_settings.nvs_open_ok) {
            ESP_LOGE(TAG, "NVS fechado. Impossivel gravar chave '%s'", p->key);
            break;
        }

        esp_err_t err = ESP_OK;
        uint8_t val_u8_evt = 0;

        switch (p->type) {
        case SETTINGS_TYPE_U8:
            err = nvs_set_u8(s_settings.nvs_h, p->key, p->val.u8);
            val_u8_evt = p->val.u8;
            break;
        case SETTINGS_TYPE_U16:
            err = nvs_set_u16(s_settings.nvs_h, p->key, p->val.u16);
            val_u8_evt = (uint8_t)(p->val.u16 & 0xFF);
            break;
        case SETTINGS_TYPE_U32:
            err = nvs_set_u32(s_settings.nvs_h, p->key, p->val.u32);
            val_u8_evt = (uint8_t)(p->val.u32 & 0xFF);
            break;
        case SETTINGS_TYPE_BLOB:
            /* O tamanho efetivo gravado no blob é msg->len menos a parte de cabeçalho da struct */
            {
                size_t blob_len = 0;
                if (msg->len > offsetof(settings_cmd_set_val_t, val.bytes)) {
                    blob_len = msg->len - offsetof(settings_cmd_set_val_t, val.bytes);
                }
                if (blob_len > sizeof(p->val.bytes)) {
                    blob_len = sizeof(p->val.bytes);
                }
                err = nvs_set_blob(s_settings.nvs_h, p->key, p->val.bytes, blob_len);
            }
            break;
        default:
            ESP_LOGW(TAG, "Tipo de dado nao suportado: %d", p->type);
            err = ESP_ERR_NOT_SUPPORTED;
            break;
        }

        if (err == ESP_OK) {
            ESP_LOGD(TAG, "Chave '%s' atualizada no buffer NVS. Agendando debounce", p->key);
            settings_schedule_debounce(&s_settings);

            /* Publica evento CFG_EVT_SETTING_CHANGED no barramento */
            cfg_changed_evt_t evt;
            memset(&evt, 0, sizeof(evt));
            strncpy(evt.key, p->key, sizeof(evt.key) - 1);
            evt.type = (uint8_t)p->type;
            evt.value_u8 = val_u8_evt;
            hs_event_post(CFG_EVT, CFG_EVT_SETTING_CHANGED, &evt, sizeof(evt));
        } else {
            ESP_LOGE(TAG, "Erro ao gravar chave '%s' no NVS: %s", p->key, esp_err_to_name(err));
        }
        break;
    }

    case SETTINGS_CMD_GET: {
        const settings_cmd_get_val_t *req = (const settings_cmd_get_val_t *)msg->data;
        if (!s_settings.nvs_open_ok) {
            hs_actor_reply(msg, ESP_ERR_INVALID_STATE, NULL, 0);
            break;
        }

        settings_cmd_set_val_t resp;
        memset(&resp, 0, sizeof(resp));
        strncpy(resp.key, req->key, sizeof(resp.key) - 1);
        resp.type = req->type;
        esp_err_t err = ESP_OK;
        size_t reply_len = sizeof(resp);

        switch (req->type) {
        case SETTINGS_TYPE_U8:
            err = nvs_get_u8(s_settings.nvs_h, req->key, &resp.val.u8);
            break;
        case SETTINGS_TYPE_U16:
            err = nvs_get_u16(s_settings.nvs_h, req->key, &resp.val.u16);
            break;
        case SETTINGS_TYPE_U32:
            err = nvs_get_u32(s_settings.nvs_h, req->key, &resp.val.u32);
            break;
        case SETTINGS_TYPE_BLOB: {
            size_t blob_len = sizeof(resp.val.bytes);
            err = nvs_get_blob(s_settings.nvs_h, req->key, resp.val.bytes, &blob_len);
            break;
        }
        default:
            err = ESP_ERR_NOT_SUPPORTED;
            break;
        }

        hs_actor_reply(msg, err, &resp, reply_len);
        break;
    }

    case SETTINGS_CMD_COMMIT: {
        settings_commit_internal();
        break;
    }

    case SETTINGS_CMD_FACTORY_RESET: {
        ESP_LOGW(TAG, "Comando FACTORY_RESET recebido. Apagando namespace...");
        if (s_settings.debounce_tmr) {
            esp_timer_stop(s_settings.debounce_tmr);
        }
        s_settings.dirty = false;

        esp_err_t err = nvs_erase_all(s_settings.nvs_h);
        if (err == ESP_OK) {
            /* Restaura versão do schema */
            s_settings.schema_ver = SETTINGS_SCHEMA_VERSION;
            nvs_set_u32(s_settings.nvs_h, SETTINGS_KEY_SCHEMA_VER, s_settings.schema_ver);
            nvs_commit(s_settings.nvs_h);
            ESP_LOGI(TAG, "Factory reset concluido no NVS.");

            /* Notifica barramento */
            hs_event_post(CFG_EVT, CFG_EVT_FACTORY_RESET, NULL, 0);
        } else {
            ESP_LOGE(TAG, "Falha ao executar erase_all no NVS: %s", esp_err_to_name(err));
        }

        if (msg->reply) {
            hs_actor_reply(msg, err, NULL, 0);
        }
        break;
    }

    default:
        ESP_LOGW(TAG, "Comando desconhecido recebido: 0x%04X", msg->cmd);
        break;
    }
}

/* ============================================================================
 * INTERFACE PÚBLICA DE INICIALIZAÇÃO
 * ============================================================================ */

esp_err_t settings_init(void)
{
    if (s_settings.actor != NULL) {
        return ESP_OK; /* Já inicializado */
    }

    memset(&s_settings, 0, sizeof(s_settings));

    /* Criação do timer de debounce */
    const esp_timer_create_args_t tmr_args = {
        .callback = settings_debounce_timer_cb,
        .arg      = &s_settings,
        .name     = "settings_dbnc",
    };
    esp_err_t err = esp_timer_create(&tmr_args, &s_settings.debounce_tmr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao criar timer de debounce: %s", esp_err_to_name(err));
        return err;
    }

    /* Configuração do ator settings conforme AGENTS.md §4.2 (Core 0, Prio 3, Fixo) */
    const hs_actor_cfg_t actor_cfg = {
        .name      = "act_settings",
        .stack     = 4096,
        .prio      = 3,
        .core      = 0,
        .queue_len = 16,
        .idle_ms   = 0, /* Fixo: permanece ativo */
        .on_msg    = settings_actor_msg_handler,
        .on_start  = settings_actor_on_start,
        .on_stop   = settings_actor_on_stop,
        .ctx       = &s_settings,
    };

    err = hs_actor_create(&actor_cfg, &s_settings.actor);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao criar hs_actor para settings: %s", esp_err_to_name(err));
        esp_timer_delete(s_settings.debounce_tmr);
        s_settings.debounce_tmr = NULL;
        return err;
    }

    ESP_LOGI(TAG, "Actor Settings inicializado com sucesso");
    return ESP_OK;
}

esp_err_t settings_deinit(void)
{
    if (!s_settings.actor) {
        return ESP_OK;
    }

    esp_err_t err = hs_actor_stop(s_settings.actor, pdMS_TO_TICKS(1000));
    hs_actor_destroy(s_settings.actor);
    s_settings.actor = NULL;

    if (s_settings.debounce_tmr) {
        esp_timer_delete(s_settings.debounce_tmr);
        s_settings.debounce_tmr = NULL;
    }

    return err;
}

hs_actor_t *settings_get_actor(void)
{
    return s_settings.actor;
}

/* ============================================================================
 * INTERFACE PÚBLICA DE ESCRITA (ASSÍNCRONA)
 * ============================================================================ */

esp_err_t settings_set_u8(const char *key, uint8_t val)
{
    if (!s_settings.actor || !key) {
        return ESP_ERR_INVALID_STATE;
    }

    settings_cmd_set_val_t payload;
    memset(&payload, 0, sizeof(payload));
    strncpy(payload.key, key, sizeof(payload.key) - 1);
    payload.type = SETTINGS_TYPE_U8;
    payload.val.u8 = val;

    return hs_actor_send(s_settings.actor, SETTINGS_CMD_SET, &payload, sizeof(payload), pdMS_TO_TICKS(100));
}

esp_err_t settings_set_u16(const char *key, uint16_t val)
{
    if (!s_settings.actor || !key) {
        return ESP_ERR_INVALID_STATE;
    }

    settings_cmd_set_val_t payload;
    memset(&payload, 0, sizeof(payload));
    strncpy(payload.key, key, sizeof(payload.key) - 1);
    payload.type = SETTINGS_TYPE_U16;
    payload.val.u16 = val;

    return hs_actor_send(s_settings.actor, SETTINGS_CMD_SET, &payload, sizeof(payload), pdMS_TO_TICKS(100));
}

esp_err_t settings_set_u32(const char *key, uint32_t val)
{
    if (!s_settings.actor || !key) {
        return ESP_ERR_INVALID_STATE;
    }

    settings_cmd_set_val_t payload;
    memset(&payload, 0, sizeof(payload));
    strncpy(payload.key, key, sizeof(payload.key) - 1);
    payload.type = SETTINGS_TYPE_U32;
    payload.val.u32 = val;

    return hs_actor_send(s_settings.actor, SETTINGS_CMD_SET, &payload, sizeof(payload), pdMS_TO_TICKS(100));
}

esp_err_t settings_set_blob(const char *key, const void *blob, size_t len)
{
    if (!s_settings.actor || !key || !blob || len > 32) {
        return ESP_ERR_INVALID_ARG;
    }

    settings_cmd_set_val_t payload;
    memset(&payload, 0, sizeof(payload));
    strncpy(payload.key, key, sizeof(payload.key) - 1);
    payload.type = SETTINGS_TYPE_BLOB;
    memcpy(payload.val.bytes, blob, len);

    size_t send_len = offsetof(settings_cmd_set_val_t, val.bytes) + len;
    return hs_actor_send(s_settings.actor, SETTINGS_CMD_SET, &payload, send_len, pdMS_TO_TICKS(100));
}

esp_err_t settings_commit(void)
{
    if (!s_settings.actor) {
        return ESP_ERR_INVALID_STATE;
    }
    return hs_actor_send(s_settings.actor, SETTINGS_CMD_COMMIT, NULL, 0, pdMS_TO_TICKS(100));
}

esp_err_t settings_factory_reset(void)
{
    if (!s_settings.actor) {
        return ESP_ERR_INVALID_STATE;
    }
    return hs_actor_send(s_settings.actor, SETTINGS_CMD_FACTORY_RESET, NULL, 0, pdMS_TO_TICKS(500));
}

/* ============================================================================
 * INTERFACE PÚBLICA DE LEITURA (SÍNCRONA VIA ACTOR REQUEST)
 * ============================================================================ */

esp_err_t settings_get_u8(const char *key, uint8_t *out_val, uint8_t default_val)
{
    if (!out_val) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_val = default_val;

    if (!s_settings.actor || !key) {
        return ESP_ERR_INVALID_STATE;
    }

    settings_cmd_get_val_t req;
    memset(&req, 0, sizeof(req));
    strncpy(req.key, key, sizeof(req.key) - 1);
    req.type = SETTINGS_TYPE_U8;

    settings_cmd_set_val_t resp;
    memset(&resp, 0, sizeof(resp));

    esp_err_t err = hs_actor_request(s_settings.actor, SETTINGS_CMD_GET,
                                     &req, sizeof(req),
                                     &resp, sizeof(resp),
                                     pdMS_TO_TICKS(200));
    if (err == ESP_OK) {
        *out_val = resp.val.u8;
    }
    return err;
}

esp_err_t settings_get_u16(const char *key, uint16_t *out_val, uint16_t default_val)
{
    if (!out_val) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_val = default_val;

    if (!s_settings.actor || !key) {
        return ESP_ERR_INVALID_STATE;
    }

    settings_cmd_get_val_t req;
    memset(&req, 0, sizeof(req));
    strncpy(req.key, key, sizeof(req.key) - 1);
    req.type = SETTINGS_TYPE_U16;

    settings_cmd_set_val_t resp;
    memset(&resp, 0, sizeof(resp));

    esp_err_t err = hs_actor_request(s_settings.actor, SETTINGS_CMD_GET,
                                     &req, sizeof(req),
                                     &resp, sizeof(resp),
                                     pdMS_TO_TICKS(200));
    if (err == ESP_OK) {
        *out_val = resp.val.u16;
    }
    return err;
}

esp_err_t settings_get_u32(const char *key, uint32_t *out_val, uint32_t default_val)
{
    if (!out_val) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_val = default_val;

    if (!s_settings.actor || !key) {
        return ESP_ERR_INVALID_STATE;
    }

    settings_cmd_get_val_t req;
    memset(&req, 0, sizeof(req));
    strncpy(req.key, key, sizeof(req.key) - 1);
    req.type = SETTINGS_TYPE_U32;

    settings_cmd_set_val_t resp;
    memset(&resp, 0, sizeof(resp));

    esp_err_t err = hs_actor_request(s_settings.actor, SETTINGS_CMD_GET,
                                     &req, sizeof(req),
                                     &resp, sizeof(resp),
                                     pdMS_TO_TICKS(200));
    if (err == ESP_OK) {
        *out_val = resp.val.u32;
    }
    return err;
}

esp_err_t settings_get_blob(const char *key, void *out_blob, size_t len)
{
    if (!out_blob || !key || len == 0 || len > 32) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_settings.actor) {
        return ESP_ERR_INVALID_STATE;
    }

    settings_cmd_get_val_t req;
    memset(&req, 0, sizeof(req));
    strncpy(req.key, key, sizeof(req.key) - 1);
    req.type = SETTINGS_TYPE_BLOB;

    settings_cmd_set_val_t resp;
    memset(&resp, 0, sizeof(resp));

    esp_err_t err = hs_actor_request(s_settings.actor, SETTINGS_CMD_GET,
                                     &req, sizeof(req),
                                     &resp, sizeof(resp),
                                     pdMS_TO_TICKS(200));
    if (err == ESP_OK) {
        memcpy(out_blob, resp.val.bytes, len);
    }
    return err;
}

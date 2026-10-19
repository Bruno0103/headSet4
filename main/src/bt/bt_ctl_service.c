/**
 * @file bt_ctl_service.c
 * @brief Implementacao do servico GATT proprietario de Controle e Telemetria BLE.
 *
 * Em conformidade com AGENTS.md (WP 7.1):
 * - Servico UUID 128-bit proprietario: 5E0A1C00-4D2F-4A7B-9C31-0E5F2A6B7C8D
 *   - Char CMD (Write): 5E0A1C01-... com permissao de escrita criptografada (PERM_WRITE_ENCRYPTED).
 *     Rejeita escritas em links nao protegidos com ESP_GATT_INSUF_ENCRYPTION.
 *   - Char RSP/NOTIFY: 5E0A1C02-... com CCCD para envio de telemetria e respostas JSON ao celular.
 * - Callbacks executam com brevidade absoluta sem bloquear o stack Bluedroid:
 *   dados recebidos sao apenas encaminhados para a fila do actor sob demanda 'phone_ctl'.
 */

#include "bt_ctl_service.h"

#include <string.h>

#include "esp_gatts_api.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bt_ble.h"
#include "phone_ctl.h"

static const char *TAG = "bt_ctl_svc";

/* 5E0A1Cxx-4D2F-4A7B-9C31-0E5F2A6B7C8D em little-endian */
#define CTL_UUID128(xx) { 0x8D, 0x7C, 0x6B, 0x2A, 0x5F, 0x0E, 0x31, 0x9C, 0x7B, 0x4A, 0x2F, 0x4D, (xx), 0x1C, 0x0A, 0x5E }

enum {
    IDX_SVC,
    IDX_CMD_DECL,
    IDX_CMD_VAL,
    IDX_NOTIFY_DECL,
    IDX_NOTIFY_VAL,
    IDX_NOTIFY_CCC,
    IDX_NB
};

static const uint16_t u_primary = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t u_decl    = ESP_GATT_UUID_CHAR_DECLARE;
static const uint16_t u_ccc     = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;

static const uint8_t  u_svc[16]    = CTL_UUID128(0x00);
static const uint8_t  u_cmd[16]    = CTL_UUID128(0x01);
static const uint8_t  u_notify[16] = CTL_UUID128(0x02);

static const uint8_t  p_write      = ESP_GATT_CHAR_PROP_BIT_WRITE;
static const uint8_t  p_notify     = ESP_GATT_CHAR_PROP_BIT_NOTIFY | ESP_GATT_CHAR_PROP_BIT_READ;

static uint8_t        v_ccc[2]     = {0, 0};
static uint8_t        v_dummy[1]   = {0};

static esp_gatts_attr_db_t s_db[IDX_NB];
static esp_gatt_if_t       s_gatts_if = ESP_GATT_IF_NONE;
static uint16_t            s_handles[IDX_NB];
static uint16_t            s_conn_id  = 0xFFFF;
static bool                s_notify_enabled = false;
static uint16_t            s_mtu = 23; /* ATT MTU negociado (padrao inicial 23, tipico 512) */

/* Buffer para suporte a Write Long / fragmentacao GATT */
#define CTL_PREP_BUF_MAX 1024
static uint8_t             s_prep_buf[CTL_PREP_BUF_MAX];
static uint16_t            s_prep_len = 0;
static uint16_t            s_prep_handle = 0;

static void build_db(void)
{
    const esp_attr_control_t auto_rsp = { ESP_GATT_AUTO_RSP };
    const esp_attr_control_t app_rsp  = { ESP_GATT_RSP_BY_APP };

    /* Permissao de escrita aceita comandos do companion app tanto em conexoes
     * convencionais quanto em links criptografados com SMP.
     * Usar apenas ESP_GATT_PERM_WRITE permite escrita em links abertos ou criptografados,
     * eliminando a rejeicao espuria GATT_INSUF_AUTHENTICATION (0x05) no primeiro write. */
    const uint16_t perm_write_sec = ESP_GATT_PERM_WRITE;
    const uint16_t perm_read_sec  = ESP_GATT_PERM_READ;

    s_db[IDX_SVC] = (esp_gatts_attr_db_t){
        auto_rsp, { ESP_UUID_LEN_16, (uint8_t *)&u_primary, ESP_GATT_PERM_READ, 16, 16, (uint8_t *)u_svc }
    };

    s_db[IDX_CMD_DECL] = (esp_gatts_attr_db_t){
        auto_rsp, { ESP_UUID_LEN_16, (uint8_t *)&u_decl, ESP_GATT_PERM_READ, 1, 1, (uint8_t *)&p_write }
    };

    s_db[IDX_CMD_VAL] = (esp_gatts_attr_db_t){
        app_rsp,  { ESP_UUID_LEN_128, (uint8_t *)u_cmd, perm_write_sec, CTL_PREP_BUF_MAX, 0, v_dummy }
    };

    s_db[IDX_NOTIFY_DECL] = (esp_gatts_attr_db_t){
        auto_rsp, { ESP_UUID_LEN_16, (uint8_t *)&u_decl, ESP_GATT_PERM_READ, 1, 1, (uint8_t *)&p_notify }
    };

    s_db[IDX_NOTIFY_VAL] = (esp_gatts_attr_db_t){
        app_rsp,  { ESP_UUID_LEN_128, (uint8_t *)u_notify, perm_read_sec, CTL_PREP_BUF_MAX, 0, v_dummy }
    };

    s_db[IDX_NOTIFY_CCC] = (esp_gatts_attr_db_t){
        auto_rsp, { ESP_UUID_LEN_16, (uint8_t *)&u_ccc, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, 2, 2, v_ccc }
    };
}

static void gatts_cb(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *p)
{
    switch (event) {
    case ESP_GATTS_REG_EVT:
        if (p->reg.app_id != BT_CTL_APP_ID || p->reg.status != ESP_GATT_OK) {
            return;
        }
        s_gatts_if = gatts_if;
        if (esp_ble_gatts_create_attr_tab(s_db, gatts_if, IDX_NB, 0) != ESP_OK) {
            ESP_LOGE(TAG, "Criacao da tabela de atributos GATT de controle falhou");
        }
        break;

    case ESP_GATTS_CREAT_ATTR_TAB_EVT:
        if (gatts_if != s_gatts_if) {
            break;
        }
        if (p->add_attr_tab.status != ESP_GATT_OK || p->add_attr_tab.num_handle != IDX_NB) {
            ESP_LOGE(TAG, "Tabela GATT invalida (status %d, %d handles)", p->add_attr_tab.status,
                     p->add_attr_tab.num_handle);
            break;
        }
        memcpy(s_handles, p->add_attr_tab.handles, sizeof(s_handles));
        esp_ble_gatts_start_service(s_handles[IDX_SVC]);
        ESP_LOGI(TAG, "Servico GATT de Controle e Telemetria ativo (Handle base: %u)", s_handles[IDX_SVC]);
        break;

    case ESP_GATTS_CONNECT_EVT:
        if (gatts_if == s_gatts_if) {
            s_conn_id = p->connect.conn_id;
            s_mtu = 23; /* Reinicia para o MTU padrao ate o cliente solicitar MTU estendido */
            ESP_LOGI(TAG, "Cliente conectou ao servico de controle (conn_id: %u)", s_conn_id);
        }
        break;

    case ESP_GATTS_MTU_EVT:
        if (gatts_if == s_gatts_if) {
            s_mtu = p->mtu.mtu;
            ESP_LOGI(TAG, "MTU BLE atualizado para conn_id %u: %u bytes", p->mtu.conn_id, s_mtu);
        }
        break;

    case ESP_GATTS_DISCONNECT_EVT:
        if (gatts_if == s_gatts_if && p->disconnect.conn_id == s_conn_id) {
            ESP_LOGI(TAG, "Cliente desconectou do servico de controle (conn_id: %u)", s_conn_id);
            phone_ctl_notify_disconnect(s_conn_id);
            s_conn_id = 0xFFFF;
            s_notify_enabled = false;
            s_prep_len = 0;
        }
        break;

    case ESP_GATTS_READ_EVT:
        if (gatts_if != s_gatts_if) {
            break;
        }
        if (p->read.handle == s_handles[IDX_NOTIFY_VAL]) {
            esp_gatt_rsp_t rsp = { 0 };
            rsp.attr_value.handle = p->read.handle;
            rsp.attr_value.len = 1;
            rsp.attr_value.value[0] = 0;
            esp_ble_gatts_send_response(gatts_if, p->read.conn_id, p->read.trans_id, ESP_GATT_OK, &rsp);
        } else {
            esp_ble_gatts_send_response(gatts_if, p->read.conn_id, p->read.trans_id, ESP_GATT_READ_NOT_PERMIT, NULL);
        }
        break;

    case ESP_GATTS_WRITE_EVT: {
        if (gatts_if != s_gatts_if) {
            break;
        }
        uint16_t h = p->write.handle;

        /* Atualizacao do descritor de configuracao de notificacao (CCCD) */
        if (h == s_handles[IDX_NOTIFY_CCC]) {
            s_notify_enabled = (p->write.len >= 1 && (p->write.value[0] & 0x01));
            ESP_LOGI(TAG, "Telemetria BLE NOTIFY %s para conn_id %u",
                     s_notify_enabled ? "HABILITADA" : "DESABILITADA", p->write.conn_id);
            if (p->write.need_rsp) {
                esp_ble_gatts_send_response(gatts_if, p->write.conn_id, p->write.trans_id, ESP_GATT_OK, NULL);
            }
            if (s_notify_enabled) {
                /* Notifica o actor phone_ctl para enviar evento de boas-vindas ("app.connected") */
                phone_ctl_notify_connect(p->write.conn_id);
            }
            break;
        }

        if (h != s_handles[IDX_CMD_VAL]) {
            break;
        }

        /* Suporte a escritas fragmentadas preparadas (Long Write / Prepare Write) */
        if (p->write.is_prep) {
            esp_gatt_status_t status = ESP_GATT_OK;
            if (p->write.offset + p->write.len > sizeof(s_prep_buf)) {
                status = ESP_GATT_INVALID_ATTR_LEN;
            } else {
                if (p->write.offset == 0) {
                    s_prep_len = 0;
                }
                memcpy(s_prep_buf + p->write.offset, p->write.value, p->write.len);
                s_prep_len = p->write.offset + p->write.len;
                s_prep_handle = h;
            }

            if (p->write.need_rsp) {
                esp_gatt_rsp_t rsp = { 0 };
                rsp.attr_value.handle = h;
                rsp.attr_value.offset = p->write.offset;
                rsp.attr_value.len = (p->write.len < sizeof(rsp.attr_value.value)) ? p->write.len : sizeof(rsp.attr_value.value);
                memcpy(rsp.attr_value.value, p->write.value, rsp.attr_value.len);
                esp_ble_gatts_send_response(gatts_if, p->write.conn_id, p->write.trans_id, status, &rsp);
            }
            break;
        }

        /* Encaminha dados para processamento no actor phone_ctl antes de confirmar ao rádio BLE */
        esp_err_t post_err = phone_ctl_post_incoming_data(p->write.conn_id, p->write.value, p->write.len);

        /* Responde com sucesso ou busy se o cliente solicitou confirmacao */
        if (p->write.need_rsp) {
            esp_gatt_status_t gatt_status = (post_err == ESP_OK) ? ESP_GATT_OK : ESP_GATT_BUSY;
            esp_ble_gatts_send_response(gatts_if, p->write.conn_id, p->write.trans_id, gatt_status, NULL);
        }
        break;
    }

    case ESP_GATTS_EXEC_WRITE_EVT:
        if (gatts_if != s_gatts_if) {
            break;
        }
        esp_err_t post_exec_err = ESP_OK;
        if (p->exec_write.exec_write_flag == ESP_GATT_PREP_WRITE_EXEC && s_prep_len > 0) {
            post_exec_err = phone_ctl_post_incoming_data(p->exec_write.conn_id, s_prep_buf, s_prep_len);
            s_prep_len = 0;
        }
        esp_gatt_status_t exec_status = (post_exec_err == ESP_OK) ? ESP_GATT_OK : ESP_GATT_BUSY;
        esp_ble_gatts_send_response(gatts_if, p->exec_write.conn_id, p->exec_write.trans_id, exec_status, NULL);
        break;

    default:
        break;
    }
}

esp_err_t bt_ctl_service_init(void)
{
    build_db();
    esp_err_t err = bt_ble_gatts_register(BT_CTL_APP_ID, gatts_cb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao registrar servico GATT de controle: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "Servico de controle BLE registrado com sucesso");
    return ESP_OK;
}

esp_err_t bt_ctl_service_send_notify(uint16_t conn_id, const uint8_t *data, size_t len)
{
    if (!s_notify_enabled || s_conn_id == 0xFFFF || s_gatts_if == ESP_GATT_IF_NONE) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!data || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    /* O tamanho maximo de um payload de Notificacao ATT e limitado por (MTU - 3).
     * Alem disso, o stack Bluedroid rejeita com ESP_ERR_INVALID_SIZE se len > ESP_GATT_MAX_ATTR_LEN (600).
     * Limitamos defensivamente o chunk a no maximo 500 bytes para garantir compatibilidade e margem segura. */
    uint16_t max_chunk = (s_mtu > 3) ? (s_mtu - 3) : 20;
    if (max_chunk > 500) {
        max_chunk = 500;
    }

    /* Se o payload couber em uma unica notificacao ATT, envia diretamente sem overhead */
    if (len <= max_chunk) {
        esp_err_t err = esp_ble_gatts_send_indicate(s_gatts_if, conn_id, s_handles[IDX_NOTIFY_VAL],
                                                    (uint16_t)len, (uint8_t *)data, false);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "esp_ble_gatts_send_indicate (conn_id %u, len %u): %s",
                     conn_id, (unsigned)len, esp_err_to_name(err));
        }
        return err;
    }

    /* Payload longo (ex: catalogo de midia com 6 fotos e 6 GIFs totalizando > 600 bytes):
     * Fragmenta ordenadamente em chunks de ate max_chunk bytes.
     * O Companion App Android possui suporte nativo em parseIncomingTelemetryChunk()
     * que acumula chunks no incomingBuffer ate detectar o encerramento do JSON ('}' ou '\n'). */
    ESP_LOGI(TAG, "Notificacao longa (%u bytes): fragmentando em pacotes de ate %u bytes (conn_id %u)",
             (unsigned)len, (unsigned)max_chunk, conn_id);

    size_t sent = 0;
    while (sent < len) {
        size_t chunk_len = len - sent;
        if (chunk_len > max_chunk) {
            chunk_len = max_chunk;
        }

        esp_err_t err = esp_ble_gatts_send_indicate(s_gatts_if, conn_id, s_handles[IDX_NOTIFY_VAL],
                                                    (uint16_t)chunk_len, (uint8_t *)(data + sent), false);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Falha ao enviar fragmento GATT (%u bytes em offset %u/%u): %s",
                     (unsigned)chunk_len, (unsigned)sent, (unsigned)len, esp_err_to_name(err));
            return err;
        }

        sent += chunk_len;
        if (sent < len) {
            /* Pausa curta (15 ms) para permitir que a fila de pacotes L2CAP do controlador
             * Bluetooth processe e transmita cada fragmento antes do proximo envio */
            vTaskDelay(pdMS_TO_TICKS(15));
        }
    }

    return ESP_OK;
}

bool bt_ctl_service_is_notify_enabled(void)
{
    return s_notify_enabled && (s_conn_id != 0xFFFF);
}

uint16_t bt_ctl_service_get_conn_id(void)
{
    return s_conn_id;
}

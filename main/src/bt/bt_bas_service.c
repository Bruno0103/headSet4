/**
 * @file bt_bas_service.c
 * @brief Implementacao do servico padrao Bluetooth SIG Battery Service (BAS, 0x180F).
 *
 * Em conformidade com AGENTS.md:
 * - Handlers assincronos, sem bloqueio (sem vTaskDelay / portMAX_DELAY).
 * - Ouve eventos SENSOR_EVT_BATTERY postados no barramento de fatos hs_events.
 * - Registrado no despachante central BLE (bt_ble.c).
 */

#include "bt_bas_service.h"

#include <string.h>

#include "esp_check.h"
#include "esp_gatts_api.h"
#include "esp_gatt_common_api.h"
#include "esp_log.h"

#include "bt_ble.h"
#include "hs_events.h"

static const char *TAG = "bt_bas";

/* Indices da tabela de atributos do Servico BAS */
enum {
    IDX_SVC,
    IDX_BAT_LEVEL_DECL,
    IDX_BAT_LEVEL_VAL,
    IDX_BAT_LEVEL_CCC,
    IDX_NB
};

static const uint16_t u_primary    = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t u_decl       = ESP_GATT_UUID_CHAR_DECLARE;
static const uint16_t u_ccc        = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
static const uint16_t u_bas_svc    = ESP_GATT_UUID_BATTERY_SERVICE_SVC; /* 0x180F */
static const uint16_t u_bat_level  = ESP_GATT_UUID_BATTERY_LEVEL;       /* 0x2A19 */

static const uint8_t  p_read_notify = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY;

static uint8_t        s_battery_percent = 0;
/* Falso até chegar a 1ª leitura real: evita informar 100% falso ao host (WP-BAT.2) */
static bool           s_have_value      = false;
static uint8_t        s_ccc[2]          = {0, 0};

static esp_gatts_attr_db_t s_db[IDX_NB];
static esp_gatt_if_t       s_gatts_if = ESP_GATT_IF_NONE;
static uint16_t            s_handles[IDX_NB];

/* O bt_ble aceita 2 conexões BLE simultâneas; cada uma tem seu próprio CCCD.
 * Antes só o último conn_id era lembrado e o outro host nunca recebia notify. */
#define BAS_MAX_CONN 2
typedef struct { bool used; bool notify; uint16_t conn_id; } bas_conn_t;
static bas_conn_t s_conns[BAS_MAX_CONN];

static bas_conn_t *find_conn(uint16_t conn_id)
{
    for (int i = 0; i < BAS_MAX_CONN; i++) {
        if (s_conns[i].used && s_conns[i].conn_id == conn_id) return &s_conns[i];
    }
    return NULL;
}

/* Envia o valor atual a uma conexão (notify, sem confirmação). */
static void notify_one(const bas_conn_t *c)
{
    if (!s_have_value || s_gatts_if == ESP_GATT_IF_NONE) return;
    esp_err_t err = esp_ble_gatts_send_indicate(s_gatts_if, c->conn_id, s_handles[IDX_BAT_LEVEL_VAL],
                                                1, &s_battery_percent, false);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Bateria %u%% notificada via BAS (conn_id=%u)", s_battery_percent, c->conn_id);
    } else {
        ESP_LOGW(TAG, "Falha no notify BAS conn_id=%u (%s)", c->conn_id, esp_err_to_name(err));
    }
}

/* Constroi a tabela padrao do Bluetooth SIG para o Battery Service */
static void build_db(void)
{
    const esp_attr_control_t auto_rsp = { ESP_GATT_AUTO_RSP };
    const esp_attr_control_t app_rsp  = { ESP_GATT_RSP_BY_APP };

    /* 1. Declaracao do Servico BAS (UUID 0x180F) */
    s_db[IDX_SVC] = (esp_gatts_attr_db_t){
        auto_rsp,
        { ESP_UUID_LEN_16, (uint8_t *)&u_primary, ESP_GATT_PERM_READ, 2, 2, (uint8_t *)&u_bas_svc }
    };

    /* 2. Declaracao da Caracteristica Battery Level */
    s_db[IDX_BAT_LEVEL_DECL] = (esp_gatts_attr_db_t){
        auto_rsp,
        { ESP_UUID_LEN_16, (uint8_t *)&u_decl, ESP_GATT_PERM_READ, 1, 1, (uint8_t *)&p_read_notify }
    };

    /* 3. Valor da Caracteristica Battery Level (UUID 0x2A19) */
    s_db[IDX_BAT_LEVEL_VAL] = (esp_gatts_attr_db_t){
        app_rsp,
        { ESP_UUID_LEN_16, (uint8_t *)&u_bat_level, ESP_GATT_PERM_READ, 1, 1, &s_battery_percent }
    };

    /* 4. Descritor CCCD (0x2902) para Notificacoes do Host */
    s_db[IDX_BAT_LEVEL_CCC] = (esp_gatts_attr_db_t){
        auto_rsp,
        { ESP_UUID_LEN_16, (uint8_t *)&u_ccc, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, 2, 2, s_ccc }
    };
}

/* Callback GATT para os eventos do servico BAS */
static void gatts_cb(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    switch (event) {
    case ESP_GATTS_REG_EVT:
        if (param->reg.status == ESP_GATT_OK) {
            s_gatts_if = gatts_if;
            build_db();
            esp_ble_gatts_create_attr_tab(s_db, gatts_if, IDX_NB, 0);
        } else {
            ESP_LOGE(TAG, "Falha no registro do servico BAS: status %d", param->reg.status);
        }
        break;

    case ESP_GATTS_CREAT_ATTR_TAB_EVT:
        if (param->add_attr_tab.status == ESP_GATT_OK && param->add_attr_tab.num_handle == IDX_NB) {
            memcpy(s_handles, param->add_attr_tab.handles, sizeof(s_handles));
            esp_ble_gatts_start_service(s_handles[IDX_SVC]);
            ESP_LOGI(TAG, "GATT Battery Service (0x180F) ativo com sucesso");
        } else {
            ESP_LOGE(TAG, "Falha ao criar tabela de atributos BAS: status %d", param->add_attr_tab.status);
        }
        break;

    case ESP_GATTS_CONNECT_EVT: {
        s_gatts_if = gatts_if;
        bas_conn_t *c = find_conn(param->connect.conn_id);
        for (int i = 0; !c && i < BAS_MAX_CONN; i++) {
            if (!s_conns[i].used) c = &s_conns[i];
        }
        if (c) {
            c->used = true;
            c->notify = false;
            c->conn_id = param->connect.conn_id;
        }
        ESP_LOGI(TAG, "Host BLE conectado ao BAS (conn_id=%u)", param->connect.conn_id);
        break;
    }

    case ESP_GATTS_DISCONNECT_EVT: {
        bas_conn_t *c = find_conn(param->disconnect.conn_id);
        if (c) {
            memset(c, 0, sizeof(*c));
            ESP_LOGI(TAG, "Host BLE desconectado do BAS (conn_id=%u)", param->disconnect.conn_id);
        }
        break;
    }

    case ESP_GATTS_READ_EVT: {
        esp_gatt_rsp_t rsp;
        memset(&rsp, 0, sizeof(rsp));
        rsp.attr_value.handle = param->read.handle;

        if (param->read.handle == s_handles[IDX_BAT_LEVEL_VAL]) {
            if (!s_have_value) {
                /* Ainda sem leitura do ADC: erro é melhor que um 100% falso */
                esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id, ESP_GATT_ERROR, &rsp);
                break;
            }
            rsp.attr_value.len = 1;
            rsp.attr_value.value[0] = s_battery_percent;
            esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id, ESP_GATT_OK, &rsp);
            ESP_LOGD(TAG, "Leitura de bateria por BLE atendida: %u%%", s_battery_percent);
        } else {
            esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id, ESP_GATT_READ_NOT_PERMIT, &rsp);
        }
        break;
    }

    case ESP_GATTS_WRITE_EVT:
        if (param->write.handle == s_handles[IDX_BAT_LEVEL_CCC]) {
            bas_conn_t *c = find_conn(param->write.conn_id);
            if (c && param->write.len == 2) {
                uint16_t ccc_val = param->write.value[0] | (param->write.value[1] << 8);
                c->notify = (ccc_val & 0x0001) != 0;
                s_ccc[0] = param->write.value[0];
                s_ccc[1] = param->write.value[1];
                ESP_LOGI(TAG, "Notificacao BAS (CCCD) conn_id=%u: %s", c->conn_id, c->notify ? "ATIVADA" : "DESATIVADA");

                /* Envia imediatamente o valor atual assim que as notificacoes forem habilitadas */
                if (c->notify) notify_one(c);
            }
        }
        break;

    default:
        break;
    }
}

/* Handler do barramento central de eventos: reage a SENSOR_EVT_BATTERY */
static void on_sensor_battery_event(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    (void)handler_args; (void)base;
    if (id == SENSOR_EVT_BATTERY && event_data) {
        const sensor_battery_evt_t *bat = (const sensor_battery_evt_t *)event_data;
        s_battery_percent = bat->percent > 100 ? 100 : bat->percent;
        s_have_value = true;

        /* Notifica TODOS os hosts conectados com CCCD ligado, a cada leitura */
        for (int i = 0; i < BAS_MAX_CONN; i++) {
            if (s_conns[i].used && s_conns[i].notify) notify_one(&s_conns[i]);
        }
    }
}

esp_err_t bt_bas_service_init(void)
{
    /* Registra o listener no barramento central de eventos */
    esp_err_t err = hs_event_register(SENSOR_EVT, SENSOR_EVT_BATTERY, on_sensor_battery_event, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao registrar listener SENSOR_EVT_BATTERY: %s", esp_err_to_name(err));
        return err;
    }

    /* Registra a aplicacao GATT no despachante BLE central */
    err = bt_ble_gatts_register(BT_BAS_APP_ID, gatts_cb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao registrar servico BAS no bt_ble: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Servico Bluetooth SIG Battery Service (BAS) inicializado com sucesso");
    return ESP_OK;
}

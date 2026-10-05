#include "bt_eq_service.h"

#include <string.h>

#include "esp_gatts_api.h"
#include "esp_log.h"

#include "bt_ble.h"
#include "eq.h"

static const char *TAG = "bt_eq";

#define EQ_APP_ID 0x45

/* 5E0A1Bxx-4D2F-4A7B-9C31-0E5F2A6B7C8D em little-endian */
#define EQ_UUID128(xx) { 0x8D, 0x7C, 0x6B, 0x2A, 0x5F, 0x0E, 0x31, 0x9C, 0x7B, 0x4A, 0x2F, 0x4D, (xx), 0x1B, 0x0A, 0x5E }

enum { IDX_SVC, IDX_PRESET_DECL, IDX_PRESET_VAL, IDX_GAINS_DECL, IDX_GAINS_VAL, IDX_NB };

static const uint16_t u_primary = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t u_decl    = ESP_GATT_UUID_CHAR_DECLARE;
static const uint8_t  u_svc[16]    = EQ_UUID128(0x00);
static const uint8_t  u_preset[16] = EQ_UUID128(0x01);
static const uint8_t  u_gains[16]  = EQ_UUID128(0x02);
static const uint8_t  p_rw = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE;
static uint8_t        v_preset;
static uint8_t        v_gains[EQ_NUM_BANDS];

static esp_gatts_attr_db_t s_db[IDX_NB];
static esp_gatt_if_t       s_if = ESP_GATT_IF_NONE;
static uint16_t            s_handles[IDX_NB];

static void build_db(void)
{
    const esp_attr_control_t auto_rsp = { ESP_GATT_AUTO_RSP };
    const esp_attr_control_t app_rsp  = { ESP_GATT_RSP_BY_APP };
    const uint16_t rw = ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE;

    s_db[IDX_SVC] = (esp_gatts_attr_db_t){ auto_rsp, { ESP_UUID_LEN_16, (uint8_t *)&u_primary, ESP_GATT_PERM_READ, 16, 16, (uint8_t *)u_svc } };
    s_db[IDX_PRESET_DECL] = (esp_gatts_attr_db_t){ auto_rsp, { ESP_UUID_LEN_16, (uint8_t *)&u_decl, ESP_GATT_PERM_READ, 1, 1, (uint8_t *)&p_rw } };
    s_db[IDX_PRESET_VAL] = (esp_gatts_attr_db_t){ app_rsp, { ESP_UUID_LEN_128, (uint8_t *)u_preset, rw, 1, 1, &v_preset } };
    s_db[IDX_GAINS_DECL] = (esp_gatts_attr_db_t){ auto_rsp, { ESP_UUID_LEN_16, (uint8_t *)&u_decl, ESP_GATT_PERM_READ, 1, 1, (uint8_t *)&p_rw } };
    s_db[IDX_GAINS_VAL] = (esp_gatts_attr_db_t){ app_rsp, { ESP_UUID_LEN_128, (uint8_t *)u_gains, rw, EQ_NUM_BANDS, EQ_NUM_BANDS, v_gains } };
}

static esp_gatt_status_t handle_write(uint16_t handle, const uint8_t *data, uint16_t len)
{
    if (handle == s_handles[IDX_PRESET_VAL]) {
        if (len != 1 || data[0] >= EQ_PRESET_CUSTOM) return ESP_GATT_INVALID_ATTR_LEN;
        return eq_set_preset((eq_preset_t)data[0]) == ESP_OK ? ESP_GATT_OK : ESP_GATT_ERROR;
    }
    if (handle == s_handles[IDX_GAINS_VAL]) {
        if (len != EQ_NUM_BANDS) return ESP_GATT_INVALID_ATTR_LEN;
        int8_t g[EQ_NUM_BANDS];
        memcpy(g, data, sizeof g);
        return eq_set_gains(g) == ESP_OK ? ESP_GATT_OK : ESP_GATT_OUT_OF_RANGE;
    }
    return ESP_GATT_WRITE_NOT_PERMIT;
}

static void gatts_cb(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *p)
{
    if (event == ESP_GATTS_REG_EVT) {
        if (p->reg.app_id != EQ_APP_ID || p->reg.status != ESP_GATT_OK) return;
        s_if = gatts_if;
        if (esp_ble_gatts_create_attr_tab(s_db, gatts_if, IDX_NB, 0) != ESP_OK) {
            ESP_LOGE(TAG, "create_attr_tab falhou");
        }
        return;
    }
    if (gatts_if != s_if) return;

    switch (event) {
    case ESP_GATTS_CREAT_ATTR_TAB_EVT:
        if (p->add_attr_tab.status != ESP_GATT_OK || p->add_attr_tab.num_handle != IDX_NB) {
            ESP_LOGE(TAG, "Tabela GATT invalida (status %d)", p->add_attr_tab.status);
            return;
        }
        memcpy(s_handles, p->add_attr_tab.handles, sizeof s_handles);
        esp_ble_gatts_start_service(s_handles[IDX_SVC]);
        ESP_LOGI(TAG, "Servico GATT de EQ ativo");
        break;

    case ESP_GATTS_READ_EVT: {
        esp_gatt_rsp_t rsp = { 0 };
        rsp.attr_value.handle = p->read.handle;
        if (p->read.handle == s_handles[IDX_PRESET_VAL]) {
            rsp.attr_value.len = 1;
            rsp.attr_value.value[0] = (uint8_t)eq_get_preset();
        } else if (p->read.handle == s_handles[IDX_GAINS_VAL]) {
            rsp.attr_value.len = EQ_NUM_BANDS;
            eq_get_gains((int8_t *)rsp.attr_value.value);
        } else {
            esp_ble_gatts_send_response(gatts_if, p->read.conn_id, p->read.trans_id, ESP_GATT_READ_NOT_PERMIT, NULL);
            break;
        }
        esp_ble_gatts_send_response(gatts_if, p->read.conn_id, p->read.trans_id, ESP_GATT_OK, &rsp);
        break;
    }

    case ESP_GATTS_WRITE_EVT: {
        if (p->write.is_prep) {
            if (p->write.need_rsp) {
                esp_ble_gatts_send_response(gatts_if, p->write.conn_id, p->write.trans_id, ESP_GATT_REQ_NOT_SUPPORTED, NULL);
            }
            break;
        }
        esp_gatt_status_t st = handle_write(p->write.handle, p->write.value, p->write.len);
        if (st != ESP_GATT_OK) {
            ESP_LOGW(TAG, "Escrita recusada (status %d, len %u)", st, p->write.len);
        }
        if (p->write.need_rsp) {
            esp_ble_gatts_send_response(gatts_if, p->write.conn_id, p->write.trans_id, st, NULL);
        }
        break;
    }

    default:
        break;
    }
}

esp_err_t bt_eq_service_init(void)
{
    build_db();
    return bt_ble_gatts_register(EQ_APP_ID, gatts_cb);
}

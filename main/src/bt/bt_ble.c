#include "bt_ble.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"
#include <string.h>

#define TAG "BT_BLE"
#define PROFILE_APP_ID 0
#define DEVICE_NAME "HeadSet4_LE"

// Intervalos de Advertising em unidades de 0.625 ms
#define ADV_INTERVAL_FAST_MIN  0x30  // ~30 ms
#define ADV_INTERVAL_FAST_MAX  0x40  // ~40 ms
#define ADV_INTERVAL_SLOW_MIN  0x800 // ~1.28 s
#define ADV_INTERVAL_SLOW_MAX  0x800 // ~1.28 s

static uint8_t adv_config_done = 0;
#define ADV_CONFIG_FLAG      (1 << 0)
#define SCAN_RSP_CONFIG_FLAG (1 << 1)

static uint8_t raw_adv_data[] = {
    0x02, 0x01, 0x06,
    0x02, 0x0A, 0xEB,
    0x0C, 0x09, 'H', 'e', 'a', 'd', 'S', 'e', 't', '4', '_', 'L', 'E'
};

static uint8_t raw_scan_rsp_data[] = {
    0x02, 0x01, 0x06,
    0x02, 0x0A, 0xEB,
};

static esp_ble_adv_params_t adv_params = {
    .adv_int_min        = ADV_INTERVAL_FAST_MIN,
    .adv_int_max        = ADV_INTERVAL_FAST_MAX,
    .adv_type           = ADV_TYPE_IND,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy  = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

// GATT Profile
struct gatts_profile_inst {
    esp_gatts_cb_t gatts_cb;
    uint16_t gatts_if;
    uint16_t app_id;
    uint16_t conn_id;
    uint16_t service_handle;
    esp_gatt_srvc_id_t service_id;
    uint16_t char_handle;
    esp_bt_uuid_t char_uuid;
    esp_gatt_perm_t perm;
    esp_gatt_char_prop_t property;
    uint16_t descr_handle;
    esp_bt_uuid_t descr_uuid;
};

static void gatts_profile_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);

static struct gatts_profile_inst gl_profile_tab[1] = {
    [PROFILE_APP_ID] = {
        .gatts_cb = gatts_profile_event_handler,
        .gatts_if = ESP_GATT_IF_NONE,
    },
};

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
        adv_config_done &= (~ADV_CONFIG_FLAG);
        if (adv_config_done == 0) {
            esp_ble_gap_start_advertising(&adv_params);
        }
        break;
    case ESP_GAP_BLE_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT:
        adv_config_done &= (~SCAN_RSP_CONFIG_FLAG);
        if (adv_config_done == 0) {
            esp_ble_gap_start_advertising(&adv_params);
        }
        break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(TAG, "Falha ao iniciar advertising BLE");
        } else {
            ESP_LOGI(TAG, "Advertising BLE iniciado com sucesso");
        }
        break;
    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        if (param->adv_stop_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(TAG, "Falha ao parar advertising BLE");
        } else {
            ESP_LOGI(TAG, "Advertising BLE parado. Reiniciando com novos parametros...");
            esp_ble_gap_start_advertising(&adv_params);
        }
        break;
    case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
        ESP_LOGI(TAG, "Parametros de conexao atualizados");
        break;
    default:
        break;
    }
}

static void gatts_profile_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param) {
    switch (event) {
    case ESP_GATTS_REG_EVT:
        ESP_LOGI(TAG, "GATT Server registrado, app_id %04x", param->reg.app_id);
        esp_ble_gap_set_device_name(DEVICE_NAME);
        
        esp_err_t raw_adv_ret = esp_ble_gap_config_adv_data_raw(raw_adv_data, sizeof(raw_adv_data));
        if (raw_adv_ret) {
            ESP_LOGE(TAG, "Falha config raw adv data: %d", raw_adv_ret);
        }
        adv_config_done |= ADV_CONFIG_FLAG;
        
        esp_err_t raw_scan_ret = esp_ble_gap_config_scan_rsp_data_raw(raw_scan_rsp_data, sizeof(raw_scan_rsp_data));
        if (raw_scan_ret) {
            ESP_LOGE(TAG, "Falha config raw scan rsp data: %d", raw_scan_ret);
        }
        adv_config_done |= SCAN_RSP_CONFIG_FLAG;
        break;
    case ESP_GATTS_CONNECT_EVT:
        ESP_LOGI(TAG, "BLE Conectado, conn_id %d, remoto %02x:%02x:%02x:%02x:%02x:%02x",
                 param->connect.conn_id,
                 param->connect.remote_bda[0], param->connect.remote_bda[1], param->connect.remote_bda[2],
                 param->connect.remote_bda[3], param->connect.remote_bda[4], param->connect.remote_bda[5]);
        gl_profile_tab[PROFILE_APP_ID].conn_id = param->connect.conn_id;
        break;
    case ESP_GATTS_DISCONNECT_EVT:
        ESP_LOGI(TAG, "BLE Desconectado, conn_id %d", param->disconnect.conn_id);
        esp_ble_gap_start_advertising(&adv_params);
        break;
    case ESP_GATTS_READ_EVT:
        // Mock de resposta de leitura (Ex: Bateria ou EQ)
        break;
    case ESP_GATTS_WRITE_EVT:
        ESP_LOGI(TAG, "GATT Write event, len %d", param->write.len);
        // Aqui conectariamos a logica do LVGL para EQ
        break;
    default:
        break;
    }
}

static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    if (event == ESP_GATTS_REG_EVT) {
        if (param->reg.status == ESP_GATT_OK) {
            gl_profile_tab[param->reg.app_id].gatts_if = gatts_if;
        } else {
            ESP_LOGI(TAG, "Falha no registro app_id %04x, status %d", param->reg.app_id, param->reg.status);
            return;
        }
    }
    
    do {
        int idx;
        for (idx = 0; idx < 1; idx++) {
            if (gatts_if == ESP_GATT_IF_NONE || gatts_if == gl_profile_tab[idx].gatts_if) {
                if (gl_profile_tab[idx].gatts_cb) {
                    gl_profile_tab[idx].gatts_cb(event, gatts_if, param);
                }
            }
        }
    } while (0);
}

esp_err_t bt_ble_init(void)
{
    esp_err_t ret;
    
    // O gap controller já deve estar init no modo dual. Aqui registramos os callbacks de BLE.
    ret = esp_ble_gap_register_callback(gap_event_handler);
    if (ret) {
        ESP_LOGE(TAG, "Falha no registro do GAP callback: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = esp_ble_gatts_register_callback(gatts_event_handler);
    if (ret) {
        ESP_LOGE(TAG, "Falha no registro do GATTS callback: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = esp_ble_gatts_app_register(PROFILE_APP_ID);
    if (ret) {
        ESP_LOGE(TAG, "Falha no registro do GATTS app: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "BLE GATT Server Inicializado");
    return ESP_OK;
}

void bt_ble_set_adv_fast(void)
{
    ESP_LOGI(TAG, "Trocando BLE Advertising para modo RÁPIDO (30ms)");
    adv_params.adv_int_min = ADV_INTERVAL_FAST_MIN;
    adv_params.adv_int_max = ADV_INTERVAL_FAST_MAX;
    esp_ble_gap_stop_advertising(); // O evento de stop chamará o start novamente com os novos params
}

void bt_ble_set_adv_slow(void)
{
    ESP_LOGI(TAG, "Trocando BLE Advertising para modo LENTO (1.28s)");
    adv_params.adv_int_min = ADV_INTERVAL_SLOW_MIN;
    adv_params.adv_int_max = ADV_INTERVAL_SLOW_MAX;
    esp_ble_gap_stop_advertising();
}

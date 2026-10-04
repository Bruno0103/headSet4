#include "bt_ble.h"

#include <stdbool.h>
#include <string.h>

#include "esp_bt.h"
#include "esp_check.h"
#include "esp_gap_ble_api.h"
#include "esp_gatt_common_api.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "bt_ble";

#define MAX_APPS         4
#define MAX_BLE_CONN     2          /* = CONFIG_BTDM_CTRL_BLE_MAX_CONN */
#define ADV_MAX_LEN      31

/* Intervalos em unidades de 0.625 ms */
#define ADV_PAIRING_MIN  0x0080     /*  80 ms */
#define ADV_PAIRING_MAX  0x00A0     /* 100 ms (limite do Fast Pair) */
#define ADV_ACTIVE_MIN   0x0320     /* 500 ms */
#define ADV_ACTIVE_MAX   0x03E0     /* 620 ms */
#define ADV_STREAM_MIN   0x0C80     /* 2.0 s  */
#define ADV_STREAM_MAX   0x0D00
#define ADV_SLEEP_MIN    0x0800     /* 1.28 s */
#define ADV_SLEEP_MAX    0x0800

#define CFG_ADV_BIT      (1 << 0)
#define CFG_SCAN_BIT     (1 << 1)

typedef struct {
    uint16_t       app_id;
    esp_gatt_if_t  gatts_if;
    esp_gatts_cb_t cb;
    bool           used;
} gatts_app_t;

static SemaphoreHandle_t     s_lock;
static gatts_app_t           s_apps[MAX_APPS];
static bt_ble_adv_provider_t s_provider;
static char                  s_name[32];

static bt_ble_profile_t s_want = BT_BLE_ADV_OFF;   /* pedido pelo link_mgr           */
static bt_ble_profile_t s_cur  = BT_BLE_ADV_OFF;   /* o que esta sendo configurado   */
static bool    s_ready;          /* privacidade local configurada */
static bool    s_running;        /* anunciando no controlador     */
static bool    s_busy;           /* cadeia stop->config->start em andamento */
static bool    s_dirty;          /* mudou o pedido durante a cadeia */
static bool    s_streaming;
static uint8_t s_cfg_pending;
static uint8_t s_conn_ids;       /* bitmask de conn_id ativos     */

static esp_ble_adv_params_t s_params = {
    .adv_type          = ADV_TYPE_IND,
    .own_addr_type     = BLE_ADDR_TYPE_PUBLIC,
    .channel_map       = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static const char *profile_name(bt_ble_profile_t p)
{
    switch (p) {
    case BT_BLE_ADV_PAIRING: return "PAIRING";
    case BT_BLE_ADV_ACTIVE:  return "ACTIVE";
    case BT_BLE_ADV_SLEEP:   return "SLEEP";
    default:                 return "OFF";
    }
}

static int conn_count(void)
{
    return __builtin_popcount(s_conn_ids);
}

static void pick_params(void)
{
    switch (s_cur) {
    case BT_BLE_ADV_PAIRING:
        s_params.adv_int_min = ADV_PAIRING_MIN;
        s_params.adv_int_max = ADV_PAIRING_MAX;
        s_params.own_addr_type = BLE_ADDR_TYPE_PUBLIC;       /* nao rotaciona durante o pareamento */
        break;
    case BT_BLE_ADV_ACTIVE:
        s_params.adv_int_min = s_streaming ? ADV_STREAM_MIN : ADV_ACTIVE_MIN;
        s_params.adv_int_max = s_streaming ? ADV_STREAM_MAX : ADV_ACTIVE_MAX;
        s_params.own_addr_type = BLE_ADDR_TYPE_RPA_PUBLIC;
        break;
    default:
        s_params.adv_int_min = ADV_SLEEP_MIN;
        s_params.adv_int_max = ADV_SLEEP_MAX;
        s_params.own_addr_type = BLE_ADDR_TYPE_RPA_PUBLIC;
        break;
    }
}

static size_t build_adv(uint8_t *buf)
{
    size_t n = 0;
    if (s_cur == BT_BLE_ADV_PAIRING) {
        buf[n++] = 2; buf[n++] = 0x01; buf[n++] = 0x02;                       /* Flags: LE General Discoverable */
        buf[n++] = 2; buf[n++] = 0x0A; buf[n++] = (uint8_t)BT_BLE_ADV_TX_POWER_DBM; /* TX Power */
    }
    if (s_provider) {
        n += s_provider(s_cur, buf + n, ADV_MAX_LEN - n);
    }
    return n;
}

static size_t build_scan_rsp(uint8_t *buf)
{
    size_t len = strlen(s_name);
    bool shortened = len > ADV_MAX_LEN - 2;
    if (shortened) {
        len = ADV_MAX_LEN - 2;
    }
    buf[0] = (uint8_t)(len + 1);
    buf[1] = shortened ? 0x08 : 0x09;
    memcpy(buf + 2, s_name, len);
    return len + 2;
}

/* ---- cadeia stop -> config -> start (chamar com s_lock) ---- */

static void configure_locked(void)
{
    if (s_cur == BT_BLE_ADV_OFF || conn_count() >= MAX_BLE_CONN) {
        s_busy = false;
        ESP_LOGI(TAG, "Advertising parado (perfil %s, %d conexoes)", profile_name(s_cur), conn_count());
        return;
    }
    pick_params();

    uint8_t adv[ADV_MAX_LEN], scan[ADV_MAX_LEN];
    size_t adv_len  = build_adv(adv);
    size_t scan_len = build_scan_rsp(scan);

    s_cfg_pending = CFG_ADV_BIT | CFG_SCAN_BIT;
    esp_err_t err = esp_ble_gap_config_adv_data_raw(adv, adv_len);
    if (err == ESP_OK) {
        err = esp_ble_gap_config_scan_rsp_data_raw(scan, scan_len);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao configurar dados de advertising: %s", esp_err_to_name(err));
        s_cfg_pending = 0;
        s_busy = false;
    }
}

static void apply_locked(void)
{
    if (!s_ready) {
        return;
    }
    if (s_busy) {
        s_dirty = true;
        return;
    }
    s_busy  = true;
    s_dirty = false;
    s_cur   = s_want;

    if (s_running) {
        esp_err_t err = esp_ble_gap_stop_advertising();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "stop_advertising: %s", esp_err_to_name(err));
            s_busy = false;
        }
        return;   /* continua em ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT */
    }
    configure_locked();
}

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    switch (event) {
    case ESP_GAP_BLE_SET_LOCAL_PRIVACY_COMPLETE_EVT:
        if (param->local_privacy_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "Privacidade local (RPA) ativa");
        } else {
            ESP_LOGE(TAG, "Falha ao ativar privacidade local: %d (advertising seguira com endereco publico)",
                     param->local_privacy_cmpl.status);
        }
        s_ready = true;
        apply_locked();
        break;

    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
    case ESP_GAP_BLE_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT: {
        s_cfg_pending &= ~(event == ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT ? CFG_ADV_BIT : CFG_SCAN_BIT);
        if (s_cfg_pending != 0) {
            break;
        }
        if (s_dirty) {           /* pedido mudou no meio: recomeca com o perfil novo */
            s_busy = false;
            apply_locked();
            break;
        }
        esp_err_t err = esp_ble_gap_start_advertising(&s_params);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "start_advertising: %s", esp_err_to_name(err));
            s_busy = false;
        }
        break;
    }

    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        s_busy = false;
        s_running = param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS;
        if (s_running) {
            ESP_LOGI(TAG, "Advertising %s: %u..%u (x0.625 ms), endereco %s", profile_name(s_cur),
                     s_params.adv_int_min, s_params.adv_int_max,
                     s_params.own_addr_type == BLE_ADDR_TYPE_PUBLIC ? "publico" : "RPA");
        } else {
            ESP_LOGE(TAG, "Falha ao iniciar advertising: %d", param->adv_start_cmpl.status);
        }
        if (s_dirty) {
            apply_locked();
        }
        break;

    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        s_running = false;
        s_dirty = false;
        s_cur = s_want;          /* usa sempre o pedido mais recente */
        configure_locked();
        break;

    default:
        break;
    }
    xSemaphoreGive(s_lock);
}

/* ---- GATTS: despacho por app + controle de conexoes ---- */

static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    bool restart_adv = false;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (event == ESP_GATTS_REG_EVT) {
        for (int i = 0; i < MAX_APPS; i++) {
            if (s_apps[i].used && s_apps[i].app_id == param->reg.app_id) {
                s_apps[i].gatts_if = (param->reg.status == ESP_GATT_OK) ? gatts_if : ESP_GATT_IF_NONE;
            }
        }
        if (param->reg.status != ESP_GATT_OK) {
            ESP_LOGE(TAG, "Registro do app GATT %u falhou (%d)", param->reg.app_id, param->reg.status);
        }
    } else if (event == ESP_GATTS_CONNECT_EVT && param->connect.conn_id < 8) {
        uint8_t before = s_conn_ids;
        s_conn_ids |= 1u << param->connect.conn_id;
        if (before != s_conn_ids) {
            ESP_LOGI(TAG, "BLE conectado (conn_id %u, %d/%d)", param->connect.conn_id, conn_count(), MAX_BLE_CONN);
            s_running = false;          /* a conexao interrompe o advertising */
            restart_adv = true;
        }
    } else if (event == ESP_GATTS_DISCONNECT_EVT && param->disconnect.conn_id < 8) {
        uint8_t before = s_conn_ids;
        s_conn_ids &= ~(1u << param->disconnect.conn_id);
        if (before != s_conn_ids) {
            ESP_LOGI(TAG, "BLE desconectado (conn_id %u, motivo 0x%02X)", param->disconnect.conn_id,
                     param->disconnect.reason);
            s_running = false;
            restart_adv = true;
        }
    }
    if (restart_adv) {
        s_busy = false;
        apply_locked();
    }

    gatts_app_t apps[MAX_APPS];
    memcpy(apps, s_apps, sizeof apps);
    xSemaphoreGive(s_lock);

    /* Callbacks dos servicos fora do lock (podem chamar de volta o bt_ble) */
    for (int i = 0; i < MAX_APPS; i++) {
        if (!apps[i].used || !apps[i].cb) {
            continue;
        }
        if (gatts_if == ESP_GATT_IF_NONE || apps[i].gatts_if == gatts_if ||
            (event == ESP_GATTS_REG_EVT && apps[i].app_id == param->reg.app_id)) {
            apps[i].cb(event, gatts_if, param);
        }
    }
}

/* ---- API ---- */

esp_err_t bt_ble_gatts_register(uint16_t app_id, esp_gatts_cb_t cb)
{
    ESP_RETURN_ON_FALSE(s_lock && cb, ESP_ERR_INVALID_STATE, TAG, "bt_ble_init() nao chamado");
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int slot = -1;
    for (int i = 0; i < MAX_APPS; i++) {
        if (!s_apps[i].used) {
            slot = slot < 0 ? i : slot;
        } else if (s_apps[i].app_id == app_id) {
            xSemaphoreGive(s_lock);
            return ESP_ERR_INVALID_ARG;
        }
    }
    if (slot < 0) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_NO_MEM;
    }
    s_apps[slot] = (gatts_app_t){ .app_id = app_id, .gatts_if = ESP_GATT_IF_NONE, .cb = cb, .used = true };
    xSemaphoreGive(s_lock);

    esp_err_t err = esp_ble_gatts_app_register(app_id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gatts_app_register(%u): %s", app_id, esp_err_to_name(err));
    }
    return err;
}

esp_err_t bt_ble_init(const char *device_name)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_NO_MEM, TAG, "mutex");
    }
    strlcpy(s_name, device_name ? device_name : "HeadSet", sizeof s_name);

    ESP_RETURN_ON_ERROR(esp_ble_gap_register_callback(gap_event_handler), TAG, "gap cb");
    ESP_RETURN_ON_ERROR(esp_ble_gatts_register_callback(gatts_event_handler), TAG, "gatts cb");
    ESP_RETURN_ON_ERROR(esp_ble_gap_set_device_name(s_name), TAG, "nome BLE");

    /* TX power fixa: o mesmo valor e anunciado no campo TX Power do Fast Pair (+3 dBm). */
    esp_err_t err = esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P3);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "tx_power_set: %s", esp_err_to_name(err));
    }

    /* O primeiro advertising so comeca quando este evento chegar (ver gap_event_handler). */
    ESP_RETURN_ON_ERROR(esp_ble_gap_config_local_privacy(true), TAG, "privacidade local");
    ESP_LOGI(TAG, "BLE inicializado (\"%s\")", s_name);
    return ESP_OK;
}

void bt_ble_set_adv_provider(bt_ble_adv_provider_t provider)
{
    s_provider = provider;
}

esp_err_t bt_ble_set_profile(bt_ble_profile_t profile)
{
    ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_INVALID_STATE, TAG, "bt_ble_init() nao chamado");
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (profile != s_want) {
        ESP_LOGI(TAG, "Perfil BLE: %s -> %s", profile_name(s_want), profile_name(profile));
    }
    s_want = profile;
    apply_locked();
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

bt_ble_profile_t bt_ble_get_profile(void)
{
    return s_want;
}

void bt_ble_set_streaming(bool streaming)
{
    if (!s_lock || streaming == s_streaming) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_streaming = streaming;
    if (s_want == BT_BLE_ADV_ACTIVE) {
        apply_locked();
    }
    xSemaphoreGive(s_lock);
}

void bt_ble_refresh_adv(void)
{
    if (!s_lock) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    apply_locked();
    xSemaphoreGive(s_lock);
}

#include "bt_core.h"

#include "esp_bt.h"
#include "esp_bt_device.h"
#include "esp_bt_main.h"
#include "esp_log.h"

#include "bt_a2dp.h"
#include "bt_app_core.h"
#include "bt_avrcp.h"
#include "bt_ble.h"
#include "bt_eq_service.h"
#include "bt_fastpair.h"
#include "bt_gap.h"
#include "bt_hfp.h"
#include "bt_link_mgr.h"

static const char *TAG = "bt_core";

enum { BT_EVT_STACK_UP = 0 };

static void log_step(const char *name, esp_err_t err)
{
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao iniciar %s: %s", name, esp_err_to_name(err));
    }
}

/* Roda na BtAppTask quando a pilha esta pronta. Ordem importa:
 * perfis -> GAP -> servicos GATT -> BLE -> link manager (que ja pode conectar). */
static void stack_up_hdl(uint16_t event, void *p)
{
    (void)p;
    if (event != BT_EVT_STACK_UP) {
        return;
    }

    log_step("AVRCP", bt_avrcp_start());
    log_step("A2DP", bt_a2dp_start());
    log_step("HFP", bt_hfp_start());
    log_step("GAP", bt_gap_start(CONFIG_HEADSET_DEVICE_NAME));

    log_step("BLE", bt_ble_init(CONFIG_HEADSET_DEVICE_NAME));
    log_step("Fast Pair", bt_fastpair_init());   /* registra o servico 0xFE2C e o provider no bt_ble */

    log_step("EQ GATT", bt_eq_service_init());

    log_step("Link manager", bt_link_mgr_start());
}

esp_err_t bt_core_init(void)
{
    esp_err_t err;

    /* BTDM: Classic (A2DP/AVRCP/HFP) + BLE (Fast Pair, SLEEP). Nao liberamos a memoria do BLE. */
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    err = esp_bt_controller_init(&bt_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_bt_controller_init: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_bt_controller_enable(ESP_BT_MODE_BTDM);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_bt_controller_enable(BTDM): %s", esp_err_to_name(err));
        return err;
    }

    esp_bluedroid_config_t bd_cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    err = esp_bluedroid_init_with_cfg(&bd_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_bluedroid_init: %s", esp_err_to_name(err));
        return err;
    }
    err = esp_bluedroid_enable();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_bluedroid_enable: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "Pilha BTDM (Classic + BLE) ativa");

    bt_app_task_start_up();
    if (!bt_app_work_dispatch(stack_up_hdl, BT_EVT_STACK_UP, NULL, 0, NULL, NULL)) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

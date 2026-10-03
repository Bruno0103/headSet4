/*
 * Orquestrador Bluetooth. Cada parte vive no seu proprio modulo:
 *   bt/bt_gap.c    pareamento, reconexao, conhecidos, visibilidade
 *   bt/bt_a2dp.c   musica (SBC)            -> audio_io
 *   bt/bt_avrcp.c  volume absoluto         -> audio_codec
 *   bt/bt_hfp.c    chamadas + microfone    -> audio_io (+ limpeza de ruido)
 *
 * O audio (WM8960 + I2S) e iniciado por audio_init() em main.c, antes daqui.
 */
#include "bluetooth.h"

#include "esp_check.h"
#include "esp_bt_device.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bt_app_core_utils.h"
#include "bredr_app_common_utils.h"

#include "bt_a2dp.h"
#include "bt_avrcp.h"
#include "bt_gap.h"
#include "bt_hfp.h"

#ifdef CONFIG_EXAMPLE_LOCAL_DEVICE_NAME
#define DEVICE_NAME CONFIG_EXAMPLE_LOCAL_DEVICE_NAME
#else
#define DEVICE_NAME "SuperHeadphones"
#endif

static const char *TAG = "bluetooth";

enum { BT_EVT_STACK_UP = 0 };

static void dev_cb(esp_bt_dev_cb_event_t event, esp_bt_dev_cb_param_t *param)
{
    bredr_app_dev_evt_def_hdl(event, param);
}

/* Fecha os dois perfis; o enlace cai quando o ultimo fechar. */
static esp_err_t link_disconnect(esp_bd_addr_t bda)
{
    bt_hfp_disconnect(bda);
    return bt_a2dp_disconnect(bda);
}

static void on_gap_state(bt_gap_state_t state)
{
    ESP_LOGI(TAG, "GAP: %s", state == BT_GAP_CONNECTED ? "conectado (oculto)" : "visivel");
    /* ponto de encaixe para LED/LCD */
}

/* Roda na task BT_APP quando a pilha esta pronta */
static void stack_up_hdl(uint16_t event, void *p)
{
    (void)p;
    if (event != BT_EVT_STACK_UP) return;

    esp_bt_dev_register_callback(dev_cb);

    ESP_ERROR_CHECK(bt_avrcp_start());      /* passo 1: controle de volume */
    ESP_ERROR_CHECK(bt_a2dp_start());       /* passo 2: streaming de musica */
    ESP_ERROR_CHECK(bt_hfp_start());        /* passos 4-5: chamadas (no-op se HFP desligado) */

    /* GAP por ultimo: ele ja pode reconectar, entao os perfis precisam estar prontos */
    const bt_gap_config_t gap = {
        .device_name = DEVICE_NAME,
        .connect     = bt_a2dp_connect,
        .disconnect  = link_disconnect,
        .state_cb    = on_gap_state,
        .confirm_cb  = NULL,                /* NULL = aceita pareamento automaticamente */
    };
    ESP_ERROR_CHECK(bt_gap_start(&gap));
}

void bluetooth_init(void)
{
    ESP_ERROR_CHECK(bredr_app_common_init());
    bt_app_task_start_up();
}

void bluetooth_task(void *arg)
{
    bt_app_work_dispatch(stack_up_hdl, BT_EVT_STACK_UP, NULL, 0, NULL, NULL);
    vTaskDelete(NULL);
}

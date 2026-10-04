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

#include "bt_app_core.h"

#include "bt_a2dp.h"
#include "bt_avrcp.h"
#include "bt_gap.h"
#include "bt_hfp.h"
#include "bt_ble.h"

#include "esp_bt.h"
#include "esp_bt_main.h"

#ifdef CONFIG_EXAMPLE_LOCAL_DEVICE_NAME
#define DEVICE_NAME CONFIG_EXAMPLE_LOCAL_DEVICE_NAME
#else
#define DEVICE_NAME "SuperHeadphones"
#endif

static const char *TAG = "bluetooth";

enum { BT_EVT_STACK_UP = 0 };

static void dev_cb(esp_bt_dev_cb_event_t event, esp_bt_dev_cb_param_t *param)
{
    ESP_LOGD(TAG, "Evento de dispositivo Bluetooth recebido: %d", event);
}

static esp_err_t link_connect(esp_bd_addr_t bda)
{
    bt_hfp_connect(bda);
    return bt_a2dp_connect(bda);
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

    esp_err_t err;

    /* passo 1: controle de volume */
    err = bt_avrcp_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao inicializar AVRCP: %s", esp_err_to_name(err));
    }

    /* passo 2: streaming de musica */
    err = bt_a2dp_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao inicializar A2DP: %s", esp_err_to_name(err));
    }

    /* passos 4-5: chamadas (no-op se HFP desligado) */
    err = bt_hfp_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao inicializar HFP: %s", esp_err_to_name(err));
    }
    
    /* passo extra: GATT Server e Advertising BLE */
    err = bt_ble_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Falha ao inicializar BLE: %s", esp_err_to_name(err));
    }

    /* GAP por ultimo: ele ja pode reconectar, entao os perfis precisam estar prontos */
    const bt_gap_config_t gap = {
        .device_name = DEVICE_NAME,
        .connect     = link_connect,
        .disconnect  = link_disconnect,
        .state_cb    = on_gap_state,
        .confirm_cb  = NULL,                /* NULL = aceita pareamento automaticamente */
    };
    err = bt_gap_start(&gap);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao iniciar GAP: %s", esp_err_to_name(err));
    }
}

esp_err_t bluetooth_init(void)
{
    esp_err_t err;

    /* Identidade Dual Mode: Não liberamos a memória do BLE. Manteremos Classic e BLE (BTDM) compartilhando a BD_ADDR. */
    // ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE)); // Removido para suportar BLE

    /* Inicializa o controlador Bluetooth com as configurações padrão */
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    err = esp_bt_controller_init(&bt_cfg);
    if (err) {
        ESP_LOGE(TAG, "Falha na inicialização do controlador Bluetooth: %s", esp_err_to_name(err));
        return err;
    }

    /* Habilita o controlador no modo Dual Mode (BR/EDR + BLE) */
    err = esp_bt_controller_enable(ESP_BT_MODE_BTDM);
    if (err) {
        ESP_LOGW(TAG, "Falha ao habilitar BTDM: %s. Tentando apenas CLASSIC_BT.", esp_err_to_name(err));
        err = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
        if (err) {
            ESP_LOGE(TAG, "Falha ao habilitar o controlador Bluetooth (CLASSIC): %s", esp_err_to_name(err));
            return err;
        }
    }

    /* Inicializa a pilha Bluedroid com as configurações padrão */
    esp_bluedroid_config_t bluedroid_cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    err = esp_bluedroid_init_with_cfg(&bluedroid_cfg);
    if (err) {
        ESP_LOGE(TAG, "Falha na inicialização do Bluedroid: %s", esp_err_to_name(err));
        return err;
    }

    /* Habilita a pilha Bluedroid */
    err = esp_bluedroid_enable();
    if (err) {
        ESP_LOGE(TAG, "Falha ao habilitar o Bluedroid: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Pilha Bluetooth inicializada com sucesso");

    /* Inicia a task principal da aplicação Bluetooth */
    bt_app_task_start_up();

    /* Despacha o evento de inicialização dos perfis */
    bt_app_work_dispatch(stack_up_hdl, BT_EVT_STACK_UP, NULL, 0, NULL, NULL);

    return ESP_OK;
}

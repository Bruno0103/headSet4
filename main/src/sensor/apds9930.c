#include "apds9930.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bt_a2dp.h"
#include "bt_avrcp.h"
#include "bt_gap.h"
#include "esp_gap_bt_api.h"
#include "bt_ble.h"

#define APDS9930_I2C_ADDR 0x39
#define APDS9930_REG_PDATA 0x9C
#define I2C_MASTER_NUM 0
#define PROXIMITY_THRESHOLD 100 // Valor de exemplo

static const char* TAG = "apds9930";
static bool s_headset_on_head = true;

// Mock de leitura I2C para simplificacao. 
// Substituir pela logica real I2C de sua placa.
static uint8_t read_proximity(void) {
    // esp_err_t ret;
    // uint8_t data;
    // i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    // i2c_master_start(cmd);
    // i2c_master_write_byte(cmd, (APDS9930_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    // i2c_master_write_byte(cmd, APDS9930_REG_PDATA, true);
    // i2c_master_start(cmd);
    // i2c_master_write_byte(cmd, (APDS9930_I2C_ADDR << 1) | I2C_MASTER_READ, true);
    // i2c_master_read_byte(cmd, &data, I2C_MASTER_NACK);
    // i2c_master_stop(cmd);
    // ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(100));
    // i2c_cmd_link_delete(cmd);
    // return (ret == ESP_OK) ? data : 0;
    
    // Fake return
    return 255;
}

void apds9930_task(void *pvParameters) {
    ESP_LOGI(TAG, "Task do sensor de proximidade APDS-9930 iniciada");
    while(1) {
        uint8_t prox_data = read_proximity();
        bool current_status = (prox_data > PROXIMITY_THRESHOLD);

        if (current_status != s_headset_on_head) {
            s_headset_on_head = current_status;
            
            if (!s_headset_on_head) {
                ESP_LOGW(TAG, "GATILHO: Fone RETIRADO da cabeca");
                
                // Hardware Master Control: Pausa a musica se estiver tocando
                // Isso envia um AVRCP Pause para o celular/PC
                if (bt_a2dp_is_streaming()) {
                    bt_avrcp_send_pause(); 
                }
                
                // Transicao para Deep Sleep BLE: 
                // Desliga a visibilidade Classic
                esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
                
                // Derruba qualquer link ativo para poupar energia (sem apagar os pareamentos salvos)
                bt_gap_disconnect_active(); 

                // Inicia um BLE Advertising beeem lento (ex: 1.28s) para poupar energia
                bt_ble_set_adv_slow();
                
            } else {
                ESP_LOGI(TAG, "GATILHO: Fone COLOCADO na cabeca");
                
                // Wake Up: Ativa o Page Scan Classic para o PC reconectar
                esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
                
                // Acelera BLE advertising para reconexao instantanea com o App (30ms)
                bt_ble_set_adv_fast();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(500)); // Polling a cada 500ms
    }
}

esp_err_t apds9930_init(void) {
    // Inicializacao do I2C master omitida (assumindo que seja feita em main.c)
    
    return ESP_OK;
}

bool apds9930_is_headset_on(void) {
    return s_headset_on_head;
}

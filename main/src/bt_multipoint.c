/**
 * @file bt_multipoint.c
 * @brief Implementação da estratégia Multipoint Inteligente
 */

#include "bt_multipoint.h"
#include "bt_a2dp.h"
#include "bt_gap.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_gap_bt_api.h" // Adicionado para corrigir os erros de 'esp_bt_gap_set_scan_mode'
#include "string.h"

static const char *TAG = "bt_multipoint";

// Variáveis de estado
static bt_multipoint_state_t current_state = BT_MP_STATE_IDLE;
static esp_bd_addr_t active_bda = {0};
static bool headset_is_worn = false;

// Timeout timer (Módulo 3 - Estado 4)
static esp_timer_handle_t inactivity_timer;
#define INACTIVITY_TIMEOUT_MS (5 * 60 * 1000) // 5 minutos

// Função callback do timer
static void inactivity_timer_cb(void* arg) {
    bt_multipoint_inactivity_timeout();
}

void bt_multipoint_init(void) {
    ESP_LOGI(TAG, "Inicializando Módulo Multipoint TWS-like");
    
    // Módulo 1: Fundação de Identidade já está sendo tratado na inicialização 
    // do Bluetooth (BTDM = Classic + BLE) e NVS (Bonding cacheado).
    // O Pareamento salva as chaves automaticamente na NVS.

    // Criando timer de inatividade
    const esp_timer_create_args_t timer_args = {
        .callback = &inactivity_timer_cb,
        .name = "inactivity_timeout"
    };
    esp_timer_create(&timer_args, &inactivity_timer);

    current_state = BT_MP_STATE_IDLE;
    ESP_LOGI(TAG, "Multipoint iniciado. Estado Atual: IDLE");
}

void bt_multipoint_headset_worn(void) {
    if (headset_is_worn) return;
    headset_is_worn = true;

    ESP_LOGI(TAG, "[Módulo 2] Sensor de Presença: Fone colocado na cabeça");
    ESP_LOGI(TAG, "Ação: Acelerando Advertising BLE e habilitando Page Scan Classic");

    // TODO: Chamar API do BLE para acelerar advertising
    // bt_ble_set_adv_fast();

    // Habilita rádio Classic para aceitar conexões novamente
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
}

void bt_multipoint_headset_removed(void) {
    if (!headset_is_worn) return;
    headset_is_worn = false;

    ESP_LOGI(TAG, "[Módulo 2] Sensor de Presença: Fone retirado da cabeça");
    ESP_LOGI(TAG, "Ação: Pausando áudio, desconectando Classic e indo para Deep Sleep BLE");

    // Para o timer
    esp_timer_stop(inactivity_timer);

    if (current_state == BT_MP_STATE_A2DP_ACTIVE) {
        // Envia Pause e desconecta A2DP
        bt_a2dp_disconnect(active_bda);
    }

    // Desabilita discoverability/connectability do Classic
    esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);

    // TODO: Chamar API do BLE para advertising lento (low power)
    // bt_ble_set_adv_slow();

    current_state = BT_MP_STATE_IDLE;
}

void bt_multipoint_audio_trigger(esp_bd_addr_t new_bda) {
    if (!headset_is_worn) {
        ESP_LOGW(TAG, "Tentativa de áudio rejeitada: Fone não está na cabeça");
        return;
    }

    ESP_LOGI(TAG, "[Módulo 4] Handover Multipoint: Requisição de áudio recebida");
    ESP_LOGI(TAG, "MAC do solicitante: %02X:%02X:%02X:%02X:%02X:%02X",
             new_bda[0], new_bda[1], new_bda[2], new_bda[3], new_bda[4], new_bda[5]);

    // Situação A: Repouso
    if (current_state == BT_MP_STATE_IDLE) {
        ESP_LOGI(TAG, "Situação A: Aceitando conexão A2DP diretamente");
        current_state = BT_MP_STATE_CONNECTING;
        // Dependendo da implementação, aceitamos a conexão ou tentamos iniciar
        // bt_a2dp_connect(new_bda);
    } 
    // Situação B: Troca de Áudio
    else if (current_state == BT_MP_STATE_A2DP_ACTIVE) {
        // Se for o MESMO dispositivo tocando, apenas reinicia o timer
        if (memcmp(active_bda, new_bda, ESP_BD_ADDR_LEN) == 0) {
            ESP_LOGI(TAG, "Dispositivo já ativo. Reiniciando timeout de inatividade.");
            esp_timer_start_once(inactivity_timer, INACTIVITY_TIMEOUT_MS * 1000);
            return;
        }

        ESP_LOGI(TAG, "Situação B: Handover em progresso. Outro dispositivo solicitou áudio.");
        
        // Passo 1: Envia comando de Pause para o ativo atual e derruba a conexão
        ESP_LOGI(TAG, "Passo 1: Desconectando dispositivo atual...");
        bt_a2dp_disconnect(active_bda);
        
        // Passo 2: O sistema aguardará a desconexão e permitirá a nova conexão.
        // O próximo MAC a conectar será 'new_bda'.
        // Como otimização, poderíamos já iniciar a conexão pro 'new_bda' aqui.
        current_state = BT_MP_STATE_CONNECTING;
        memcpy(active_bda, new_bda, ESP_BD_ADDR_LEN);
        
        ESP_LOGI(TAG, "Passo 2 & 3: Portões abertos para novo dispositivo (A2DP).");
        bt_a2dp_connect(new_bda);
    }
}

void bt_multipoint_a2dp_connected(esp_bd_addr_t bda) {
    ESP_LOGI(TAG, "[Módulo 3] Estado 3: A2DP Ativo (Streaming)");
    current_state = BT_MP_STATE_A2DP_ACTIVE;
    memcpy(active_bda, bda, ESP_BD_ADDR_LEN);

    // Inicia/Reinicia timer de inatividade
    esp_timer_start_once(inactivity_timer, INACTIVITY_TIMEOUT_MS * 1000);
}

void bt_multipoint_a2dp_disconnected(esp_bd_addr_t bda) {
    ESP_LOGI(TAG, "[Módulo 3] Conexão A2DP encerrada.");
    
    // Se a desconexão veio do dispositivo ativo
    if (memcmp(active_bda, bda, ESP_BD_ADDR_LEN) == 0) {
        current_state = BT_MP_STATE_IDLE;
        memset(active_bda, 0, ESP_BD_ADDR_LEN);
        esp_timer_stop(inactivity_timer);
        ESP_LOGI(TAG, "Retornando para Estado 1 (Repouso)");
    }
}

void bt_multipoint_inactivity_timeout(void) {
    ESP_LOGI(TAG, "[Módulo 3] Estado 4: Timeout de Inatividade.");
    ESP_LOGI(TAG, "Música pausada por muito tempo. Derrubando link Classic voluntariamente...");

    if (current_state == BT_MP_STATE_A2DP_ACTIVE) {
        bt_a2dp_disconnect(active_bda);
        current_state = BT_MP_STATE_IDLE;
        memset(active_bda, 0, ESP_BD_ADDR_LEN);
    }
}

/**
 * @file bt_multipoint.h
 * @brief Gerenciador de Estados Bluetooth Multipoint Inteligente
 * 
 * Implementa a estratégia TWS-like para Multipoint, compartilhando o rádio
 * e priorizando conexões com base no uso.
 */
#pragma once

#include <stdbool.h>
#include "esp_bt_defs.h"

/**
 * @brief Estados do Fone de Ouvido (Módulo 3)
 */
typedef enum {
    BT_MP_STATE_IDLE,        ///< Estado 1: Repouso / Ocioso (Apenas BLE ou s/ conexão ativa)
    BT_MP_STATE_CONNECTING,  ///< Estado 2: Gatilho de Áudio (Handover)
    BT_MP_STATE_A2DP_ACTIVE, ///< Estado 3: A2DP Ativo (Streaming)
} bt_multipoint_state_t;

/**
 * @brief Inicializa o gerenciador Multipoint.
 */
void bt_multipoint_init(void);

/**
 * @brief Módulo 2: Gatilho - Fone colocado na cabeça.
 * Acelera o Advertising BLE e liga o Page Scan Classic.
 */
void bt_multipoint_headset_worn(void);

/**
 * @brief Módulo 2: Gatilho - Fone retirado da cabeça.
 * Encerra fluxos A2DP e coloca o Classic em repouso. Mantém BLE lento.
 */
void bt_multipoint_headset_removed(void);

/**
 * @brief Módulo 4: Handover Multipoint (A Troca Inteligente).
 * Gatilho de Áudio solicitado por um dispositivo (Celular ou PC).
 * 
 * @param bda Endereço MAC do dispositivo solicitando áudio.
 */
void bt_multipoint_audio_trigger(esp_bd_addr_t bda);

/**
 * @brief Notifica o gerenciador que o A2DP conectou.
 */
void bt_multipoint_a2dp_connected(esp_bd_addr_t bda);

/**
 * @brief Notifica o gerenciador que o A2DP desconectou.
 */
void bt_multipoint_a2dp_disconnected(esp_bd_addr_t bda);

/**
 * @brief Timer de inatividade (Timeout).
 * Chamado periodicamente ou por um timer RTOS quando o áudio pausa.
 */
void bt_multipoint_inactivity_timeout(void);

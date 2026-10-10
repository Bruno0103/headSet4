/**
 * @file hs_cmds.h
 * @brief Definições e contratos dos comandos direcionados aos Actors (FreeRTOS Queues).
 *
 * Arquitetura Event-Driven & Actor Model (Agente A0 - Arquiteto / Agente A1 - Infraestrutura):
 * - Representa ordens imperativas (comandos no presente/futuro).
 * - Comunicação 1-para-1 direta para a fila do único dono do recurso.
 * - Cada ator possui um subconjunto numérico de IDs de comando.
 * - Suporta execução unidirecional assíncrona (hs_actor_send) ou request/reply (hs_actor_request).
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_bt_defs.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * INTERVALOS NUMÉRICOS DE COMANDOS POR ACTOR
 * ============================================================================ */

#define HS_CMD_AUDIO_BASE      0x0100
#define HS_CMD_BT_BASE         0x0200
#define HS_CMD_SETTINGS_BASE   0x0300
#define HS_CMD_UI_BASE         0x0400
#define HS_CMD_SENSORS_BASE    0x0500
#define HS_CMD_ACTUATORS_BASE  0x0600
#define HS_CMD_FASTPAIR_BASE   0x0700
#define HS_CMD_PHONE_BASE      0x0800

/* ============================================================================
 * 1. COMANDOS DO ACTOR AUDIO (Dono do I2S DMA, WM8960 Codec, EQ, Tons, Voice NR)
 * Core 1, Prioridade 10
 * ============================================================================ */

typedef enum {
    /** Inicia reprodução de streaming de música A2DP (payload: audio_cmd_start_music_t) */
    AUDIO_CMD_START_MUSIC = HS_CMD_AUDIO_BASE + 1,

    /** Inicia modo de chamada bidirecional HFP com Voice NR (payload: audio_cmd_start_call_t) */
    AUDIO_CMD_START_CALL,

    /** Para o modo ativo (payload: audio_cmd_stop_t) */
    AUDIO_CMD_STOP,

    /** Dispara a reprodução de um tom ou efeito sonoro SFX (payload: audio_cmd_play_tone_t) */
    AUDIO_CMD_PLAY_TONE,

    /** Ajusta o volume geral do hardware codec (payload: audio_cmd_set_volume_t) */
    AUDIO_CMD_SET_VOLUME,

    /** Aplica preset ou bandas do equalizador (payload: audio_cmd_set_eq_t) */
    AUDIO_CMD_SET_EQ,

    /** Muta ou desmuta a saída de áudio (payload: audio_cmd_set_mute_t) */
    AUDIO_CMD_SET_MUTE,
} audio_cmd_id_t;

typedef struct {
    uint32_t sample_rate;        /**< Taxa de amostragem em Hz (ex: 44100). */
    uint8_t  channels;           /**< Número de canais (normalmente 2 para estéreo). */
    uint8_t  bits_per_sample;    /**< Resolução em bits (16 ou 24). */
} audio_cmd_start_music_t;

typedef struct {
    uint32_t sample_rate;        /**< Taxa de amostragem em Hz (ex: 8000 para NB, 16000 para WB). */
    bool     enable_nr;          /**< Habilita algoritmo de redução de ruído no microfone. */
} audio_cmd_start_call_t;

typedef struct {
    uint8_t  mode_mask;          /**< Máscara de modos a parar (0xFF para forçar retorno a IDLE). */
} audio_cmd_stop_t;

typedef struct {
    uint16_t tone_id;            /**< Identificador da tabela de tons SFX. */
    bool     interrupt_current;  /**< Se deve interromper tom atualmente em execução. */
} audio_cmd_play_tone_t;

typedef struct {
    uint8_t  volume_percent;     /**< Volume de 0 a 100%. */
} audio_cmd_set_volume_t;

typedef struct {
    uint8_t  preset_id;          /**< ID do preset (Flat, Bass, Vocal, etc.) ou 0xFF para bandas custom. */
    int8_t   gains_db[5];        /**< Ganhos para 5 bandas em dB (-12 a +12). */
} audio_cmd_set_eq_t;

typedef struct {
    bool     mute;               /**< true para mutar, false para desmutar. */
} audio_cmd_set_mute_t;

/* ============================================================================
 * 2. COMANDOS DO ACTOR BT_LINK (Dono da política de slots, conexões, pareamento)
 * Core 0, Prioridade 6
 * ============================================================================ */

typedef enum {
    /** Seleciona qual slot é o primário/ativo (payload: bt_cmd_select_slot_t) */
    BT_CMD_SELECT_SLOT = HS_CMD_BT_BASE + 1,

    /** Conecta a um slot pré-vinculado ou endereço (payload: bt_cmd_connect_t) */
    BT_CMD_CONNECT,

    /** Desconecta o link do slot selecionado (payload: bt_cmd_disconnect_t) */
    BT_CMD_DISCONNECT,

    /** Inicia o modo de pareamento abrindo visibilidade e descoberta (payload: bt_cmd_start_pairing_t) */
    BT_CMD_START_PAIRING,

    /** Cancela imediatamente o modo de pareamento (sem payload) */
    BT_CMD_CANCEL_PAIRING,

    /** Configura o recurso de troca automática entre slots ativos (payload: bt_cmd_set_auto_switch_t) */
    BT_CMD_SET_AUTO_SWITCH,
} bt_cmd_id_t;

typedef struct {
    uint8_t slot;                /**< Slot a selecionar (0 ou 1). */
} bt_cmd_select_slot_t;

typedef struct {
    uint8_t       slot;          /**< Slot de destino. */
    esp_bd_addr_t bda;           /**< Endereço opcional (se todos zeros, conecta ao memorizado). */
} bt_cmd_connect_t;

typedef struct {
    uint8_t slot;                /**< Slot a desconectar. */
} bt_cmd_disconnect_t;

typedef struct {
    uint16_t duration_sec;       /**< Duração da janela em segundos (ex: 60 ou 120). */
} bt_cmd_start_pairing_t;

typedef struct {
    bool enabled;                /**< true para ligar auto-switch de áudio, false para desligar. */
} bt_cmd_set_auto_switch_t;

/* ============================================================================
 * 3. COMANDOS DO ACTOR SETTINGS (Dono único e exclusivo do NVS)
 * Core 0, Prioridade 3
 * ============================================================================ */

typedef enum {
    /** Grava um valor inteiro u8/u16/u32 ou booleano (payload: settings_cmd_set_val_t) */
    SETTINGS_CMD_SET = HS_CMD_SETTINGS_BASE + 1,

    /** Solicita leitura síncrona de chave via request/reply (payload: settings_cmd_get_val_t) */
    SETTINGS_CMD_GET,

    /** Força o flush de gravações pendentes em buffer de debounce para o NVS flash */
    SETTINGS_CMD_COMMIT,

    /** Executa o reset de todas as configurações de volta para o padrão de fábrica */
    SETTINGS_CMD_FACTORY_RESET,
} settings_cmd_id_t;

typedef enum {
    SETTINGS_TYPE_U8,
    SETTINGS_TYPE_U16,
    SETTINGS_TYPE_U32,
    SETTINGS_TYPE_STR,
    SETTINGS_TYPE_BLOB,
} settings_val_type_t;

typedef struct {
    char                key[16];     /**< Chave no NVS (max 15 chars + null). */
    settings_val_type_t type;        /**< Tipo de dado. */
    union {
        uint32_t        u32;
        uint16_t        u16;
        uint8_t         u8;
        uint8_t         bytes[32];   /**< Para blobs curtos ou strings. */
    } val;
} settings_cmd_set_val_t;

typedef struct {
    char                key[16];     /**< Chave no NVS requerida. */
    settings_val_type_t type;        /**< Tipo esperado. */
} settings_cmd_get_val_t;

/* ============================================================================
 * 4. COMANDOS DO ACTOR UI (Dono da task LVGL, subjects e atualizações de tela)
 * Core livre, Prioridade 3
 * ============================================================================ */

typedef enum {
    /** Notifica atualização no modelo de dados para renderização pelo LVGL */
    UI_CMD_UPDATE_MODEL = HS_CMD_UI_BASE + 1,

    /** Define o nível de brilho da tela (0 a 100%) */
    UI_CMD_SET_BRIGHTNESS,

    /** Força despertar ou suspensão do display */
    UI_CMD_SET_DISPLAY_STATE,
} ui_cmd_id_t;

typedef struct {
    uint8_t brightness_pct;      /**< Percentual de brilho (0 a 100). */
} ui_cmd_set_brightness_t;

typedef struct {
    bool    turn_on;             /**< true = liga e reseta timeout de sono, false = suspende. */
} ui_cmd_set_display_state_t;

/* ============================================================================
 * 5. COMANDOS DO ACTOR SENSORS (Dono do APDS-9930, Botão e ADC de Bateria)
 * Core 0, Prioridade 4
 * ============================================================================ */

typedef enum {
    /** Habilita ou desabilita o sensor de proximidade APDS (payload: sensor_cmd_enable_t) */
    SENSOR_CMD_ENABLE_PROX = HS_CMD_SENSORS_BASE + 1,

    /** Configura o período de amostragem/duty-cycle do sensor (payload: sensor_cmd_set_rate_t) */
    SENSOR_CMD_SET_POLL_RATE,

    /** Dispara leitura imediata da bateria fora do ciclo padrão */
    SENSOR_CMD_READ_BATTERY_NOW,
} sensor_cmd_id_t;

typedef struct {
    bool enable;                 /**< true = ativo, false = modo de economia profunda/standby. */
} sensor_cmd_enable_t;

typedef struct {
    uint16_t poll_period_ms;     /**< Período entre amostras (ms). */
} sensor_cmd_set_rate_t;

/* ============================================================================
 * 6. COMANDOS DO ACTOR ACTUATORS (Dono do LEDC: Vibracall, Servos de Orelha, PWM)
 * Sob demanda (Core 0, Prioridade 3)
 * ============================================================================ */

typedef enum {
    /** Aciona padrão vibratório no motor haptics (payload: actuator_cmd_vibrate_t) */
    ACTUATOR_CMD_VIBRATE = HS_CMD_ACTUATORS_BASE + 1,

    /** Posiciona servomotores das orelhas robóticas (payload: actuator_cmd_ears_t) */
    ACTUATOR_CMD_SET_EARS,

    /** Ajusta ciclo de trabalho de backlight (payload: actuator_cmd_backlight_t) */
    ACTUATOR_CMD_SET_BACKLIGHT,
} actuator_cmd_id_t;

typedef struct {
    uint16_t duration_ms;        /**< Duração da vibração em milissegundos. */
    uint8_t  intensity_percent;  /**< Intensidade PWM do motor de 0 a 100%. */
} actuator_cmd_vibrate_t;

typedef struct {
    int16_t  angle_left_deg;     /**< Ângulo da orelha esquerda (-90 a +90). */
    int16_t  angle_right_deg;    /**< Ângulo da orelha direita (-90 a +90). */
} actuator_cmd_ears_t;

typedef struct {
    uint8_t  duty_percent;       /**< 0 a 100%. */
} actuator_cmd_backlight_t;

/* ============================================================================
 * 7. COMANDOS DO ACTOR FASTPAIR_CRYPTO (Dono dos cálculos pesados ECDH/AES)
 * Sob demanda (Core 0, Prioridade 3)
 * ============================================================================ */

typedef enum {
    /** Solicita cálculo de segredo compartilhado ECDH (payload: fp_crypto_ecdh_req_t) */
    FP_CRYPTO_CMD_COMPUTE_SECRET = HS_CMD_FASTPAIR_BASE + 1,

    /** Solicita descriptografia de bloco de dados com AES-128 */
    FP_CRYPTO_CMD_DECRYPT,
} fp_crypto_cmd_id_t;

/* ============================================================================
 * 8. COMANDOS DO ACTOR PHONE_CTL (Tratamento de Comandos BLE do Smartphone)
 * Sob demanda (Core 0, Prioridade 5)
 * ============================================================================ */

typedef enum {
    /** Solicita processamento de mensagem/pacote recebido do app (payload: phone_cmd_msg_t) */
    PHONE_CMD_PROCESS_MSG = HS_CMD_PHONE_BASE + 1,

    /** Notifica desconexão ou encerramento de link BLE para o ator */
    PHONE_CMD_CLIENT_DISCONNECTED,
} phone_cmd_id_t;

typedef struct {
    uint16_t conn_id;            /**< Identificador da conexão BLE ativa. */

    uint16_t len;                /**< Tamanho dos dados contidos em raw. */
    uint8_t  raw[40];            /**< Payload fragmentado ou comando direto. */
} phone_cmd_msg_t;

#ifdef __cplusplus
}
#endif


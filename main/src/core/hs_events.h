/**
 * @file hs_events.h
 * @brief Definições e contratos das bases de eventos do Headset (esp_event).
 *
 * Arquitetura Event-Driven (Agente A0 - Arquiteto / Agente A1 - Infraestrutura):
 * - Representa fatos ocorridos no sistema (passado).
 * - Notificação 1-para-N (Pub/Sub) usando o loop de eventos centralizado do sistema.
 * - Handlers NUNCA bloqueiam (sem I2C, sem NVS, sem delays, sem vTaskDelay).
 * - Payloads são copiados pelo esp_event e DEVEM ser sempre <= 64 bytes.
 * - Dados contínuos de áudio (PCM) NUNCA passam por este barramento de eventos.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_bt_defs.h"
#include "esp_err.h"
#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * DECLARAÇÃO DAS BASES DE EVENTOS (ESP_EVENT_DECLARE_BASE)
 * ============================================================================ */

/**
 * @brief Base de eventos para sensores físicos (proximidade, botões, bateria).
 */
ESP_EVENT_DECLARE_BASE(SENSOR_EVT);

/**
 * @brief Base de eventos para Bluetooth e Link Manager (status de conexão, slots, pareamento).
 */
ESP_EVENT_DECLARE_BASE(BT_EVT);

/**
 * @brief Base de eventos para subsistema de áudio (mudança de modo, volume, equalização, SFX).
 */
ESP_EVENT_DECLARE_BASE(AUDIO_EVT);

/**
 * @brief Base de eventos para configurações persistentes e NVS (notificação de alteração de chaves).
 */
ESP_EVENT_DECLARE_BASE(CFG_EVT);

/* ============================================================================
 * 1. SENSOR_EVT — IDs e Payloads
 * ============================================================================ */

typedef enum {
    SENSOR_EVT_WORN = 0,         /**< Sensor APDS: Headset colocado na cabeça (sem payload). */
    SENSOR_EVT_REMOVED,          /**< Sensor APDS: Headset retirado da cabeça (sem payload). */
    SENSOR_EVT_BUTTON_SHORT,     /**< Botão: Clique curto detectado (payload: sensor_button_evt_t). */
    SENSOR_EVT_BUTTON_LONG,      /**< Botão: Pressão longa detectada (payload: sensor_button_evt_t). */
    SENSOR_EVT_BUTTON_DOUBLE,    /**< Botão: Duplo clique detectado (payload: sensor_button_evt_t). */
    SENSOR_EVT_BATTERY,          /**< Bateria: Nível ou tensão atualizados (payload: sensor_battery_evt_t). */
} sensor_event_id_t;

/**
 * @brief Payload para eventos de botão (curto, longo, duplo).
 */
typedef struct {
    uint8_t button_id;           /**< Identificador do botão físico acionado (ex: 0 = principal). */
    uint16_t duration_ms;        /**< Duração em milissegundos da pressão. */
} sensor_button_evt_t;

/**
 * @brief Payload para telemetria de bateria.
 */
typedef struct {
    uint8_t  percent;            /**< Porcentagem de carga restante (0 a 100%). */
    uint16_t millivolts;         /**< Tensão medida pelo ADC em milivolts. */
    bool     is_charging;        /**< Indicador se a bateria está atualmente em carga. */
} sensor_battery_evt_t;

/* ============================================================================
 * 2. BT_EVT — IDs e Payloads
 * ============================================================================ */

typedef enum {
    BT_EVT_LINK_UP = 0,          /**< Perfil conectado com sucesso (payload: bt_link_evt_t). */
    BT_EVT_LINK_DOWN,            /**< Perfil desconectado (payload: bt_link_evt_t). */
    BT_EVT_PAIRING_MODE,         /**< Janela de pareamento aberta ou fechada (payload: bt_pairing_evt_t). */
    BT_EVT_STREAMING,            /**< A2DP iniciou ou pausou stream de áudio (payload: bt_streaming_evt_t). */
    BT_EVT_CALL_STATE,           /**< HFP: Estado da chamada telefônica alterado (payload: bt_call_state_evt_t). */
    BT_EVT_SLOT_CHANGED,         /**< Slot ativo ou mapeamento de dispositivo alterado (payload: bt_slot_evt_t). */
    BT_EVT_PEER_NAME,            /**< Nome amigável do peer descoberto/autenticado (payload: bt_peer_name_evt_t). */
} bt_event_id_t;

/**
 * @brief Perfis suportados para link_up / link_down.
 */
typedef enum {
    BT_PROFILE_NONE = 0,
    BT_PROFILE_A2DP = (1 << 0),  /**< Perfil de áudio estéreo de alta qualidade A2DP. */
    BT_PROFILE_HFP  = (1 << 1),  /**< Perfil de headset / chamadas bidirecionais HFP. */
    BT_PROFILE_AVRCP= (1 << 2),  /**< Perfil de controle de mídia AVRCP. */
} bt_profile_mask_t;

/**
 * @brief Estados de chamada HFP reportados pelo stack.
 */
typedef enum {
    BT_CALL_IDLE = 0,            /**< Nenhuma chamada em andamento. */
    BT_CALL_INCOMING,            /**< Chamada recebida tocando. */
    BT_CALL_OUTGOING,            /**< Chamada discando / chamando. */
    BT_CALL_ACTIVE,              /**< Chamada em curso. */
    BT_CALL_HELD,                /**< Chamada em espera. */
} bt_call_state_t;

/**
 * @brief Conexão ou desconexão de link Bluetooth.
 */
typedef struct {
    uint8_t           slot;          /**< Índice do slot no link manager (0 ou 1). */
    esp_bd_addr_t     bda;           /**< Endereço Bluetooth do dispositivo remoto (6 bytes). */
    bt_profile_mask_t profile;       /**< Perfil envolvido no evento. */
    bool              is_voluntary;  /**< true se desconexão voluntária/normal, false se queda de sinal/timeout. */
} bt_link_evt_t;

/**
 * @brief Status do modo de pareamento.
 */
typedef struct {
    bool active;                 /**< true se a janela de pareamento está ativa, false se encerrada. */
    uint16_t timeout_sec;        /**< Timeout configurado para a janela (em segundos). */
} bt_pairing_evt_t;

/**
 * @brief Estado de streaming A2DP.
 */
typedef struct {
    uint8_t slot;                /**< Slot que está transmitindo áudio. */
    bool    streaming;           /**< true se pacote de mídia está sendo recebido/reproduzido. */
} bt_streaming_evt_t;

/**
 * @brief Notificação de mudança de chamada telefônica HFP.
 */
typedef struct {
    uint8_t         slot;        /**< Slot Bluetooth do dispositivo em chamada. */
    bt_call_state_t state;       /**< Estado atual da chamada. */
} bt_call_state_evt_t;

/**
 * @brief Notificação de seleção de slot ou alteração de vínculo.
 */
typedef struct {
    uint8_t       active_slot;   /**< Índice do slot atualmente selecionado como ativo. */
    esp_bd_addr_t bda;           /**< Endereço do dispositivo do slot. */
    bool          is_connected;  /**< Se o slot está atualmente conectado. */
} bt_slot_evt_t;

/**
 * @brief Nome legível obtido do dispositivo remoto (GAP / Auth).
 * Payload limitado para respeitar o teto de 64 bytes (32 bytes de nome + cabeçalho).
 */
typedef struct {
    uint8_t       slot;          /**< Slot associado ao dispositivo. */
    esp_bd_addr_t bda;           /**< Endereço Bluetooth do dispositivo. */
    char          name[36];      /**< Nome amigável truncado terminado em nulo. */
} bt_peer_name_evt_t;

/* ============================================================================
 * 3. AUDIO_EVT — IDs e Payloads
 * ============================================================================ */

typedef enum {
    AUDIO_EVT_MODE_CHANGED = 0,  /**< Modo de áudio alterado (IDLE, A2DP, HFP, SFX) (payload: audio_mode_evt_t). */
    AUDIO_EVT_VOLUME_CHANGED,    /**< Volume do codec ajustado (payload: audio_volume_evt_t). */
    AUDIO_EVT_EQ_CHANGED,        /**< Preset de equalização aplicado (payload: audio_eq_evt_t). */
    AUDIO_EVT_TONE_DONE,         /**< Reprodução de tom/SFX finalizada (payload: audio_tone_done_evt_t). */
} audio_event_id_t;

/**
 * @brief Modos operacionais do subsistema de áudio.
 */
typedef enum {
    AUDIO_MODE_IDLE = 0,         /**< Áudio desligado / sem fluxo ativo (codec em repouso). */
    AUDIO_MODE_MUSIC_A2DP,       /**< Reprodução de música em alta definição A2DP. */
    AUDIO_MODE_CALL_HFP,         /**< Chamada de voz bidirecional HFP com Voice NR. */
    AUDIO_MODE_TONE_SFX,         /**< Tom sonoro / SFX de sistema em reprodução. */
} audio_mode_t;

/**
 * @brief Notificação de mudança de modo do motor de áudio.
 */
typedef struct {
    audio_mode_t mode;           /**< Modo ativo atual. */
    uint32_t     sample_rate;    /**< Taxa de amostragem configurada no I2S (ex: 44100, 16000). */
} audio_mode_evt_t;

/**
 * @brief Notificação de alteração de volume.
 */
typedef struct {
    uint8_t volume_percent;      /**< Nível de volume em porcentagem (0 a 100%). */
    bool    muted;               /**< Indicador se o canal está mutado. */
} audio_volume_evt_t;

/**
 * @brief Notificação de alteração de equalização.
 */
typedef struct {
    uint8_t preset_index;        /**< Índice do preset ativo (Flat, Bass Boost, etc.). */
    int8_t  gains_db[5];         /**< Ganhos em dB por banda (-12dB a +12dB). */
} audio_eq_evt_t;

/**
 * @brief Notificação de encerramento de reprodução de tom/SFX.
 */
typedef struct {
    uint16_t tone_id;            /**< ID do tom ou som que terminou de tocar. */
    esp_err_t status;            /**< Resultado da reprodução (ESP_OK se completou). */
} audio_tone_done_evt_t;

/* ============================================================================
 * 4. CFG_EVT — IDs e Payloads
 * ============================================================================ */

typedef enum {
    CFG_EVT_SETTING_CHANGED = 0, /**< Uma configuração persistida no NVS foi alterada (payload: cfg_changed_evt_t). */
    CFG_EVT_FACTORY_RESET,       /**< Restauração de configurações de fábrica executada (sem payload). */
} cfg_event_id_t;

/**
 * @brief Notificação de alteração de parâmetro pelo actor settings.
 */
typedef struct {
    char    key[16];             /**< Chave da configuração que sofreu alteração (padrão NVS max 15 chars + null). */
    uint8_t type;                /**< Tipo do dado (ex: uint8, uint16, blob, etc.). */
    uint8_t value_u8;            /**< Valor utilitário rápido caso seja booleano/u8. */
} cfg_changed_evt_t;

/* ============================================================================
 * VALIDAÇÃO ESTÁTICA DE TAMANHO DE PAYLOAD (TODOS <= 64 BYTES)
 * ============================================================================ */

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(sizeof(sensor_button_evt_t) <= 64, "sensor_button_evt_t excede 64 bytes");
_Static_assert(sizeof(sensor_battery_evt_t) <= 64, "sensor_battery_evt_t excede 64 bytes");
_Static_assert(sizeof(bt_link_evt_t) <= 64, "bt_link_evt_t excede 64 bytes");
_Static_assert(sizeof(bt_pairing_evt_t) <= 64, "bt_pairing_evt_t excede 64 bytes");
_Static_assert(sizeof(bt_streaming_evt_t) <= 64, "bt_streaming_evt_t excede 64 bytes");
_Static_assert(sizeof(bt_call_state_evt_t) <= 64, "bt_call_state_evt_t excede 64 bytes");
_Static_assert(sizeof(bt_slot_evt_t) <= 64, "bt_slot_evt_t excede 64 bytes");
_Static_assert(sizeof(bt_peer_name_evt_t) <= 64, "bt_peer_name_evt_t excede 64 bytes");
_Static_assert(sizeof(audio_mode_evt_t) <= 64, "audio_mode_evt_t excede 64 bytes");
_Static_assert(sizeof(audio_volume_evt_t) <= 64, "audio_volume_evt_t excede 64 bytes");
_Static_assert(sizeof(audio_eq_evt_t) <= 64, "audio_eq_evt_t excede 64 bytes");
_Static_assert(sizeof(audio_tone_done_evt_t) <= 64, "audio_tone_done_evt_t excede 64 bytes");
_Static_assert(sizeof(cfg_changed_evt_t) <= 64, "cfg_changed_evt_t excede 64 bytes");
#endif

/* ============================================================================
 * INTERFACE DO BARRAMENTO CENTRAL DE EVENTOS (CTRL LOOP)
 * ============================================================================ */

/**
 * @brief Inicializa o laço central de eventos do Headset (Core 0, prioridade 5).
 * Idempotente.
 *
 * @return ESP_OK se criado ou já inicializado, código de erro caso contrário.
 */
esp_err_t hs_events_init(void);

/**
 * @brief Retorna o handle do loop central de eventos dedicado (ctrl loop).
 */
esp_event_loop_handle_t hs_events_get_loop(void);

/**
 * @brief Publica um evento no laço central com timeout de 0 (não bloqueante).
 * O esp_event faz a cópia interna dos dados.
 *
 * @param base Base do evento (SENSOR_EVT, BT_EVT, etc.).
 * @param id ID específico da base.
 * @param data Ponteiro para os dados do payload (pode ser NULL se tamanho for 0).
 * @param size Tamanho dos dados em bytes (deve ser <= 64).
 * @return ESP_OK em caso de sucesso, ESP_ERR_TIMEOUT se fila cheia, ou erro apropriado.
 */
esp_err_t hs_event_post(esp_event_base_t base, int32_t id, const void *data, size_t size);

/**
 * @brief Registra um handler de evento para uma base e ID específicos no loop central.
 *
 * @param base Base do evento.
 * @param id ID do evento ou ESP_EVENT_ANY_ID.
 * @param handler Função de callback do handler.
 * @param arg Argumento de contexto do usuário repassado ao handler.
 * @return ESP_OK em caso de sucesso.
 */
esp_err_t hs_event_register(esp_event_base_t base, int32_t id, esp_event_handler_t handler, void *arg);

/**
 * @brief Remove o registro de um handler previamente associado.
 */
esp_err_t hs_event_unregister(esp_event_base_t base, int32_t id, esp_event_handler_t handler);

#ifdef __cplusplus
}
#endif

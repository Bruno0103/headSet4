/**
 * @file bt_link_mgr.c
 * @brief Implementação do Actor bt_link (Gerenciador de Links Bluetooth, slots e pareamento).
 *
 * Arquitetura Event-Driven & Actor Model (AGENTS.md §4.2, WP 4.1):
 * - Migrado de chamadas síncronas com mutex recursivo para o Actor bt_link:
 *   - Core de afinidade: 0 (Core do stack Bluetooth/Bluedroid)
 *   - Prioridade FreeRTOS: 6
 *   - Ciclo de vida: Fixo (iniciado após a subida do stack Bluetooth)
 *   - Fila de comandos dedicada: 24 mensagens
 * - Eliminação total de mutexes/semáforos recursivos e concorrência direta no estado de slots.
 * - Todos os timers (esp_timer) despacham comandos internos para a fila do próprio ator,
 *   garantindo execução sequencial e thread-safe.
 * - Callbacks do Bluedroid (GAP/ACL/Auth) apenas repassam mensagens leves para a fila do ator.
 * - Exceção documentada (§2): SSP Confirm síncrono consulta estado atômico de pareamento / Fast Pair.
 * - As escritas e leituras de configurações continuam delegadas ao Actor settings (dono único do NVS).
 */

#include "bt_link_mgr.h"

#include <stdatomic.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "settings.h"

#include "apds9930.h"
#include "audio.h"
#include "bt_a2dp.h"
#include "bt_ble.h"
#include "bt_fastpair.h"
#include "bt_gap.h"
#include "bt_hfp.h"
#include "hs_actor.h"
#include "hs_cmds.h"
#include "hs_events.h"

static const char *TAG = "act_bt_link";

#define PAIRING_WINDOW_MS   120000
#define COMPLETE_DELAY_MS   800       /* Acelera a subida do perfil de voz HFP após o A2DP conectar (reduzido de 2500 ms) */
#define VOLUME_SAVE_MS      5000
#define DEFAULT_VOLUME      64
#define MAX_CONNECT_TRIES   12

/**
 * Tabela de reconexão ultrarrápida (ativa quando todos os slots estiverem cheios):
 * - Tentativa 0: 50 ms (reconexão praticamente instantânea ao ligar, colocar na cabeça ou trocar de slot)
 * - Tentativa 1: 1.000 ms (tempo para o host Bluetooth do celular reativar a banda base)
 * - Tentativas seguintes: recuo progressivo defensivo
 */
static const uint32_t k_backoff_ultrafast_ms[] = { 50, 1000, 2500, 5000, 8000, 15000, 30000 };

/**
 * Tabela de reconexão padrão (usada enquanto houver slot vazio para novos dispositivos):
 * - Deixa janela temporal adequada para anúncios e sincronização BLE de Fast Pair / Swift Pair
 */
static const uint32_t k_backoff_standard_ms[]  = { 300, 1500, 3000, 5000, 8000, 15000, 30000 };

/* Estrutura armazenada no NVS */
typedef struct {
    uint8_t bda[ESP_BD_ADDR_LEN];
    uint8_t valid;
    uint8_t volume;
    uint8_t acct_ref;
} slot_nv_t;

/* Comandos internos privados do Actor bt_link (offset além dos públicos em hs_cmds.h) */
enum {
    BT_INTERNAL_CMD_CONNECT_TIMER = HS_CMD_BT_BASE + 0x80,
    BT_INTERNAL_CMD_COMPLETE_TIMER,
    BT_INTERNAL_CMD_PAIR_TIMER,
    BT_INTERNAL_CMD_VOLUME_SAVE_TIMER,
    BT_INTERNAL_CMD_GAP_AUTH_COMPLETE,
    BT_INTERNAL_CMD_REMOTE_NAME,
    BT_INTERNAL_CMD_EVENT,
    BT_INTERNAL_CMD_SET_ACCOUNT_REF,
    BT_INTERNAL_CMD_SET_VOLUME,
    BT_INTERNAL_CMD_GET_VOLUME,
    BT_INTERNAL_CMD_GET_STATUS,
};

/* Payload interno para eventos do barramento repassados ao ator */
typedef struct {
    esp_event_base_t base;
    int32_t          id;
    uint8_t          len;
    uint8_t          payload[32];
} bt_internal_evt_msg_t;

/* Payload interno para autenticação GAP */
typedef struct {
    esp_bd_addr_t bda;
    bool          success;
    char          device_name[32];
} bt_internal_auth_msg_t;

/* Payload interno para nome remoto recebido via GAP */
typedef struct {
    esp_bd_addr_t bda;
    char          device_name[32];
} bt_internal_remote_name_msg_t;

/* Estado privado mantido exclusivamente dentro da task do ator */
typedef struct {
    slot_nv_t        slot[BT_LINK_NUM_SLOTS];
    bt_link_state_t  state[BT_LINK_NUM_SLOTS];
    int              sel;
    bool             worn;
    bool             pairing;
    int              attempts;
    int              battery;
    bool             auto_switch;
    bool             switching;
    bool             manual_disconnect;
    int64_t          switch_start_us;

    struct {
        uint8_t bda[ESP_BD_ADDR_LEN];
        uint8_t profiles;
        bool    complete_tried;
    } link;
} bt_link_actor_state_t;

/* Instância singleton do Ator */
static hs_actor_t *s_bt_actor = NULL;

/* Estado mantido dentro do contexto do ator */
static bt_link_actor_state_t s_ctx;

/* Variáveis atômicas / rápidas para consulta síncrona sem bloqueio (getters e SSP confirm) */
static atomic_bool s_atomic_pairing    = ATOMIC_VAR_INIT(false);
static atomic_int  s_atomic_sel        = ATOMIC_VAR_INIT(0);
static atomic_int  s_atomic_state[BT_LINK_NUM_SLOTS] = { ATOMIC_VAR_INIT(BT_LINK_SLEEP), ATOMIC_VAR_INIT(BT_LINK_SLEEP) };
static atomic_int  s_atomic_volume     = ATOMIC_VAR_INIT(DEFAULT_VOLUME);
static atomic_bool s_atomic_slots_full = ATOMIC_VAR_INIT(false);

/* Timers FreeRTOS / esp_timer */
static esp_timer_handle_t s_connect_tmr  = NULL;
static esp_timer_handle_t s_complete_tmr = NULL;
static esp_timer_handle_t s_pair_tmr     = NULL;
static esp_timer_handle_t s_vol_tmr      = NULL;

/* ============================================================================
 * DECLARAÇÕES ANTECIPADAS DAS OPERAÇÕES DO ATOR
 * ============================================================================ */
static void apply_state(void);
static void start_pairing(void);
static void stop_pairing(const char *why);
static void schedule_connect(void);
static void stop_connect_timer(void);
static bool has_empty_slot(void);
static void update_slots_full_status(void);
static void switch_to_slot(int target_slot);
static void post_slot_changed(void);

/* ============================================================================
 * PERSISTÊNCIA NVS (DELEGADA AO ACTOR SETTINGS)
 * ============================================================================ */

static void nvs_load(void)
{
    memset(s_ctx.slot, 0, sizeof(s_ctx.slot));
    for (int i = 0; i < BT_LINK_NUM_SLOTS; i++) {
        s_ctx.slot[i].volume = DEFAULT_VOLUME;
    }
    s_ctx.sel = 0;

    slot_nv_t tmp[BT_LINK_NUM_SLOTS];
    if (settings_get_blob(SETTINGS_KEY_BT_SLOTS, tmp, sizeof(tmp)) == ESP_OK) {
        memcpy(s_ctx.slot, tmp, sizeof(s_ctx.slot));
    } else {
        ESP_LOGI(TAG, "Sem slots salvos no actor settings (primeiro boot)");
    }

    uint8_t sel = 0;
    if (settings_get_u8(SETTINGS_KEY_BT_SEL_SLOT, &sel, 0) == ESP_OK && sel < BT_LINK_NUM_SLOTS) {
        s_ctx.sel = sel;
    }

    /* Auto-switch de fundo desativado: fone opera em modo 1-Clique Estável */
    s_ctx.auto_switch = false;

    atomic_store(&s_atomic_sel, s_ctx.sel);
    atomic_store(&s_atomic_volume, s_ctx.slot[s_ctx.sel].volume);
    update_slots_full_status();
}

static void nvs_save(void)
{
    esp_err_t err = settings_set_blob(SETTINGS_KEY_BT_SLOTS, s_ctx.slot, sizeof(s_ctx.slot));
    if (err == ESP_OK) {
        err = settings_set_u8(SETTINGS_KEY_BT_SEL_SLOT, (uint8_t)s_ctx.sel);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao solicitar gravação de slots ao actor settings: %s", esp_err_to_name(err));
    }
    update_slots_full_status();
}

/* ============================================================================
 * HELPERS PRIVADOS DE LÓGICA DO LINK MANAGER
 * ============================================================================ */

static bool sel_valid(void)
{
    return s_ctx.slot[s_ctx.sel].valid != 0;
}

static bool is_sel_bda(const uint8_t *bda)
{
    return sel_valid() && memcmp(s_ctx.slot[s_ctx.sel].bda, bda, ESP_BD_ADDR_LEN) == 0;
}

/**
 * @brief Verifica se há algum slot vazio (não preenchido/não válido).
 * Permite que novos aparelhos se conectem via Fast Pair e Swift Pair.
 */
static bool has_empty_slot(void)
{
    for (int i = 0; i < BT_LINK_NUM_SLOTS; i++) {
        if (!s_ctx.slot[i].valid) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Atualiza a variável atômica s_atomic_slots_full com base nos slots salvos.
 * Quando ambos os slots estiverem cheios, o headset migra para o modo de reconexão ultrarrápido.
 */
static void update_slots_full_status(void)
{
    bool full = !has_empty_slot();
    atomic_store(&s_atomic_slots_full, full);
    ESP_LOGI(TAG, "Status dos slots: %s (slot 0: %s, slot 1: %s)",
             full ? "100% CHEIOS -> MODO ULTRARRAPIDO" : "HA VAGAS -> MODO DESCOBERTA (Fast Pair / Swift Pair)",
             s_ctx.slot[0].valid ? "ocupado" : "vazio",
             s_ctx.slot[1].valid ? "ocupado" : "vazio");
}

static void drop_link(void)
{
    if (!s_ctx.link.profiles) {
        return;
    }
    ESP_LOGI(TAG, "Desconectando " ESP_BD_ADDR_STR " (perfis 0x%X)", ESP_BD_ADDR_HEX(s_ctx.link.bda), s_ctx.link.profiles);
    if (s_ctx.link.profiles & BT_PROFILE_HFP) {
        bt_hfp_disconnect(s_ctx.link.bda);
    }
    if (s_ctx.link.profiles & BT_PROFILE_A2DP) {
        bt_a2dp_disconnect(s_ctx.link.bda);
    }
}

/**
 * @brief Executa comutação atômica e graciosa para um slot desejado (Protocolo 1-Clique Estável).
 *
 * 1. Para imediatamente o streaming de áudio I2S no Core 1 via audio_cmd_stop_send(1) para evitar ruídos residuais.
 * 2. Se o slot anterior estava em pareamento, encerra.
 * 3. Se houver link ativo conectado cujo endereço BDA difere do novo slot, sinaliza switching = true
 *    e chama drop_link() (desconexão graciosa do A2DP e HFP).
 * 4. Garante que manual_disconnect = false e attempts = 0 para permitir a reconexão imediata.
 * 5. Salva novo slot selecionado e notifica o barramento de eventos (post_slot_changed()).
 * 6. Ao receber LINK_DOWN do link anterior (ou se já não havia link ativo), apply_state() dispara
 *    a reconexão ultrarrápida (50 ms) para o novo slot (ou abre modo de pareamento se o slot for vazio).
 */
static void switch_to_slot(int target_slot)
{
    if (target_slot < 0 || target_slot >= BT_LINK_NUM_SLOTS) {
        return;
    }

    /* Se houver troca em andamento mas já passou mais de 2,5 segundos, expira o bloqueio de segurança */
    if (s_ctx.switching) {
        int64_t elapsed_ms = (esp_timer_get_time() - s_ctx.switch_start_us) / 1000;
        if (elapsed_ms < 2500) {
            ESP_LOGW(TAG, "Troca de dispositivos em andamento (%lld ms): aguarde conclusao", (long long)elapsed_ms);
            return;
        } else {
            ESP_LOGW(TAG, "Timeout na comutacao anterior (%lld ms); forcando novo slot", (long long)elapsed_ms);
            s_ctx.switching = false;
            s_ctx.link.profiles = 0; /* Descarta estado fantasma */
        }
    }

    if (s_ctx.pairing) {
        stop_pairing("troca_slot");
    }

    /* Silencia o pipeline de áudio I2S no Core 1 imediatamente para evitar estalos residuais */
    audio_cmd_stop_send(1);

    bool need_disconnect = false;
    if (s_ctx.link.profiles != 0) {
        /* Se o dispositivo conectado atual é diferente do que está salvo no target_slot */
        if (!s_ctx.slot[target_slot].valid || memcmp(s_ctx.link.bda, s_ctx.slot[target_slot].bda, ESP_BD_ADDR_LEN) != 0) {
            need_disconnect = true;
        }
    }

    s_ctx.sel = target_slot;
    atomic_store(&s_atomic_sel, s_ctx.sel);
    atomic_store(&s_atomic_volume, s_ctx.slot[s_ctx.sel].volume);
    s_ctx.attempts = 0;
    s_ctx.manual_disconnect = false; /* Intenção voluntária de conexão pelo usuário */
    nvs_save();
    ESP_LOGI(TAG, ">>> [1-CLIQUE] Slot %d ativado (%s)", s_ctx.sel, sel_valid() ? "pareado" : "vazio");
    post_slot_changed();
    stop_connect_timer();

    if (need_disconnect) {
        s_ctx.switching = true;
        s_ctx.switch_start_us = esp_timer_get_time();
        ESP_LOGI(TAG, "Desconectando dispositivo anterior antes de assumir Slot %d...", s_ctx.sel);
        drop_link();
    } else {
        s_ctx.switching = false;
        apply_state();
    }
}

static void stop_connect_timer(void)
{
    if (s_connect_tmr)  esp_timer_stop(s_connect_tmr);
    if (s_complete_tmr) esp_timer_stop(s_complete_tmr);
}

static void schedule_connect(void)
{
    if (!s_connect_tmr || esp_timer_is_active(s_connect_tmr)) {
        return;
    }
    bool ultrafast = !has_empty_slot();
    const uint32_t *table = ultrafast ? k_backoff_ultrafast_ms : k_backoff_standard_ms;
    size_t n = ultrafast ? (sizeof(k_backoff_ultrafast_ms) / sizeof(k_backoff_ultrafast_ms[0]))
                         : (sizeof(k_backoff_standard_ms) / sizeof(k_backoff_standard_ms[0]));
    uint32_t ms = table[s_ctx.attempts < (int)n ? s_ctx.attempts : (int)n - 1];
    ESP_LOGI(TAG, "[%s] Reconexao com slot %d em %u ms (tentativa %d/%d)",
             ultrafast ? "MODO ULTRARRAPIDO" : "MODO DESCOBERTA",
             s_ctx.sel, (unsigned)ms, s_ctx.attempts + 1, MAX_CONNECT_TRIES);
    esp_timer_start_once(s_connect_tmr, (uint64_t)ms * 1000);
}

static void post_slot_changed(void)
{
    bt_slot_evt_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.active_slot = (uint8_t)s_ctx.sel;
    if (s_ctx.slot[s_ctx.sel].valid) {
        memcpy(ev.bda, s_ctx.slot[s_ctx.sel].bda, ESP_BD_ADDR_LEN);
    }
    ev.is_connected = (s_ctx.link.profiles != 0) && is_sel_bda(s_ctx.link.bda);
    hs_event_post(BT_EVT, BT_EVT_SLOT_CHANGED, &ev, sizeof(ev));
}

static const char *state_str(bt_link_state_t s)
{
    return s == BT_LINK_ACTIVE ? "ACTIVE" : "SLEEP";
}

static void set_states(void)
{
    for (int i = 0; i < BT_LINK_NUM_SLOTS; i++) {
        bt_link_state_t ns = (i == s_ctx.sel && (s_ctx.worn || s_ctx.pairing)) ? BT_LINK_ACTIVE : BT_LINK_SLEEP;
        if (ns != s_ctx.state[i]) {
            ESP_LOGI(TAG, "Slot %d: %s -> %s", i, state_str(s_ctx.state[i]), state_str(ns));
            s_ctx.state[i] = ns;
            atomic_store(&s_atomic_state[i], (int)ns);
        }
    }
}

static void apply_state(void)
{
    set_states();

    if (s_ctx.pairing) {
        stop_connect_timer();
        bt_gap_set_scan(true, true);
        bt_ble_set_profile(BT_BLE_ADV_PAIRING);
        return;
    }

    if (!s_ctx.worn) {
        /* SLEEP: Classic fora do ar, so BLE lento aguardando proximidade */
        stop_connect_timer();
        drop_link();
        bt_gap_set_scan(false, false);
        bt_ble_set_profile(BT_BLE_ADV_SLEEP);
        return;
    }

    /* ACTIVE */
    bt_ble_set_profile(BT_BLE_ADV_ACTIVE);
    if (!sel_valid()) {
        ESP_LOGI(TAG, "Slot %d vazio: abrindo janela de pareamento (Fast Pair & Swift Pair ativos)", s_ctx.sel);
        start_pairing();
        return;
    }
    bt_gap_set_scan(true, false);

    if (s_ctx.link.profiles && !is_sel_bda(s_ctx.link.bda)) {
        drop_link();       /* troca de slot: o LINK_DOWN volta aqui para conectar o novo */
        return;
    }
    if (!s_ctx.link.profiles) {
        if (s_ctx.manual_disconnect) {
            ESP_LOGI(TAG, "Reconexao automatica suprimida: dispositivo foi desconectado voluntariamente.");
        } else {
            if (!has_empty_slot()) {
                ESP_LOGI(TAG, "[MODO ULTRARRAPIDO] Todos os slots cheios (0 e 1 validos). Reconexao imediata ativada.");
            } else {
                ESP_LOGI(TAG, "[MODO DESCOBERTA] Ha slot livre. Fast Pair e Swift Pair disponiveis.");
            }
            schedule_connect();
        }
    }
}

static void stop_pairing(const char *why)
{
    if (!s_ctx.pairing) {
        return;
    }
    ESP_LOGI(TAG, "Janela de pareamento encerrada (%s)", why);
    s_ctx.pairing = false;
    atomic_store(&s_atomic_pairing, false);

    if (s_pair_tmr) esp_timer_stop(s_pair_tmr);
    bt_pairing_evt_t ev = { .active = false, .timeout_sec = 0 };
    hs_event_post(BT_EVT, BT_EVT_PAIRING_MODE, &ev, sizeof(ev));
    bt_fastpair_on_pairing_mode(false);

    /* Se o pareamento encerrou (timeout ou cancelamento) e o slot atual continua vazio,
     * mas existe outro slot com aparelho válido pareado, retorna para o slot válido existente! */
    if (!sel_valid()) {
        for (int i = 0; i < BT_LINK_NUM_SLOTS; i++) {
            if (s_ctx.slot[i].valid) {
                ESP_LOGI(TAG, "Pareamento sem sucesso no slot %d: retornando ao slot %d existente", s_ctx.sel, i);
                switch_to_slot(i);
                return;
            }
        }
    }

    apply_state();
}

static void start_pairing(void)
{
    if (s_ctx.pairing) {
        return;
    }
    /* Se o slot selecionado atual já possui dispositivo pareado e o outro slot está vazio,
     * direciona o pareamento para o slot livre para NÃO sobrescrever o aparelho existente! */
    if (sel_valid()) {
        for (int i = 0; i < BT_LINK_NUM_SLOTS; i++) {
            if (!s_ctx.slot[i].valid) {
                s_ctx.sel = i;
                atomic_store(&s_atomic_sel, s_ctx.sel);
                ESP_LOGI(TAG, "Direcionando janela de pareamento para o slot livre %d", s_ctx.sel);
                break;
            }
        }
    }

    ESP_LOGI(TAG, "Janela de pareamento aberta para o slot %d (%d s)", s_ctx.sel, PAIRING_WINDOW_MS / 1000);
    s_ctx.pairing = true;
    atomic_store(&s_atomic_pairing, true);
    s_ctx.attempts = 0;
    drop_link();

    if (s_pair_tmr) esp_timer_start_once(s_pair_tmr, (uint64_t)PAIRING_WINDOW_MS * 1000);
    bt_pairing_evt_t ev = { .active = true, .timeout_sec = PAIRING_WINDOW_MS / 1000 };
    hs_event_post(BT_EVT, BT_EVT_PAIRING_MODE, &ev, sizeof(ev));
    bt_fastpair_on_pairing_mode(true);
    apply_state();
}

/* ============================================================================
 * CALLBACKS DE TIMERS (DISPARAM COMANDOS INTERNOS AO ATOR)
 * ============================================================================ */

static void connect_timer_cb(void *arg)
{
    if (s_bt_actor) {
        hs_actor_send(s_bt_actor, BT_INTERNAL_CMD_CONNECT_TIMER, NULL, 0, 0);
    }
}

static void complete_timer_cb(void *arg)
{
    if (s_bt_actor) {
        hs_actor_send(s_bt_actor, BT_INTERNAL_CMD_COMPLETE_TIMER, NULL, 0, 0);
    }
}

static void pair_timer_cb(void *arg)
{
    if (s_bt_actor) {
        hs_actor_send(s_bt_actor, BT_INTERNAL_CMD_PAIR_TIMER, NULL, 0, 0);
    }
}

static void volume_timer_cb(void *arg)
{
    if (s_bt_actor) {
        hs_actor_send(s_bt_actor, BT_INTERNAL_CMD_VOLUME_SAVE_TIMER, NULL, 0, 0);
    }
}

/* ============================================================================
 * TRATAMENTO DE EVENTOS DO BARRAMENTO (DESPACHO PARA O ATOR)
 * ============================================================================ */

static void on_bus_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    /* Handler do esp_event NUNCA bloqueia: empacota e enfileira no Actor bt_link */
    if (!s_bt_actor) {
        return;
    }
    bt_internal_evt_msg_t msg = {
        .base = base,
        .id = id,
        .len = 0,
    };
    if (data != NULL) {
        if (base == BT_EVT) {
            switch (id) {
            case BT_EVT_LINK_UP:
            case BT_EVT_LINK_DOWN:
                memcpy(msg.payload, data, sizeof(bt_link_evt_t));
                msg.len = sizeof(bt_link_evt_t);
                break;
            case BT_EVT_STREAMING:
                memcpy(msg.payload, data, sizeof(bt_streaming_evt_t));
                msg.len = sizeof(bt_streaming_evt_t);
                break;
            default:
                break;
            }
        } else if (base == SENSOR_EVT) {
            switch (id) {
            case SENSOR_EVT_BUTTON_SHORT:
            case SENSOR_EVT_BUTTON_LONG:
                memcpy(msg.payload, data, sizeof(sensor_button_evt_t));
                msg.len = sizeof(sensor_button_evt_t);
                break;
            case SENSOR_EVT_BATTERY:
                memcpy(msg.payload, data, sizeof(sensor_battery_evt_t));
                msg.len = sizeof(sensor_battery_evt_t);
                break;
            default:
                break;
            }
        }
    }
    hs_actor_send(s_bt_actor, BT_INTERNAL_CMD_EVENT, &msg, sizeof(msg), 0);
}

/* ============================================================================
 * CALLBACKS DO BLUEDROID / GAP
 * ============================================================================ */

/* Exceção documentada (§2): SSP confirm síncrono da pilha */
static bt_gap_cfm_decision_t on_ssp_confirm(const uint8_t *bda, uint32_t passkey)
{
    if (bt_fastpair_session_active()) {
        return bt_fastpair_ssp_confirm(bda, passkey);
    }
    if (atomic_load(&s_atomic_pairing)) {
        ESP_LOGI(TAG, "Pareamento manual: aceitando " ESP_BD_ADDR_STR, ESP_BD_ADDR_HEX(bda));
        return BT_GAP_CFM_ACCEPT;
    }

    /* Se o dispositivo já tiver vínculo prévio registrado em qualquer slot ou no GAP, aceita a reconexão */
    if (bt_gap_is_bonded(bda)) {
        ESP_LOGI(TAG, "Reconexao de dispositivo vinculado aceita: " ESP_BD_ADDR_STR, ESP_BD_ADDR_HEX(bda));
        return BT_GAP_CFM_ACCEPT;
    }

    ESP_LOGW(TAG, "SSP de " ESP_BD_ADDR_STR " recusado: fora da janela de pareamento", ESP_BD_ADDR_HEX(bda));
    return BT_GAP_CFM_REJECT;
}

static void on_auth_complete(const uint8_t *bda, bool success, const char *device_name)
{
    if (!s_bt_actor) {
        return;
    }
    bt_internal_auth_msg_t auth;
    memset(&auth, 0, sizeof(auth));
    memcpy(auth.bda, bda, ESP_BD_ADDR_LEN);
    auth.success = success;
    if (device_name) {
        strncpy(auth.device_name, device_name, sizeof(auth.device_name) - 1);
    }
    hs_actor_send(s_bt_actor, BT_INTERNAL_CMD_GAP_AUTH_COMPLETE, &auth, sizeof(auth), 0);
}

static void on_acl(const uint8_t *bda, bool connected, uint8_t reason)
{
    ESP_LOGD(TAG, "ACL " ESP_BD_ADDR_STR " %s (0x%02X)", ESP_BD_ADDR_HEX(bda), connected ? "up" : "down", reason);
}

static void on_remote_name(const uint8_t *bda, const char *device_name)
{
    if (!s_bt_actor || !bda || !device_name) {
        return;
    }
    bt_internal_remote_name_msg_t rmt;
    memset(&rmt, 0, sizeof(rmt));
    memcpy(rmt.bda, bda, ESP_BD_ADDR_LEN);
    strncpy(rmt.device_name, device_name, sizeof(rmt.device_name) - 1);
    hs_actor_send(s_bt_actor, BT_INTERNAL_CMD_REMOTE_NAME, &rmt, sizeof(rmt), 0);
}

/* ============================================================================
 * LÓGICA DE PROCESSAMENTO DE MENSAGENS DO ACTOR BT_LINK (RUNS NO CORE 0, PRIO 6)
 * ============================================================================ */

static void handle_bus_evt_in_actor(const bt_internal_evt_msg_t *ev_msg)
{
    if (ev_msg->base == SENSOR_EVT) {
        switch (ev_msg->id) {
        case SENSOR_EVT_WORN:
            ESP_LOGI(TAG, "Fone colocado");
            s_ctx.worn = true;
            s_ctx.attempts = 0;
            apply_state();
            break;

        case SENSOR_EVT_REMOVED:
            ESP_LOGI(TAG, "Fone retirado");
            s_ctx.worn = false;
            apply_state();
            break;

        case SENSOR_EVT_BUTTON_SHORT: {
            int next_slot = (s_ctx.sel + 1) % BT_LINK_NUM_SLOTS;
            ESP_LOGI(TAG, "Botão curto acionado: avaliando comutação para Slot %d (atual: Slot %d, valid: %d)",
                     next_slot, s_ctx.sel, s_ctx.slot[s_ctx.sel].valid);
            if (s_ctx.slot[next_slot].valid) {
                /* O slot de destino possui dispositivo pareado: executa comutação 1-Clique estável */
                ESP_LOGI(TAG, "Slot %d válido: comutando link ativo", next_slot);
                switch_to_slot(next_slot);
            } else if (sel_valid()) {
                /* Slot de destino está vazio, mas o slot atual possui aparelho ativo ou pareado:
                 * NÃO derruba o link atual para não cortar o áudio nem desconectar o BLE GATT espuriamente. */
                ESP_LOGW(TAG, "Slot %d vazio. Mantendo Slot %d ativo. Para parear um novo dispositivo no Slot %d, mantenha o botão pressionado por 3s (clique longo).",
                         next_slot, s_ctx.sel, next_slot);
            } else {
                /* Nenhum slot possui dispositivo: abre janela de pareamento */
                ESP_LOGI(TAG, "Nenhum slot pareado: abrindo pareamento");
                start_pairing();
            }
            break;
        }

        case SENSOR_EVT_BUTTON_LONG:
            if (s_ctx.pairing) {
                stop_pairing("botao");
            } else {
                start_pairing();
            }
            break;

        case SENSOR_EVT_BATTERY: {
            const sensor_battery_evt_t *ev = (const sensor_battery_evt_t *)ev_msg->payload;
            s_ctx.battery = ev->percent;
            /* Sempre repassa: bt_hfp_report_battery ignora sozinho se o SLC nao esta ativo.
             * Antes dependia de profiles&HFP, que nao estava setado mesmo com o SLC conectado
             * (log: "HFP conectado (SLC)" sem "Link ativo ... perfis 0x3"). */
            bt_hfp_report_battery(ev->percent);
            break;
        }

        default:
            break;
        }
    } else if (ev_msg->base == BT_EVT) {
        switch (ev_msg->id) {
        case BT_EVT_LINK_UP: {
            const bt_link_evt_t *ev = (const bt_link_evt_t *)ev_msg->payload;
            if (!is_sel_bda(ev->bda)) {
                ESP_LOGW(TAG, "Perfil 0x%X de " ESP_BD_ADDR_STR " recusado (nao e o slot selecionado)", ev->profile,
                         ESP_BD_ADDR_HEX(ev->bda));
                if (ev->profile & BT_PROFILE_HFP) {
                    bt_hfp_disconnect((uint8_t *)ev->bda);
                }
                if (ev->profile & BT_PROFILE_A2DP) {
                    bt_a2dp_disconnect((uint8_t *)ev->bda);
                }
                break;
            }
            if (!s_ctx.link.profiles) {
                memcpy(s_ctx.link.bda, ev->bda, ESP_BD_ADDR_LEN);
                s_ctx.link.complete_tried = false;
            }
            s_ctx.link.profiles |= ev->profile;
            s_ctx.attempts = 0;
            s_ctx.manual_disconnect = false;
            if (s_connect_tmr) esp_timer_stop(s_connect_tmr);
            ESP_LOGI(TAG, "Link ativo com " ESP_BD_ADDR_STR " (perfis 0x%X)", ESP_BD_ADDR_HEX(s_ctx.link.bda), s_ctx.link.profiles);
            post_slot_changed();

            /* Emite imediatamente o nome amigável salvo no NVS para sincronizar a UI e o App */
            const char *key_name = (s_ctx.sel == 0) ? SETTINGS_KEY_PEER_NAME_SLOT0 : SETTINGS_KEY_PEER_NAME_SLOT1;
            char cached_name[32] = {0};
            if (settings_get_blob(key_name, cached_name, sizeof(cached_name) - 1) == ESP_OK && cached_name[0] != '\0') {
                bt_peer_name_evt_t name_evt;
                memset(&name_evt, 0, sizeof(name_evt));
                name_evt.slot = (uint8_t)s_ctx.sel;
                memcpy(name_evt.bda, s_ctx.link.bda, ESP_BD_ADDR_LEN);
                strncpy(name_evt.name, cached_name, sizeof(name_evt.name) - 1);
                hs_event_post(BT_EVT, BT_EVT_PEER_NAME, &name_evt, sizeof(name_evt));
            }
            /* Solicita atualização/confirmação do nome remoto via GAP HCI */
            bt_gap_request_remote_name(s_ctx.link.bda);

            if ((ev->profile & BT_PROFILE_HFP) && s_ctx.battery >= 0) {
                bt_hfp_report_battery((uint8_t)s_ctx.battery);
            }
            if (s_ctx.link.profiles != (BT_PROFILE_A2DP | BT_PROFILE_HFP) && !s_ctx.link.complete_tried) {
                if (s_complete_tmr) {
                    esp_timer_stop(s_complete_tmr);
                    esp_timer_start_once(s_complete_tmr, (uint64_t)COMPLETE_DELAY_MS * 1000);
                }
            }
            break;
        }

        case BT_EVT_LINK_DOWN: {
            const bt_link_evt_t *ev = (const bt_link_evt_t *)ev_msg->payload;
            if (s_ctx.link.profiles && memcmp(s_ctx.link.bda, ev->bda, ESP_BD_ADDR_LEN) == 0) {
                s_ctx.link.profiles &= ~ev->profile;
                if (ev->is_voluntary) {
                    if (s_ctx.switching) {
                        /* Desconexão foi solicitada pelo headset para comutar de slot: NÃO marcar manual_disconnect */
                        s_ctx.manual_disconnect = false;
                        ESP_LOGI(TAG, "Desconexao do slot anterior concluida durante comutacao (switching=true, manual_disconnect=false)");
                    } else {
                        /* Desconexão voluntária feita pelo próprio smartphone */
                        s_ctx.manual_disconnect = true;
                        ESP_LOGI(TAG, "Desconexao voluntaria recebida do smartphone (manual_disconnect=true)");
                    }
                }
                ESP_LOGI(TAG, "Perfil 0x%X caiu (restam 0x%X, voluntario=%d)", ev->profile, s_ctx.link.profiles, ev->is_voluntary);
                if (!s_ctx.link.profiles) {
                    if (s_complete_tmr) esp_timer_stop(s_complete_tmr);
                    s_ctx.switching = false;
                    s_ctx.manual_disconnect = false; /* Garante prontidão para o novo slot */
                    post_slot_changed();
                    apply_state();
                }
            }
            break;
        }

        case BT_EVT_STREAMING: {
            const bt_streaming_evt_t *ev = (const bt_streaming_evt_t *)ev_msg->payload;
            bt_ble_set_streaming(ev->streaming);
            break;
        }

        default:
            break;
        }
    }
}

static void bt_link_actor_fn(hs_actor_t *self, const hs_msg_t *msg)
{
    switch (msg->cmd) {
    case BT_CMD_SELECT_SLOT: {
        if (msg->len >= sizeof(bt_cmd_select_slot_t)) {
            const bt_cmd_select_slot_t *cmd = (const bt_cmd_select_slot_t *)msg->data;
            if (cmd->slot < BT_LINK_NUM_SLOTS) {
                switch_to_slot(cmd->slot);
            }
        }
        break;
    }

    case BT_CMD_CONNECT: {
        if (msg->len >= sizeof(bt_cmd_connect_t)) {
            const bt_cmd_connect_t *cmd = (const bt_cmd_connect_t *)msg->data;
            const uint8_t *target_bda = cmd->bda;
            static const uint8_t zero_bda[6] = {0};
            if (memcmp(target_bda, zero_bda, 6) == 0 && cmd->slot < BT_LINK_NUM_SLOTS && s_ctx.slot[cmd->slot].valid) {
                target_bda = s_ctx.slot[cmd->slot].bda;
            }
            ESP_LOGI(TAG, "Comando BT_CMD_CONNECT recebido para " ESP_BD_ADDR_STR, ESP_BD_ADDR_HEX(target_bda));
            bt_a2dp_connect((uint8_t *)target_bda);
        }
        break;
    }

    case BT_CMD_DISCONNECT: {
        if (s_ctx.link.profiles) {
            drop_link();
        }
        break;
    }

    case BT_CMD_START_PAIRING: {
        if (!s_ctx.pairing) {
            start_pairing();
        }
        break;
    }

    case BT_CMD_CANCEL_PAIRING: {
        if (s_ctx.pairing) {
            stop_pairing("comando");
        }
        break;
    }

    case BT_CMD_SET_AUTO_SWITCH: {
        /* Auto-switch de fundo foi eliminado: o sistema opera em modo 1-Clique Estável */
        s_ctx.auto_switch = false;
        settings_set_u8(SETTINGS_KEY_BT_AUTOSWITCH, 0);
        ESP_LOGI(TAG, "Auto-switch desativado: modo determinístico 1-Clique ativo");
        break;
    }

    case BT_INTERNAL_CMD_CONNECT_TIMER: {
        if (s_ctx.worn && !s_ctx.pairing && sel_valid() && !s_ctx.link.profiles) {
            if (++s_ctx.attempts > MAX_CONNECT_TRIES) {
                ESP_LOGW(TAG, "Slot %d nao respondeu apos %d tentativas; aguardando celular", s_ctx.sel,
                         MAX_CONNECT_TRIES);
            } else {
                ESP_LOGI(TAG, "Conectando A2DP em " ESP_BD_ADDR_STR, ESP_BD_ADDR_HEX(s_ctx.slot[s_ctx.sel].bda));
                if (bt_a2dp_connect(s_ctx.slot[s_ctx.sel].bda) != ESP_OK) {
                    ESP_LOGW(TAG, "bt_a2dp_connect recusado pela pilha");
                }
                schedule_connect();
            }
        }
        break;
    }

    case BT_INTERNAL_CMD_COMPLETE_TIMER: {
        if (s_ctx.link.profiles && !s_ctx.link.complete_tried) {
            s_ctx.link.complete_tried = true;
            if (!(s_ctx.link.profiles & BT_PROFILE_A2DP)) {
                bt_a2dp_connect(s_ctx.link.bda);
            } else if (!(s_ctx.link.profiles & BT_PROFILE_HFP)) {
                bt_hfp_connect(s_ctx.link.bda);
            }
        }
        break;
    }

    case BT_INTERNAL_CMD_PAIR_TIMER: {
        stop_pairing("timeout");
        break;
    }

    case BT_INTERNAL_CMD_VOLUME_SAVE_TIMER: {
        nvs_save();
        break;
    }

    case BT_INTERNAL_CMD_GAP_AUTH_COMPLETE: {
        if (msg->len >= sizeof(bt_internal_auth_msg_t)) {
            const bt_internal_auth_msg_t *auth = (const bt_internal_auth_msg_t *)msg->data;
            if (auth->success && s_ctx.pairing) {
                if (sel_valid() && memcmp(s_ctx.slot[s_ctx.sel].bda, auth->bda, ESP_BD_ADDR_LEN) != 0) {
                    ESP_LOGI(TAG, "Slot %d substituido; removendo bond anterior", s_ctx.sel);
                    bt_gap_remove_bond(s_ctx.slot[s_ctx.sel].bda);
                }
                for (int i = 0; i < BT_LINK_NUM_SLOTS; i++) {
                    if (i != s_ctx.sel && s_ctx.slot[i].valid && memcmp(s_ctx.slot[i].bda, auth->bda, ESP_BD_ADDR_LEN) == 0) {
                        memset(&s_ctx.slot[i], 0, sizeof(s_ctx.slot[i]));
                        s_ctx.slot[i].volume = DEFAULT_VOLUME;
                    }
                }
                memcpy(s_ctx.slot[s_ctx.sel].bda, auth->bda, ESP_BD_ADDR_LEN);
                s_ctx.slot[s_ctx.sel].valid = 1;
                s_ctx.slot[s_ctx.sel].acct_ref = 0;
                nvs_save();

                /* Persiste o nome do peer (WP 4.3) no actor settings */
                const char *key_name = (s_ctx.sel == 0) ? SETTINGS_KEY_PEER_NAME_SLOT0 : SETTINGS_KEY_PEER_NAME_SLOT1;
                if (auth->device_name[0] != '\0') {
                    settings_set_blob(key_name, auth->device_name, strlen(auth->device_name) + 1);
                }

                /* Publica BT_EVT_PEER_NAME no barramento central */
                bt_peer_name_evt_t name_evt;
                memset(&name_evt, 0, sizeof(name_evt));
                name_evt.slot = (uint8_t)s_ctx.sel;
                memcpy(name_evt.bda, auth->bda, ESP_BD_ADDR_LEN);
                strncpy(name_evt.name, auth->device_name, sizeof(name_evt.name) - 1);
                hs_event_post(BT_EVT, BT_EVT_PEER_NAME, &name_evt, sizeof(name_evt));

                ESP_LOGI(TAG, "Slot %d = " ESP_BD_ADDR_STR " (%s)", s_ctx.sel, ESP_BD_ADDR_HEX(auth->bda),
                         auth->device_name[0] ? auth->device_name : "sem nome");
                s_ctx.attempts = 0;
                stop_pairing("pareado");
            }
        }
        break;
    }

    case BT_INTERNAL_CMD_REMOTE_NAME: {
        if (msg->len >= sizeof(bt_internal_remote_name_msg_t)) {
            const bt_internal_remote_name_msg_t *rmt = (const bt_internal_remote_name_msg_t *)msg->data;
            int matched_slot = -1;
            for (int i = 0; i < BT_LINK_NUM_SLOTS; i++) {
                if (s_ctx.slot[i].valid && memcmp(s_ctx.slot[i].bda, rmt->bda, ESP_BD_ADDR_LEN) == 0) {
                    matched_slot = i;
                    break;
                }
            }
            if (matched_slot < 0 && s_ctx.link.profiles && memcmp(s_ctx.link.bda, rmt->bda, ESP_BD_ADDR_LEN) == 0) {
                matched_slot = s_ctx.sel;
            }
            if (matched_slot >= 0 && rmt->device_name[0] != '\0') {
                const char *key_name = (matched_slot == 0) ? SETTINGS_KEY_PEER_NAME_SLOT0 : SETTINGS_KEY_PEER_NAME_SLOT1;
                settings_set_blob(key_name, rmt->device_name, strlen(rmt->device_name) + 1);

                bt_peer_name_evt_t name_evt;
                memset(&name_evt, 0, sizeof(name_evt));
                name_evt.slot = (uint8_t)matched_slot;
                memcpy(name_evt.bda, rmt->bda, ESP_BD_ADDR_LEN);
                strncpy(name_evt.name, rmt->device_name, sizeof(name_evt.name) - 1);
                hs_event_post(BT_EVT, BT_EVT_PEER_NAME, &name_evt, sizeof(name_evt));
                ESP_LOGI(TAG, "Nome remoto atualizado para slot %d: '%s'", matched_slot, rmt->device_name);
            }
        }
        break;
    }

    case BT_INTERNAL_CMD_EVENT: {
        if (msg->len >= sizeof(bt_internal_evt_msg_t)) {
            handle_bus_evt_in_actor((const bt_internal_evt_msg_t *)msg->data);
        }
        break;
    }

    case BT_INTERNAL_CMD_SET_ACCOUNT_REF: {
        if (msg->len >= 1) {
            uint8_t ref = msg->data[0];
            s_ctx.slot[s_ctx.sel].acct_ref = ref;
            nvs_save();
        }
        break;
    }

    case BT_INTERNAL_CMD_SET_VOLUME: {
        if (msg->len >= 1) {
            uint8_t volume = msg->data[0];
            if (volume > 127) volume = 127;
            if (s_ctx.slot[s_ctx.sel].volume != volume) {
                s_ctx.slot[s_ctx.sel].volume = volume;
                atomic_store(&s_atomic_volume, volume);
                if (s_vol_tmr) {
                    esp_timer_stop(s_vol_tmr);
                    esp_timer_start_once(s_vol_tmr, (uint64_t)VOLUME_SAVE_MS * 1000);
                }
            }
        }
        break;
    }

    default:
        ESP_LOGD(TAG, "Comando BT desconhecido: 0x%04X", msg->cmd);
        break;
    }
}

/* ============================================================================
 * INTERFACE PÚBLICA / COMPATIBILIDADE
 * ============================================================================ */

struct hs_actor *bt_link_actor_get(void)
{
    return s_bt_actor;
}

bool bt_link_mgr_pairing_active(void)
{
    return atomic_load(&s_atomic_pairing);
}

int bt_link_mgr_selected(void)
{
    return atomic_load(&s_atomic_sel);
}

bt_link_state_t bt_link_mgr_state(int slot)
{
    if (slot >= 0 && slot < BT_LINK_NUM_SLOTS) {
        return (bt_link_state_t)atomic_load(&s_atomic_state[slot]);
    }
    return BT_LINK_SLEEP;
}

void bt_link_mgr_set_account_ref(uint8_t ref)
{
    if (s_bt_actor) {
        hs_actor_send(s_bt_actor, BT_INTERNAL_CMD_SET_ACCOUNT_REF, &ref, 1, 0);
    }
}

uint8_t bt_link_mgr_get_volume(void)
{
    return (uint8_t)atomic_load(&s_atomic_volume);
}

void bt_link_mgr_set_volume(uint8_t volume)
{
    if (volume > 127) volume = 127;
    atomic_store(&s_atomic_volume, volume);
    if (s_bt_actor) {
        hs_actor_send(s_bt_actor, BT_INTERNAL_CMD_SET_VOLUME, &volume, 1, 0);
    }
}

bool bt_link_mgr_slots_full(void)
{
    return atomic_load(&s_atomic_slots_full);
}

static esp_timer_handle_t make_timer(const char *name, esp_timer_cb_t cb)
{
    const esp_timer_create_args_t a = { .callback = cb, .name = name };
    esp_timer_handle_t t = NULL;
    if (esp_timer_create(&a, &t) != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao criar o timer %s", name);
    }
    return t;
}

esp_err_t bt_link_mgr_start(void)
{
    if (s_bt_actor != NULL) {
        return ESP_OK;
    }

    /* 1. Criação dos timers de conexão e debounce */
    s_connect_tmr  = make_timer("lm_conn", connect_timer_cb);
    s_complete_tmr = make_timer("lm_compl", complete_timer_cb);
    s_pair_tmr     = make_timer("lm_pair", pair_timer_cb);
    s_vol_tmr      = make_timer("lm_vol", volume_timer_cb);
    ESP_RETURN_ON_FALSE(s_connect_tmr && s_complete_tmr && s_pair_tmr && s_vol_tmr, ESP_ERR_NO_MEM, TAG, "timers");

    /* 2. Inicialização do estado e carga do NVS */
    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.worn = true;
    s_ctx.battery = -1;
    nvs_load();

    for (int i = 0; i < BT_LINK_NUM_SLOTS; i++) {
        if (s_ctx.slot[i].valid) {
            ESP_LOGI(TAG, "Slot %d: " ESP_BD_ADDR_STR " (volume %u)%s", i, ESP_BD_ADDR_HEX(s_ctx.slot[i].bda),
                     s_ctx.slot[i].volume, i == s_ctx.sel ? " [selecionado]" : "");
        } else {
            ESP_LOGI(TAG, "Slot %d: vazio%s", i, i == s_ctx.sel ? " [selecionado]" : "");
        }
    }

    /* 3. Criação do Actor bt_link (Core 0, prioridade 6, fila 24, fixo) */
    const hs_actor_cfg_t cfg = {
        .name      = "act_bt_link",
        .stack     = 4096,
        .prio      = 6,
        .core      = 0,
        .queue_len = 24,
        .idle_ms   = 0,            /* Fixo: não encerra */
        .on_msg    = bt_link_actor_fn,
        .on_start  = NULL,
        .on_stop   = NULL,
        .ctx       = NULL,
    };
    esp_err_t err = hs_actor_create(&cfg, &s_bt_actor);
    ESP_RETURN_ON_ERROR(err, TAG, "falha ao criar actor bt_link");

    /* 4. Registro de callbacks do GAP */
    const bt_gap_cbs_t cbs = {
        .on_ssp_confirm   = on_ssp_confirm,
        .on_auth_complete = on_auth_complete,
        .on_acl           = on_acl,
        .on_remote_name   = on_remote_name,
    };
    bt_gap_register_cbs(&cbs);

    /* 5. Registro de eventos do barramento central */
    hs_event_register(SENSOR_EVT, SENSOR_EVT_WORN, on_bus_event, NULL);
    hs_event_register(SENSOR_EVT, SENSOR_EVT_REMOVED, on_bus_event, NULL);
    hs_event_register(SENSOR_EVT, SENSOR_EVT_BUTTON_SHORT, on_bus_event, NULL);
    hs_event_register(SENSOR_EVT, SENSOR_EVT_BUTTON_LONG, on_bus_event, NULL);
    hs_event_register(SENSOR_EVT, SENSOR_EVT_BATTERY, on_bus_event, NULL);

    hs_event_register(BT_EVT, BT_EVT_LINK_UP, on_bus_event, NULL);
    hs_event_register(BT_EVT, BT_EVT_LINK_DOWN, on_bus_event, NULL);
    hs_event_register(BT_EVT, BT_EVT_STREAMING, on_bus_event, NULL);

    s_ctx.worn = apds9930_is_worn();

    /* 6. Aplica o estado inicial e notifica o barramento */
    apply_state();
    post_slot_changed();

    ESP_LOGI(TAG, "Actor bt_link pronto no Core 0 (prioridade 6, fone %s)", s_ctx.worn ? "colocado" : "retirado");
    return ESP_OK;
}

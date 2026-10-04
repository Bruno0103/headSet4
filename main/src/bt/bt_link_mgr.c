#include "bt_link_mgr.h"

#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

#include "bt_a2dp.h"
#include "bt_ble.h"
#include "bt_fastpair.h"
#include "bt_gap.h"
#include "bt_hfp.h"
#include "headset_events.h"

static const char *TAG = "bt_link";

#define NVS_NS              "lnkmgr"
#define NVS_KEY_SLOTS       "slots"
#define NVS_KEY_SEL         "sel"

#define PAIRING_WINDOW_MS   120000
#define COMPLETE_DELAY_MS   2500      /* espera o celular abrir o 2o perfil antes de conectarmos */
#define VOLUME_SAVE_MS      5000
#define DEFAULT_VOLUME      64
#define MAX_CONNECT_TRIES   12

static const uint32_t k_backoff_ms[] = { 2000, 3000, 5000, 8000, 13000, 20000, 30000 };

typedef struct {
    uint8_t bda[ESP_BD_ADDR_LEN];
    uint8_t valid;
    uint8_t volume;
    uint8_t acct_ref;
} slot_nv_t;

static slot_nv_t        s_slot[BT_LINK_NUM_SLOTS];
static bt_link_state_t  s_state[BT_LINK_NUM_SLOTS];
static int              s_sel;

static SemaphoreHandle_t s_mtx;
static esp_timer_handle_t s_connect_tmr, s_complete_tmr, s_pair_tmr, s_vol_tmr;

static bool          s_worn = true;
static volatile bool s_pairing;
static int           s_attempts;

/* Unico link Classic aceito (do slot selecionado) */
static struct {
    uint8_t bda[ESP_BD_ADDR_LEN];
    uint8_t profiles;          /* headset_profile_t */
    bool    complete_tried;
} s_link;

#define LOCK()   xSemaphoreTakeRecursive(s_mtx, portMAX_DELAY)
#define UNLOCK() xSemaphoreGiveRecursive(s_mtx)

/* ---------------- NVS ---------------- */

static void nvs_load(void)
{
    nvs_handle_t h;
    memset(s_slot, 0, sizeof s_slot);
    for (int i = 0; i < BT_LINK_NUM_SLOTS; i++) {
        s_slot[i].volume = DEFAULT_VOLUME;
    }
    s_sel = 0;

    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "Sem slots salvos (primeiro boot)");
        return;
    }
    size_t len = sizeof s_slot;
    slot_nv_t tmp[BT_LINK_NUM_SLOTS];
    if (nvs_get_blob(h, NVS_KEY_SLOTS, tmp, &len) == ESP_OK && len == sizeof tmp) {
        memcpy(s_slot, tmp, sizeof s_slot);
    }
    uint8_t sel = 0;
    if (nvs_get_u8(h, NVS_KEY_SEL, &sel) == ESP_OK && sel < BT_LINK_NUM_SLOTS) {
        s_sel = sel;
    }
    nvs_close(h);
}

static void nvs_save(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open: %s", esp_err_to_name(err));
        return;
    }
    err = nvs_set_blob(h, NVS_KEY_SLOTS, s_slot, sizeof s_slot);
    if (err == ESP_OK) {
        err = nvs_set_u8(h, NVS_KEY_SEL, (uint8_t)s_sel);
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao gravar slots no NVS: %s", esp_err_to_name(err));
    }
    nvs_close(h);
}

/* ---------------- helpers ---------------- */

static bool sel_valid(void)
{
    return s_slot[s_sel].valid;
}

static bool is_sel_bda(const uint8_t *bda)
{
    return sel_valid() && memcmp(s_slot[s_sel].bda, bda, ESP_BD_ADDR_LEN) == 0;
}

static void drop_link(void)
{
    if (!s_link.profiles) {
        return;
    }
    ESP_LOGI(TAG, "Desconectando " ESP_BD_ADDR_STR " (perfis 0x%X)", ESP_BD_ADDR_HEX(s_link.bda), s_link.profiles);
    if (s_link.profiles & HEADSET_PROFILE_HFP) {
        bt_hfp_disconnect(s_link.bda);
    }
    if (s_link.profiles & HEADSET_PROFILE_A2DP) {
        bt_a2dp_disconnect(s_link.bda);
    }
}

static void stop_connect_timer(void)
{
    esp_timer_stop(s_connect_tmr);
    esp_timer_stop(s_complete_tmr);
}

static void schedule_connect(void)
{
    if (esp_timer_is_active(s_connect_tmr)) {
        return;
    }
    size_t n = sizeof k_backoff_ms / sizeof k_backoff_ms[0];
    uint32_t ms = k_backoff_ms[s_attempts < (int)n ? s_attempts : (int)n - 1];
    ESP_LOGI(TAG, "Reconexao com slot %d em %u ms (tentativa %d/%d)", s_sel, (unsigned)ms, s_attempts + 1,
             MAX_CONNECT_TRIES);
    esp_timer_start_once(s_connect_tmr, (uint64_t)ms * 1000);
}

static const char *state_str(bt_link_state_t s)
{
    return s == BT_LINK_ACTIVE ? "ACTIVE" : "SLEEP";
}

static void set_states(void)
{
    for (int i = 0; i < BT_LINK_NUM_SLOTS; i++) {
        bt_link_state_t ns = (i == s_sel && (s_worn || s_pairing)) ? BT_LINK_ACTIVE : BT_LINK_SLEEP;
        if (ns != s_state[i]) {
            ESP_LOGI(TAG, "Slot %d: %s -> %s", i, state_str(s_state[i]), state_str(ns));
            s_state[i] = ns;
        }
    }
}

static void start_pairing(void);

/* Reaplica a politica inteira (idempotente); chamado a cada evento relevante. */
static void apply_state(void)
{
    set_states();

    if (s_pairing) {
        stop_connect_timer();
        bt_gap_set_scan(true, true);
        bt_ble_set_profile(BT_BLE_ADV_PAIRING);
        return;
    }

    if (!s_worn) {
        /* SLEEP: Classic fora do ar, so BLE lento aguardando o evento de proximidade */
        stop_connect_timer();
        drop_link();
        bt_gap_set_scan(false, false);
        bt_ble_set_profile(BT_BLE_ADV_SLEEP);
        return;
    }

    /* ACTIVE */
    bt_ble_set_profile(BT_BLE_ADV_ACTIVE);
    if (!sel_valid()) {
        ESP_LOGI(TAG, "Slot %d vazio: abrindo janela de pareamento", s_sel);
        start_pairing();
        return;
    }
    bt_gap_set_scan(true, false);

    if (s_link.profiles && !is_sel_bda(s_link.bda)) {
        drop_link();       /* troca de slot: o LINK_DOWN volta aqui para conectar o novo */
        return;
    }
    if (!s_link.profiles) {
        schedule_connect();
    }
}

/* ---------------- pareamento ---------------- */

static void stop_pairing(const char *why)
{
    if (!s_pairing) {
        return;
    }
    ESP_LOGI(TAG, "Janela de pareamento encerrada (%s)", why);
    s_pairing = false;
    esp_timer_stop(s_pair_tmr);
    headset_pairing_evt_t ev = { .active = false };
    headset_event_post(HEADSET_EVT_PAIRING_MODE, &ev, sizeof ev);
    bt_fastpair_on_pairing_mode(false);
    apply_state();
}

static void start_pairing(void)
{
    if (s_pairing) {
        return;
    }
    ESP_LOGI(TAG, "Janela de pareamento aberta para o slot %d (%d s)", s_sel, PAIRING_WINDOW_MS / 1000);
    s_pairing = true;
    s_attempts = 0;
    drop_link();
    esp_timer_start_once(s_pair_tmr, (uint64_t)PAIRING_WINDOW_MS * 1000);
    headset_pairing_evt_t ev = { .active = true };
    headset_event_post(HEADSET_EVT_PAIRING_MODE, &ev, sizeof ev);
    bt_fastpair_on_pairing_mode(true);
    apply_state();
}

/* ---------------- timers ---------------- */

static void connect_timer_cb(void *arg)
{
    LOCK();
    if (s_worn && !s_pairing && sel_valid() && !s_link.profiles) {
        if (++s_attempts > MAX_CONNECT_TRIES) {
            ESP_LOGW(TAG, "Slot %d nao respondeu apos %d tentativas; aguardando conexao do celular", s_sel,
                     MAX_CONNECT_TRIES);
        } else {
            ESP_LOGI(TAG, "Conectando A2DP em " ESP_BD_ADDR_STR, ESP_BD_ADDR_HEX(s_slot[s_sel].bda));
            if (bt_a2dp_connect(s_slot[s_sel].bda) != ESP_OK) {
                ESP_LOGW(TAG, "bt_a2dp_connect recusado pela pilha");
            }
            schedule_connect();
        }
    }
    UNLOCK();
}

static void complete_timer_cb(void *arg)
{
    LOCK();
    if (s_link.profiles && !s_link.complete_tried) {
        s_link.complete_tried = true;
        if (!(s_link.profiles & HEADSET_PROFILE_A2DP)) {
            bt_a2dp_connect(s_link.bda);
        } else if (!(s_link.profiles & HEADSET_PROFILE_HFP)) {
            bt_hfp_connect(s_link.bda);
        }
    }
    UNLOCK();
}

static void pair_timer_cb(void *arg)
{
    LOCK();
    stop_pairing("timeout");
    UNLOCK();
}

static void volume_timer_cb(void *arg)
{
    LOCK();
    nvs_save();
    UNLOCK();
}

/* ---------------- eventos do HEADSET_EVENT ---------------- */

static void on_headset_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    LOCK();
    switch (id) {
    case HEADSET_EVT_WORN:
        ESP_LOGI(TAG, "Fone colocado");
        s_worn = true;
        s_attempts = 0;
        apply_state();
        break;

    case HEADSET_EVT_REMOVED:
        ESP_LOGI(TAG, "Fone retirado");
        s_worn = false;
        apply_state();
        break;

    case HEADSET_EVT_BUTTON_SWITCH:
        if (s_pairing) {
            stop_pairing("botao");
            break;
        }
        s_sel = (s_sel + 1) % BT_LINK_NUM_SLOTS;
        s_attempts = 0;
        nvs_save();
        ESP_LOGI(TAG, "Slot selecionado: %d (%s)", s_sel, sel_valid() ? "pareado" : "vazio");
        stop_connect_timer();
        apply_state();
        break;

    case HEADSET_EVT_BUTTON_PAIRING:
        if (s_pairing) {
            stop_pairing("botao");
        } else {
            start_pairing();
        }
        break;

    case HEADSET_EVT_LINK_UP: {
        const headset_link_evt_t *ev = data;
        if (!is_sel_bda(ev->bda)) {
            ESP_LOGW(TAG, "Perfil 0x%X de " ESP_BD_ADDR_STR " recusado (nao e o slot selecionado)", ev->profile,
                     ESP_BD_ADDR_HEX(ev->bda));
            if (ev->profile & HEADSET_PROFILE_HFP) {
                bt_hfp_disconnect((uint8_t *)ev->bda);
            }
            if (ev->profile & HEADSET_PROFILE_A2DP) {
                bt_a2dp_disconnect((uint8_t *)ev->bda);
            }
            break;
        }
        if (!s_link.profiles) {
            memcpy(s_link.bda, ev->bda, ESP_BD_ADDR_LEN);
            s_link.complete_tried = false;
        }
        s_link.profiles |= ev->profile;
        s_attempts = 0;
        esp_timer_stop(s_connect_tmr);
        ESP_LOGI(TAG, "Link ativo com " ESP_BD_ADDR_STR " (perfis 0x%X)", ESP_BD_ADDR_HEX(s_link.bda), s_link.profiles);
        if (s_link.profiles != (HEADSET_PROFILE_A2DP | HEADSET_PROFILE_HFP) && !s_link.complete_tried) {
            esp_timer_stop(s_complete_tmr);
            esp_timer_start_once(s_complete_tmr, (uint64_t)COMPLETE_DELAY_MS * 1000);
        }
        break;
    }

    case HEADSET_EVT_LINK_DOWN: {
        const headset_link_evt_t *ev = data;
        if (s_link.profiles && memcmp(s_link.bda, ev->bda, ESP_BD_ADDR_LEN) == 0) {
            s_link.profiles &= ~ev->profile;
            ESP_LOGI(TAG, "Perfil 0x%X caiu (restam 0x%X)", ev->profile, s_link.profiles);
            if (!s_link.profiles) {
                esp_timer_stop(s_complete_tmr);
                apply_state();
            }
        }
        break;
    }

    case HEADSET_EVT_STREAMING: {
        const headset_streaming_evt_t *ev = data;
        bt_ble_set_streaming(ev->streaming);
        break;
    }

    default:
        break;
    }
    UNLOCK();
}

/* ---------------- callbacks do GAP (politica de SSP) ---------------- */

static bt_gap_cfm_decision_t on_ssp_confirm(const uint8_t *bda, uint32_t passkey)
{
    if (bt_fastpair_session_active()) {
        return bt_fastpair_ssp_confirm(bda, passkey);   /* compara com o passkey enviado pelo Seeker via GATT */
    }
    if (s_pairing) {
        ESP_LOGI(TAG, "Pareamento manual: aceitando " ESP_BD_ADDR_STR, ESP_BD_ADDR_HEX(bda));
        return BT_GAP_CFM_ACCEPT;
    }
    ESP_LOGW(TAG, "SSP de " ESP_BD_ADDR_STR " recusado: fora da janela de pareamento", ESP_BD_ADDR_HEX(bda));
    return BT_GAP_CFM_REJECT;
}

static void on_auth_complete(const uint8_t *bda, bool success)
{
    LOCK();
    if (success && s_pairing) {
        if (sel_valid() && memcmp(s_slot[s_sel].bda, bda, ESP_BD_ADDR_LEN) != 0) {
            ESP_LOGI(TAG, "Slot %d substituido; removendo bond anterior", s_sel);
            bt_gap_remove_bond(s_slot[s_sel].bda);
        }
        for (int i = 0; i < BT_LINK_NUM_SLOTS; i++) {   /* o mesmo aparelho nao ocupa dois slots */
            if (i != s_sel && s_slot[i].valid && memcmp(s_slot[i].bda, bda, ESP_BD_ADDR_LEN) == 0) {
                memset(&s_slot[i], 0, sizeof s_slot[i]);
                s_slot[i].volume = DEFAULT_VOLUME;
            }
        }
        memcpy(s_slot[s_sel].bda, bda, ESP_BD_ADDR_LEN);
        s_slot[s_sel].valid = 1;
        s_slot[s_sel].acct_ref = 0;
        nvs_save();
        ESP_LOGI(TAG, "Slot %d = " ESP_BD_ADDR_STR, s_sel, ESP_BD_ADDR_HEX(bda));
        s_attempts = 0;
        stop_pairing("pareado");
    }
    UNLOCK();
}

static void on_acl(const uint8_t *bda, bool connected, uint8_t reason)
{
    ESP_LOGD(TAG, "ACL " ESP_BD_ADDR_STR " %s (0x%02X)", ESP_BD_ADDR_HEX(bda), connected ? "up" : "down", reason);
}

/* ---------------- API ---------------- */

bool bt_link_mgr_pairing_active(void)
{
    return s_pairing;
}

int bt_link_mgr_selected(void)
{
    return s_sel;
}

bt_link_state_t bt_link_mgr_state(int slot)
{
    return (slot >= 0 && slot < BT_LINK_NUM_SLOTS) ? s_state[slot] : BT_LINK_SLEEP;
}

void bt_link_mgr_set_account_ref(uint8_t ref)
{
    LOCK();
    s_slot[s_sel].acct_ref = ref;
    nvs_save();
    UNLOCK();
}

uint8_t bt_link_mgr_get_volume(void)
{
    return s_slot[s_sel].volume;
}

void bt_link_mgr_set_volume(uint8_t volume)
{
    LOCK();
    if (volume > 127) {
        volume = 127;
    }
    if (s_slot[s_sel].volume != volume) {
        s_slot[s_sel].volume = volume;
        esp_timer_stop(s_vol_tmr);
        esp_timer_start_once(s_vol_tmr, (uint64_t)VOLUME_SAVE_MS * 1000);
    }
    UNLOCK();
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
    s_mtx = xSemaphoreCreateRecursiveMutex();
    ESP_RETURN_ON_FALSE(s_mtx, ESP_ERR_NO_MEM, TAG, "mutex");

    s_connect_tmr  = make_timer("lm_conn", connect_timer_cb);
    s_complete_tmr = make_timer("lm_compl", complete_timer_cb);
    s_pair_tmr     = make_timer("lm_pair", pair_timer_cb);
    s_vol_tmr      = make_timer("lm_vol", volume_timer_cb);
    ESP_RETURN_ON_FALSE(s_connect_tmr && s_complete_tmr && s_pair_tmr && s_vol_tmr, ESP_ERR_NO_MEM, TAG, "timers");

    nvs_load();
    for (int i = 0; i < BT_LINK_NUM_SLOTS; i++) {
        if (s_slot[i].valid) {
            ESP_LOGI(TAG, "Slot %d: " ESP_BD_ADDR_STR " (volume %u)%s", i, ESP_BD_ADDR_HEX(s_slot[i].bda),
                     s_slot[i].volume, i == s_sel ? " [selecionado]" : "");
        } else {
            ESP_LOGI(TAG, "Slot %d: vazio%s", i, i == s_sel ? " [selecionado]" : "");
        }
    }

    const bt_gap_cbs_t cbs = {
        .on_ssp_confirm   = on_ssp_confirm,
        .on_auth_complete = on_auth_complete,
        .on_acl           = on_acl,
    };
    bt_gap_register_cbs(&cbs);

    static const headset_event_id_t ids[] = {
        HEADSET_EVT_WORN, HEADSET_EVT_REMOVED, HEADSET_EVT_BUTTON_SWITCH, HEADSET_EVT_BUTTON_PAIRING,
        HEADSET_EVT_LINK_UP, HEADSET_EVT_LINK_DOWN, HEADSET_EVT_STREAMING,
    };
    for (size_t i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        ESP_RETURN_ON_ERROR(headset_event_register(ids[i], on_headset_event, NULL), TAG, "assinatura de evento");
    }

    bool worn;
    if (headset_events_get_worn(&worn)) {
        s_worn = worn;
    }

    LOCK();
    apply_state();
    UNLOCK();
    ESP_LOGI(TAG, "Gerenciador de links pronto (fone %s)", s_worn ? "colocado" : "retirado");
    return ESP_OK;
}

/*
 * GAP do headset. Organizacao do arquivo:
 *   1) estado e constantes
 *   2) conhecidos (bonds da pilha + ordem de uso salva na NVS)
 *   3) visibilidade
 *   4) reconexao automatica
 *   5) eventos de enlace (ACL)
 *   6) pareamento seguro (SSP)
 *   7) comandos e API publica
 *
 * Todo o estado e alterado na task BT_APP (callbacks e comandos sao despachados
 * para ela); o mutex recursivo existe so para as consultas de outras tasks.
 *
 * NAO TESTADO EM HARDWARE.
 */
#include "bt_gap.h"

#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_gap_bt_api.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

#include "bt_app_core_utils.h"
#include "bredr_app_common_utils.h"

/* ------------------------------------------------------------------ */
/* 1) estado e constantes                                              */
/* ------------------------------------------------------------------ */

#define CONNECT_TIMEOUT_MS  8000      /* tempo por tentativa (page timeout padrao ~5 s) */
#define BOOT_ROUNDS         2         /* rodadas de reconexao ao ligar */
#define LINKLOSS_ROUNDS     8         /* rodadas apos perda de sinal */

#define NVS_NS              "bt_gap"
#define NVS_KEY_MRU         "mru"

/* Motivos HCI de desconexao em que NAO devemos reconectar (foi proposital) */
#define HCI_REMOTE_USER_TERM  0x13
#define HCI_REMOTE_POWER_OFF  0x15
#define HCI_LOCAL_HOST_TERM   0x16

static const uint8_t BACKOFF_S[] = { 2, 5, 10, 20, 30 };   /* espera entre rodadas */

enum { CMD_RETRY_TICK, CMD_FORGET, CMD_FORGET_ALL, CMD_PAIR_NEW, CMD_CONFIRM, CMD_SWITCH_TICK };

static const char *TAG = "bt_gap";

static bt_gap_config_t   s_cfg;
static SemaphoreHandle_t s_lock;
static esp_timer_handle_t s_timer;

static volatile bt_gap_state_t s_state = BT_GAP_PAIRABLE;

static esp_bd_addr_t s_active;            /* dispositivo com enlace ativo (conectado ou pareando) */
static bool          s_has_active;

static esp_bd_addr_t s_mru[BT_GAP_MAX_KNOWN];   /* conhecidos, [0] = mais recente */
static size_t        s_mru_n;

static struct { bool on; size_t idx; uint8_t round, max_rounds; } s_rc;

/* Troca em andamento: 'old' esta sendo derrubado, 'nu' deve assumir os perfis */
static struct { bool on; esp_bd_addr_t old, nu; } s_sw;
static esp_timer_handle_t s_sw_timer;
#define SWITCH_TIMEOUT_MS   2500   /* espera maxima pelo antigo cair */
#define SWITCH_SETTLE_MS    300    /* folga depois que o antigo caiu */

static bool         s_confirm_pending;
static esp_bd_addr_t s_confirm_bda;

#define LOCK()    xSemaphoreTakeRecursive(s_lock, portMAX_DELAY)
#define UNLOCK()  xSemaphoreGiveRecursive(s_lock)

static bool same(const uint8_t *a, const uint8_t *b) { return memcmp(a, b, ESP_BD_ADDR_LEN) == 0; }

/* ------------------------------------------------------------------ */
/* 2) conhecidos                                                       */
/* ------------------------------------------------------------------ */

/* Lista de bonds da pilha. Retorna a quantidade (-1 = erro); quem chama faz free(*out). */
static int bonds_load(esp_bd_addr_t **out)
{
    *out = NULL;
    int n = esp_bt_gap_get_bond_device_num();
    if (n <= 0) return 0;
    esp_bd_addr_t *list = malloc(n * sizeof *list);
    if (!list) return -1;
    if (esp_bt_gap_get_bond_device_list(&n, list) != ESP_OK) { free(list); return -1; }
    *out = list;
    return n;
}

static bool addr_in(const esp_bd_addr_t *list, int n, const uint8_t *bda)
{
    for (int i = 0; i < n; i++) if (same(list[i], bda)) return true;
    return false;
}

static bool is_bonded(const uint8_t *bda)
{
    esp_bd_addr_t *list;
    int n = bonds_load(&list);
    bool found = n > 0 && addr_in(list, n, bda);
    free(list);
    return found;
}

static int mru_find(const uint8_t *bda)
{
    for (size_t i = 0; i < s_mru_n; i++) if (same(s_mru[i], bda)) return (int)i;
    return -1;
}

static void mru_remove_at(size_t i)
{
    memmove(s_mru[i], s_mru[i + 1], (s_mru_n - i - 1) * sizeof s_mru[0]);
    s_mru_n--;
}

static void mru_save(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, NVS_KEY_MRU, s_mru, s_mru_n * sizeof s_mru[0]);
    nvs_commit(h);
    nvs_close(h);
}

static void mru_load(void)
{
    s_mru_n = 0;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    size_t len = sizeof s_mru;
    if (nvs_get_blob(h, NVS_KEY_MRU, s_mru, &len) == ESP_OK) s_mru_n = len / sizeof s_mru[0];
    nvs_close(h);
}

/* Marca como o mais recente. Lista cheia: o mais antigo perde o pareamento. */
static void mru_touch(const uint8_t *bda)
{
    int i = mru_find(bda);
    if (i == 0) return;
    if (i > 0) {
        mru_remove_at(i);
    } else if (s_mru_n == BT_GAP_MAX_KNOWN) {
        ESP_LOGW(TAG, "lista cheia: esquecendo " ESP_BD_ADDR_STR, ESP_BD_ADDR_HEX(s_mru[s_mru_n - 1]));
        esp_bt_gap_remove_bond_device(s_mru[s_mru_n - 1]);
        s_mru_n--;
    }
    memmove(s_mru[1], s_mru[0], s_mru_n * sizeof s_mru[0]);
    memcpy(s_mru[0], bda, ESP_BD_ADDR_LEN);
    s_mru_n++;
    mru_save();
}

/* Reconcilia a ordem salva com os bonds reais da pilha (ex.: apos reflash/apagar NVS) */
static void mru_sync_with_bonds(void)
{
    mru_load();

    esp_bd_addr_t *bonds;
    int n = bonds_load(&bonds);
    if (n < 0) { ESP_LOGE(TAG, "sem memoria para listar bonds"); return; }

    for (size_t i = s_mru_n; i-- > 0;) {
        if (!addr_in(bonds, n, s_mru[i])) mru_remove_at(i);          /* bond sumiu */
    }
    for (int j = 0; j < n; j++) {
        if (mru_find(bonds[j]) >= 0) continue;
        if (s_mru_n < BT_GAP_MAX_KNOWN) memcpy(s_mru[s_mru_n++], bonds[j], ESP_BD_ADDR_LEN);
        else esp_bt_gap_remove_bond_device(bonds[j]);                /* acima do limite */
    }
    free(bonds);
    mru_save();
    ESP_LOGI(TAG, "%u dispositivo(s) conhecido(s)", (unsigned)s_mru_n);
}

/* ------------------------------------------------------------------ */
/* 3) visibilidade                                                     */
/* ------------------------------------------------------------------ */

static void apply_visibility(void)
{
    /* Sempre conectavel; so fica visivel quando nao ha conexao */
    esp_bt_discovery_mode_t disc = (s_state == BT_GAP_CONNECTED) ? ESP_BT_NON_DISCOVERABLE
                                                                 : ESP_BT_GENERAL_DISCOVERABLE;
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, disc);
}

static void set_state(bt_gap_state_t st)
{
    if (st == s_state) return;
    static const char *const NAMES[] = { "PAREAVEL", "RECONECTANDO", "CONECTADO" };
    ESP_LOGI(TAG, "estado: %s -> %s", NAMES[s_state], NAMES[st]);
    s_state = st;
    apply_visibility();
    if (s_cfg.state_cb) s_cfg.state_cb(st);
}

/* ------------------------------------------------------------------ */
/* 4) reconexao automatica                                             */
/* ------------------------------------------------------------------ */

static void rc_arm(uint32_t ms)
{
    esp_timer_stop(s_timer);
    esp_timer_start_once(s_timer, (uint64_t)ms * 1000);
}

static void rc_stop(void)
{
    s_rc.on = false;
    esp_timer_stop(s_timer);
}

/* Uma tentativa por chamada; o timer chama de novo (timeout da tentativa ou espera da rodada). */
static void rc_step(void)
{
    if (!s_rc.on) return;
    if (s_has_active) { rc_stop(); return; }

    if (s_rc.idx >= s_mru_n) {                                  /* fim da rodada */
        if (++s_rc.round >= s_rc.max_rounds) {
            ESP_LOGW(TAG, "reconexao desistiu; aguardando um aparelho");
            rc_stop();
            set_state(BT_GAP_PAIRABLE);
            return;
        }
        size_t b = s_rc.round - 1;
        if (b >= sizeof BACKOFF_S) b = sizeof BACKOFF_S - 1;
        s_rc.idx = 0;
        rc_arm(BACKOFF_S[b] * 1000u);
        return;
    }

    uint8_t *bda = s_mru[s_rc.idx++];
    ESP_LOGI(TAG, "reconectando a " ESP_BD_ADDR_STR " (rodada %u)", ESP_BD_ADDR_HEX(bda), s_rc.round + 1);
    esp_err_t err = s_cfg.connect(bda);
    if (err != ESP_OK) ESP_LOGW(TAG, "connect: %s", esp_err_to_name(err));
    rc_arm(CONNECT_TIMEOUT_MS);
}

static void rc_begin(uint8_t rounds)
{
    if (s_mru_n == 0 || s_has_active) { set_state(BT_GAP_PAIRABLE); return; }
    s_rc.on = true;
    s_rc.idx = 0;
    s_rc.round = 0;
    s_rc.max_rounds = rounds;
    set_state(BT_GAP_RECONNECTING);
    rc_step();
}

/* ------------------------------------------------------------------ */
/* 5) eventos de enlace (ACL)                                          */
/* ------------------------------------------------------------------ */

/* Troca: derruba o antigo, espera ele sair e so entao abre os perfis com o novo.
 * (a pilha so aceita um aparelho por vez; se o novo tentar antes, e rejeitado) */
static void sw_begin(uint8_t *old, uint8_t *nu)
{
    s_sw.on = true;
    memcpy(s_sw.old, old, ESP_BD_ADDR_LEN);
    memcpy(s_sw.nu, nu, ESP_BD_ADDR_LEN);
    esp_timer_stop(s_sw_timer);
    esp_timer_start_once(s_sw_timer, SWITCH_TIMEOUT_MS * 1000ULL);
    s_cfg.disconnect(old);
}

static void sw_cancel(void)
{
    s_sw.on = false;
    esp_timer_stop(s_sw_timer);
}

static void sw_finish(void)
{
    if (!s_sw.on) return;
    s_sw.on = false;
    if (s_has_active && s_state == BT_GAP_CONNECTED && same(s_active, s_sw.nu)) {
        ESP_LOGI(TAG, "troca: abrindo perfis com " ESP_BD_ADDR_STR, ESP_BD_ADDR_HEX(s_sw.nu));
        s_cfg.connect(s_sw.nu);
    }
}

static bool should_reconnect(uint8_t reason)
{
    return reason != HCI_REMOTE_USER_TERM && reason != HCI_REMOTE_POWER_OFF &&
           reason != HCI_LOCAL_HOST_TERM;
}

static void take_active(uint8_t *bda)
{
    memcpy(s_active, bda, ESP_BD_ADDR_LEN);
    s_has_active = true;
    rc_stop();
    if (is_bonded(bda)) {
        mru_touch(bda);
        set_state(BT_GAP_CONNECTED);
    } else {
        ESP_LOGI(TAG, "pareamento em andamento com " ESP_BD_ADDR_STR, ESP_BD_ADDR_HEX(bda));
    }
}

static void on_acl_connected(uint8_t *bda)
{
    if (s_has_active && same(bda, s_active)) return;

    if (!s_has_active) {
        take_active(bda);
    } else if (s_state == BT_GAP_CONNECTED && is_bonded(bda)) {
        /* Troca pedida por um conhecido: ele assume, o anterior e desconectado */
        esp_bd_addr_t old;
        memcpy(old, s_active, sizeof old);
        ESP_LOGI(TAG, "troca: " ESP_BD_ADDR_STR " assume", ESP_BD_ADDR_HEX(bda));
        take_active(bda);
        sw_begin(old, bda);
    } else {
        ESP_LOGW(TAG, "enlace de " ESP_BD_ADDR_STR " ignorado (ocupado/desconhecido)", ESP_BD_ADDR_HEX(bda));
    }
}

static void on_acl_disconnected(uint8_t *bda, uint8_t reason)
{
    if (!s_has_active || !same(bda, s_active)) {          /* aparelho trocado ou recusado */
        ESP_LOGI(TAG, ESP_BD_ADDR_STR " desconectou (inativo)", ESP_BD_ADDR_HEX(bda));
        if (s_sw.on && same(bda, s_sw.old)) {             /* antigo caiu: o novo pode assumir */
            esp_timer_stop(s_sw_timer);
            esp_timer_start_once(s_sw_timer, SWITCH_SETTLE_MS * 1000ULL);
        }
        return;
    }
    bool was_connected = (s_state == BT_GAP_CONNECTED);
    s_has_active = false;
    sw_cancel();                                          /* o novo caiu antes de assumir */
    if (s_confirm_pending && same(bda, s_confirm_bda)) s_confirm_pending = false;
    ESP_LOGI(TAG, ESP_BD_ADDR_STR " desconectou (motivo 0x%02X)", ESP_BD_ADDR_HEX(bda), reason);

    if (!was_connected) return;                          /* pareamento que nao concluiu */

    if (should_reconnect(reason)) rc_begin(LINKLOSS_ROUNDS);
    else                          set_state(BT_GAP_PAIRABLE);
}

/* ------------------------------------------------------------------ */
/* 6) pareamento seguro (SSP)                                          */
/* ------------------------------------------------------------------ */

/* Janela de pareamento: so sem conexao ativa, e so com o aparelho que esta pareando */
static bool pairing_allowed(const uint8_t *bda)
{
    return s_state != BT_GAP_CONNECTED && (!s_has_active || same(bda, s_active));
}

static void on_cfm_req(uint8_t *bda, uint32_t passkey)
{
    if (!pairing_allowed(bda)) {
        ESP_LOGW(TAG, "pareamento recusado de " ESP_BD_ADDR_STR, ESP_BD_ADDR_HEX(bda));
        esp_bt_gap_ssp_confirm_reply(bda, false);
        return;
    }
    if (s_cfg.confirm_cb) {
        s_confirm_pending = true;
        memcpy(s_confirm_bda, bda, sizeof s_confirm_bda);
        s_cfg.confirm_cb(bda, passkey);
    } else {
        esp_bt_gap_ssp_confirm_reply(bda, true);
    }
}

static void on_auth_cmpl(esp_bt_gap_cb_param_t *p)
{
    uint8_t *bda = p->auth_cmpl.bda;
    if (s_confirm_pending && same(bda, s_confirm_bda)) s_confirm_pending = false;

    if (p->auth_cmpl.stat != ESP_BT_STATUS_SUCCESS) {
        ESP_LOGW(TAG, "autenticacao falhou (%d) com " ESP_BD_ADDR_STR, p->auth_cmpl.stat, ESP_BD_ADDR_HEX(bda));
        return;
    }
    ESP_LOGI(TAG, "pareado: %s", (char *)p->auth_cmpl.device_name);
    mru_touch(bda);
    if (s_has_active && same(bda, s_active)) {
        rc_stop();
        set_state(BT_GAP_CONNECTED);
    }
}

/* ------------------------------------------------------------------ */
/* 7) despacho, comandos e API                                         */
/* ------------------------------------------------------------------ */

/* Roda na task BT_APP */
static void gap_evt_hdl(uint16_t ev, void *p)
{
    esp_bt_gap_cb_param_t *prm = p;
    LOCK();
    switch (ev) {
    case ESP_BT_GAP_ACL_CONN_CMPL_STAT_EVT:
        if (prm->acl_conn_cmpl_stat.stat == ESP_BT_STATUS_SUCCESS) on_acl_connected(prm->acl_conn_cmpl_stat.bda);
        else ESP_LOGW(TAG, "falha ao abrir enlace (stat %d)", prm->acl_conn_cmpl_stat.stat);
        break;
    case ESP_BT_GAP_ACL_DISCONN_CMPL_STAT_EVT:
        on_acl_disconnected(prm->acl_disconn_cmpl_stat.bda, prm->acl_disconn_cmpl_stat.reason);
        break;
    case ESP_BT_GAP_CFM_REQ_EVT:
        on_cfm_req(prm->cfm_req.bda, prm->cfm_req.num_val);
        break;
    case ESP_BT_GAP_AUTH_CMPL_EVT:
        on_auth_cmpl(prm);
        break;
    case ESP_BT_GAP_KEY_REQ_EVT:                          /* passkey digitado aqui: nao suportado */
        esp_bt_gap_ssp_passkey_reply(prm->key_req.bda, false, 0);
        break;
    case ESP_BT_GAP_PIN_REQ_EVT:                          /* pareamento legado (PIN) = inseguro: recusa */
        ESP_LOGW(TAG, "PIN legado recusado");
        esp_bt_gap_pin_reply(prm->pin_req.bda, false, 0, NULL);
        break;
    default:
        break;
    }
    UNLOCK();
}

static void gap_cb(esp_bt_gap_cb_event_t ev, esp_bt_gap_cb_param_t *param)
{
    switch (ev) {
    case ESP_BT_GAP_ACL_CONN_CMPL_STAT_EVT:
    case ESP_BT_GAP_ACL_DISCONN_CMPL_STAT_EVT:
    case ESP_BT_GAP_CFM_REQ_EVT:
    case ESP_BT_GAP_AUTH_CMPL_EVT:
    case ESP_BT_GAP_KEY_REQ_EVT:
    case ESP_BT_GAP_PIN_REQ_EVT:
        bt_app_work_dispatch(gap_evt_hdl, ev, param, sizeof(esp_bt_gap_cb_param_t), NULL, NULL);
        break;
    default:
        bredr_app_gap_evt_def_hdl(ev, param);             /* o resto segue o padrao do exemplo */
        break;
    }
}

static void do_forget(uint8_t *bda)
{
    int i = mru_find(bda);
    if (i >= 0) { mru_remove_at(i); mru_save(); }
    esp_bt_gap_remove_bond_device(bda);
    if (s_has_active && same(bda, s_active)) s_cfg.disconnect(s_active);   /* motivo local: nao reconecta */
    ESP_LOGI(TAG, "esquecido: " ESP_BD_ADDR_STR, ESP_BD_ADDR_HEX(bda));
}

static void do_forget_all(void)
{
    rc_stop();
    esp_bd_addr_t *bonds;
    int n = bonds_load(&bonds);
    for (int i = 0; i < n; i++) esp_bt_gap_remove_bond_device(bonds[i]);
    free(bonds);
    s_mru_n = 0;
    mru_save();
    if (s_has_active) s_cfg.disconnect(s_active);
    else              set_state(BT_GAP_PAIRABLE);
    ESP_LOGI(TAG, "todos os pareamentos apagados");
}

static void do_pair_new(void)
{
    rc_stop();
    if (s_state == BT_GAP_CONNECTED && s_has_active) s_cfg.disconnect(s_active);   /* ACL cai -> PAIRABLE */
    else set_state(BT_GAP_PAIRABLE);
}

static void do_confirm(bool accept)
{
    if (!s_confirm_pending) return;
    s_confirm_pending = false;
    esp_bt_gap_ssp_confirm_reply(s_confirm_bda, accept && pairing_allowed(s_confirm_bda));
}

static void on_command(uint16_t cmd, void *p)
{
    LOCK();
    switch (cmd) {
    case CMD_RETRY_TICK:  rc_step();                      break;
    case CMD_SWITCH_TICK: sw_finish();                    break;
    case CMD_FORGET:     do_forget(p);                    break;
    case CMD_FORGET_ALL: do_forget_all();                 break;
    case CMD_PAIR_NEW:   do_pair_new();                   break;
    case CMD_CONFIRM:    do_confirm(*(bool *)p);          break;
    }
    UNLOCK();
}

/* timer -> task BT_APP */
static void timer_cb(void *arg)
{
    bt_app_work_dispatch(on_command, CMD_RETRY_TICK, NULL, 0, NULL, NULL);
}

static void sw_timer_cb(void *arg)
{
    bt_app_work_dispatch(on_command, CMD_SWITCH_TICK, NULL, 0, NULL, NULL);
}

void bt_gap_forget(esp_bd_addr_t bda)    { bt_app_work_dispatch(on_command, CMD_FORGET, bda, ESP_BD_ADDR_LEN, NULL, NULL); }
void bt_gap_forget_all(void)             { bt_app_work_dispatch(on_command, CMD_FORGET_ALL, NULL, 0, NULL, NULL); }
void bt_gap_pair_new(void)               { bt_app_work_dispatch(on_command, CMD_PAIR_NEW, NULL, 0, NULL, NULL); }
void bt_gap_confirm_pairing(bool accept) { bt_app_work_dispatch(on_command, CMD_CONFIRM, &accept, sizeof accept, NULL, NULL); }

bt_gap_state_t bt_gap_get_state(void) { return s_state; }

bool bt_gap_get_active(esp_bd_addr_t out)
{
    LOCK();
    bool ok = s_has_active && s_state == BT_GAP_CONNECTED;
    if (ok) memcpy(out, s_active, ESP_BD_ADDR_LEN);
    UNLOCK();
    return ok;
}

size_t bt_gap_get_known(esp_bd_addr_t *out, size_t max)
{
    LOCK();
    size_t n = s_mru_n < max ? s_mru_n : max;
    memcpy(out, s_mru, n * sizeof s_mru[0]);
    UNLOCK();
    return n;
}

esp_err_t bt_gap_start(const bt_gap_config_t *cfg)
{
    ESP_RETURN_ON_FALSE(cfg && cfg->connect && cfg->disconnect, ESP_ERR_INVALID_ARG, TAG, "config");
    s_cfg = *cfg;

    s_lock = xSemaphoreCreateRecursiveMutex();
    ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_NO_MEM, TAG, "mutex");
    const esp_timer_create_args_t targs = { .callback = timer_cb, .name = "bt_gap_rc" };
    ESP_RETURN_ON_ERROR(esp_timer_create(&targs, &s_timer), TAG, "timer");
    const esp_timer_create_args_t sw_args = { .callback = sw_timer_cb, .name = "bt_gap_sw" };
    ESP_RETURN_ON_ERROR(esp_timer_create(&sw_args, &s_sw_timer), TAG, "sw timer");

    if (cfg->device_name) esp_bt_gap_set_device_name(cfg->device_name);
    ESP_RETURN_ON_ERROR(esp_bt_gap_register_callback(gap_cb), TAG, "gap cb");

#if (CONFIG_EXAMPLE_SSP_ENABLED == true)
    /* SSP com DisplayYesNo: toda tentativa de pareamento gera CFM_REQ, que e onde
     * aplicamos a janela de pareamento (e a comparacao numerica, se houver confirm_cb). */
    esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_IO;
    ESP_RETURN_ON_ERROR(esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &iocap, sizeof iocap), TAG, "iocap");
#endif

    LOCK();
    mru_sync_with_bonds();
    rc_begin(BOOT_ROUNDS);
    apply_visibility();
    UNLOCK();

    ESP_LOGI(TAG, "GAP pronto");
    return ESP_OK;
}
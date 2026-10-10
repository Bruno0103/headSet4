#include "bt_fastpair.h"

#include <string.h>

#include "esp_bt_device.h"
#include "esp_check.h"
#include "esp_gatt_common_api.h"
#include "esp_gatts_api.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "settings.h"
#include "psa/crypto.h"
#include "sdkconfig.h"

#include "bt_a2dp.h"
#include "bt_ble.h"
#include "bt_link_mgr.h"

static const char *TAG = "bt_fp";

#define FP_APP_ID           0x46
#define FP_MAX_KEYS         5            /* o spec exige no minimo 5 chaves de conta */
#define FP_SESSION_MS       60000
#define FP_MTU              517
#define FP_MAX_WRITE        80           /* 16 (cifrado) + 64 (chave publica do Seeker) */

#define KBP_FLAG_INITIATE_BONDING  0x40  /* bit 1 numerado a partir do MSB */

/* UUID base FE2C12xx-8366-4814-8EB0-01DE32100BEA, little-endian, xx = byte baixo / alto */
#define FP_UUID128(lo, hi) { 0xEA, 0x0B, 0x10, 0x32, 0xDE, 0x01, 0xB0, 0x8E, 0x14, 0x48, 0x66, 0x83, lo, hi, 0x2C, 0xFE }

enum {
    IDX_SVC,
    IDX_MODEL_DECL, IDX_MODEL_VAL,
    IDX_KBP_DECL,   IDX_KBP_VAL,  IDX_KBP_CCC,
    IDX_PK_DECL,    IDX_PK_VAL,   IDX_PK_CCC,
    IDX_AK_DECL,    IDX_AK_VAL,
    IDX_NB,
};

typedef enum { JOB_KBP, JOB_PASSKEY, JOB_ACCOUNT_KEY } job_op_t;

typedef struct {
    job_op_t op;
    uint16_t len;
    uint8_t  data[FP_MAX_WRITE];
} fp_job_t;

static bool    s_enabled;
static uint8_t s_model_id[3];
static psa_key_id_t s_as_key;

static void ensure_crypto_task(void);

static SemaphoreHandle_t s_mtx;
static QueueHandle_t     s_jobs;
static esp_timer_handle_t s_sess_tmr;

static uint8_t s_keys[FP_MAX_KEYS][16];
static int     s_nkeys;

static struct {
    bool     active;
    bool     verified;            /* passkey do Seeker == passkey do SSP */
    uint8_t  key[16];
    uint8_t  peer_bda[6];
    bool     peer_valid;
    bool     seeker_pk_valid;
    uint32_t seeker_pk;
    bool     provider_pk_valid;
    uint32_t provider_pk;
} s_sess;

static esp_gatt_if_t s_gatts_if = ESP_GATT_IF_NONE;
static uint16_t s_handles[IDX_NB];
static uint16_t s_conn_id = 0xFFFF;
static bool     s_notify_kbp, s_notify_pk;
static volatile bool s_pairing;

/* escrita longa (prepare write) */
static uint8_t  s_prep[FP_MAX_WRITE];
static uint16_t s_prep_len, s_prep_handle;

#define LOCK()   xSemaphoreTake(s_mtx, portMAX_DELAY)
#define UNLOCK() xSemaphoreGive(s_mtx)

/* ======================= criptografia (PSA) ======================= */

/* AES-128-ECB de um bloco = AES-CBC com IV zero (PSA so expoe ECB de forma opcional). */
static bool aes128_block(const uint8_t key[16], const uint8_t in[16], uint8_t out[16], bool encrypt)
{
    psa_key_attributes_t at = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&at, PSA_KEY_TYPE_AES);
    psa_set_key_bits(&at, 128);
    psa_set_key_usage_flags(&at, encrypt ? PSA_KEY_USAGE_ENCRYPT : PSA_KEY_USAGE_DECRYPT);
    psa_set_key_algorithm(&at, PSA_ALG_CBC_NO_PADDING);

    psa_key_id_t id;
    if (psa_import_key(&at, key, 16, &id) != PSA_SUCCESS) {
        return false;
    }
    psa_cipher_operation_t op = PSA_CIPHER_OPERATION_INIT;
    const uint8_t iv[16] = { 0 };
    uint8_t tmp[32];
    size_t ol = 0, fl = 0;
    bool ok = false;

    psa_status_t st = encrypt ? psa_cipher_encrypt_setup(&op, id, PSA_ALG_CBC_NO_PADDING)
                              : psa_cipher_decrypt_setup(&op, id, PSA_ALG_CBC_NO_PADDING);
    if (st == PSA_SUCCESS && psa_cipher_set_iv(&op, iv, sizeof iv) == PSA_SUCCESS &&
        psa_cipher_update(&op, in, 16, tmp, sizeof tmp, &ol) == PSA_SUCCESS &&
        psa_cipher_finish(&op, tmp + ol, sizeof tmp - ol, &fl) == PSA_SUCCESS && ol + fl == 16) {
        memcpy(out, tmp, 16);
        ok = true;
    } else {
        psa_cipher_abort(&op);
    }
    psa_destroy_key(id);
    return ok;
}

static bool sha256(const uint8_t *in, size_t len, uint8_t out[32])
{
    size_t hl = 0;
    return psa_hash_compute(PSA_ALG_SHA_256, in, len, out, 32, &hl) == PSA_SUCCESS && hl == 32;
}

/* Chave de sessao = SHA-256(ECDH(anti-spoofing privada, publica do Seeker))[0..15] */
static bool derive_session_key(const uint8_t seeker_pub[64], uint8_t key[16])
{
    uint8_t peer[65] = { 0x04 };
    memcpy(peer + 1, seeker_pub, 64);

    uint8_t secret[32], hash[32];
    size_t sl = 0;
    psa_status_t st = psa_raw_key_agreement(PSA_ALG_ECDH, s_as_key, peer, sizeof peer, secret, sizeof secret, &sl);
    if (st != PSA_SUCCESS || sl != sizeof secret) {
        ESP_LOGW(TAG, "ECDH falhou (psa %d)", (int)st);
        return false;
    }
    bool ok = sha256(secret, sizeof secret, hash);
    if (ok) {
        memcpy(key, hash, 16);
    }
    memset(secret, 0, sizeof secret);
    memset(hash, 0, sizeof hash);
    return ok;
}

/* ======================= configuracao (Kconfig) ======================= */

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool parse_model_id(const char *s, uint8_t out[3])
{
    if (strncmp(s, "0x", 2) == 0) {
        s += 2;
    }
    if (strlen(s) != 6) {
        return false;
    }
    for (int i = 0; i < 3; i++) {
        int hi = hex_nibble(s[2 * i]), lo = hex_nibble(s[2 * i + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        out[i] = (uint8_t)(hi << 4 | lo);
    }
    return true;
}

static int b64_val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
}

static int b64_decode(const char *s, uint8_t *out, size_t max)
{
    size_t n = 0;
    uint32_t acc = 0;
    int bits = 0;
    for (; *s && *s != '='; s++) {
        int v = b64_val(*s);
        if (v < 0) {
            return -1;
        }
        acc = (acc << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (n >= max) {
                return -1;
            }
            out[n++] = (uint8_t)(acc >> bits);
        }
    }
    return (int)n;
}

static esp_err_t load_antispoof_key(void)
{
    uint8_t priv[32];
    int n = b64_decode(CONFIG_HEADSET_FASTPAIR_ANTISPOOF_PRIVATE_KEY_B64, priv, sizeof priv);
    ESP_RETURN_ON_FALSE(n == 32, ESP_ERR_INVALID_ARG, TAG, "chave Anti-Spoofing deve ter 32 bytes (base64), veio %d", n);

    psa_key_attributes_t at = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&at, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&at, 256);
    psa_set_key_usage_flags(&at, PSA_KEY_USAGE_DERIVE);
    psa_set_key_algorithm(&at, PSA_ALG_ECDH);
    psa_status_t st = psa_import_key(&at, priv, sizeof priv, &s_as_key);
    memset(priv, 0, sizeof priv);
    ESP_RETURN_ON_FALSE(st == PSA_SUCCESS, ESP_FAIL, TAG, "psa_import_key falhou (%d)", (int)st);
    return ESP_OK;
}

/* ======================= chaves de conta (Actor settings) ======================= */

static void keys_load(void)
{
    s_nkeys = 0;
    size_t len = sizeof(s_keys);
    esp_err_t err = settings_get_large_blob(SETTINGS_KEY_FP_KEYS, s_keys, &len);
    if (err == ESP_OK && len % 16 == 0) {
        s_nkeys = (int)(len / 16);
    }
    ESP_LOGI(TAG, "%d chave(s) de conta carregada(s) via actor settings", s_nkeys);
}

static void keys_save_locked(void)
{
    size_t len = (size_t)s_nkeys * 16;
    esp_err_t err = settings_set_large_blob(SETTINGS_KEY_FP_KEYS, s_nkeys ? s_keys : NULL, len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao gravar chaves de conta via actor settings: %s", esp_err_to_name(err));
    }
}

/* LRU: a chave mais recente fica no fim; ao lotar, a mais antiga (indice 0) sai. */
static void account_key_add(const uint8_t key[16])
{
    LOCK();
    int found = -1;
    for (int i = 0; i < s_nkeys; i++) {
        if (memcmp(s_keys[i], key, 16) == 0) {
            found = i;
        }
    }
    if (found >= 0) {
        memmove(s_keys[found], s_keys[found + 1], (size_t)(s_nkeys - found - 1) * 16);
        s_nkeys--;
    } else if (s_nkeys == FP_MAX_KEYS) {
        ESP_LOGW(TAG, "Lista de chaves cheia: removendo a menos recente");
        memmove(s_keys[0], s_keys[1], (size_t)(FP_MAX_KEYS - 1) * 16);
        s_nkeys--;
    }
    memcpy(s_keys[s_nkeys++], key, 16);
    keys_save_locked();
    UNLOCK();

    uint8_t h[32];
    uint8_t ref = sha256(key, 16, h) ? h[0] : 1;
    bt_link_mgr_set_account_ref(ref ? ref : 1);
    ESP_LOGI(TAG, "Chave de conta armazenada (%d/%d)", s_nkeys, FP_MAX_KEYS);
    bt_ble_refresh_adv();
}

esp_err_t bt_fastpair_clear_account_keys(void)
{
    if (!s_mtx) {
        return ESP_ERR_INVALID_STATE;
    }
    LOCK();
    s_nkeys = 0;
    keys_save_locked();
    UNLOCK();
    bt_ble_refresh_adv();
    return ESP_OK;
}

/* ======================= payload de advertising ======================= */

static size_t adv_provider(bt_ble_profile_t profile, uint8_t *out, size_t max)
{
    if (!s_enabled) {
        return 0;
    }

    if (profile == BT_BLE_ADV_PAIRING) {
        if (max < 7) {
            return 0;
        }
        const uint8_t ad[] = { 6, 0x16, 0x2C, 0xFE, s_model_id[0], s_model_id[1], s_model_id[2] };
        memcpy(out, ad, sizeof ad);
        return sizeof ad;
    }
    if (profile == BT_BLE_ADV_OFF) {
        return 0;
    }

    /* Nao descobrivel: filtro de Bloom com as chaves de conta (so se houver alguma) */
    LOCK();
    int n = s_nkeys;
    if (n == 0) {
        UNLOCK();
        return 0;
    }
    size_t s = (size_t)((uint8_t)(1.2f * (float)n)) + 3;
    size_t total = 9 + s;
    if (total > max) {
        UNLOCK();
        ESP_LOGW(TAG, "Sem espaco para o filtro de Bloom (%u > %u)", (unsigned)total, (unsigned)max);
        return 0;
    }
    uint8_t salt[2];
    esp_fill_random(salt, sizeof salt);          /* novo salt a cada payload (acompanha a rotacao do RPA) */

    uint8_t filter[16] = { 0 };
    for (int k = 0; k < n; k++) {
        uint8_t v[18], h[32];
        memcpy(v, s_keys[k], 16);
        memcpy(v + 16, salt, 2);
        if (!sha256(v, sizeof v, h)) {
            continue;
        }
        for (int i = 0; i < 8; i++) {
            uint32_t x = (uint32_t)h[4 * i] << 24 | (uint32_t)h[4 * i + 1] << 16 | (uint32_t)h[4 * i + 2] << 8 | h[4 * i + 3];
            uint32_t m = x % (uint32_t)(s * 8);
            filter[m / 8] |= (uint8_t)(1u << (m % 8));
        }
    }
    UNLOCK();

    size_t p = 0;
    out[p++] = (uint8_t)(8 + s);        /* tamanho do AD: tipo + uuid + dados */
    out[p++] = 0x16;                    /* Service Data - 16-bit UUID */
    out[p++] = 0x2C;
    out[p++] = 0xFE;
    out[p++] = 0x00;                    /* versao/flags */
    out[p++] = (uint8_t)(s << 4);       /* filtro, tipo 0 = mostrar UI */
    memcpy(out + p, filter, s);
    p += s;
    out[p++] = 0x21;                    /* salt: tamanho 2, tipo 1 */
    out[p++] = salt[0];
    out[p++] = salt[1];
    return p;
}

/* ======================= sessao / notificacoes ======================= */

static void session_end(void)
{
    LOCK();
    memset(&s_sess, 0, sizeof s_sess);
    UNLOCK();
    esp_timer_stop(s_sess_tmr);
}

static void session_timeout_cb(void *arg)
{
    ESP_LOGI(TAG, "Sessao Fast Pair expirou");
    session_end();
}

static void notify(uint16_t handle, bool enabled, const uint8_t data[16])
{
    if (!enabled || s_conn_id == 0xFFFF || s_gatts_if == ESP_GATT_IF_NONE) {
        ESP_LOGW(TAG, "Notificacao nao enviada (CCC desativado ou sem conexao)");
        return;
    }
    esp_err_t err = esp_ble_gatts_send_indicate(s_gatts_if, s_conn_id, handle, 16, (uint8_t *)data, false);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "send_indicate: %s", esp_err_to_name(err));
    }
}

bool bt_fastpair_enabled(void)
{
    return s_enabled;
}

bool bt_fastpair_session_active(void)
{
    if (!s_enabled) {
        return false;
    }
    LOCK();
    bool a = s_sess.active;
    UNLOCK();
    return a;
}

void bt_fastpair_on_pairing_mode(bool active)
{
    s_pairing = active;
    if (active && s_enabled) {
        /* Garante que a task criptográfica com 8 KB de stack esteja acordada para responder rapidamente ao Seeker */
        ensure_crypto_task();
    }
}

bt_gap_cfm_decision_t bt_fastpair_ssp_confirm(const uint8_t *bda, uint32_t passkey)
{
    if (!s_enabled) {
        return BT_GAP_CFM_REJECT;
    }
    LOCK();
    if (!s_sess.active) {
        UNLOCK();
        return BT_GAP_CFM_REJECT;
    }
    memcpy(s_sess.peer_bda, bda, 6);
    s_sess.peer_valid = true;
    s_sess.provider_pk = passkey;
    s_sess.provider_pk_valid = true;
    bool have_seeker = s_sess.seeker_pk_valid;
    uint32_t seeker_pk = s_sess.seeker_pk;
    uint8_t key[16];
    memcpy(key, s_sess.key, 16);
    UNLOCK();

    /* Provider -> Seeker: passkey (tipo 0x03) cifrado */
    uint8_t plain[16], enc[16];
    plain[0] = 0x03;
    plain[1] = (uint8_t)(passkey >> 16);
    plain[2] = (uint8_t)(passkey >> 8);
    plain[3] = (uint8_t)passkey;
    esp_fill_random(plain + 4, 12);
    if (aes128_block(key, plain, enc, true)) {
        notify(s_handles[IDX_PK_VAL], s_notify_pk, enc);
    }
    memset(key, 0, sizeof key);

    if (!have_seeker) {
        return BT_GAP_CFM_DEFER;     /* responde quando o Seeker escrever o passkey dele */
    }
    bool ok = seeker_pk == passkey;
    if (ok) {
        LOCK();
        s_sess.verified = true;
        UNLOCK();
    }
    ESP_LOGI(TAG, "Passkey %s", ok ? "confere" : "NAO confere");
    return ok ? BT_GAP_CFM_ACCEPT : BT_GAP_CFM_REJECT;
}

/* ======================= tratamento das escritas (task fp_crypto) ======================= */

static void handle_kbp(const fp_job_t *j)
{
    if (!s_pairing) {
        ESP_LOGW(TAG, "Key-based Pairing ignorado: fora da janela de pareamento");
        return;
    }
    const uint8_t *own = esp_bt_dev_get_address();
    if (!own || (j->len != 16 && j->len != 80)) {
        ESP_LOGW(TAG, "Key-based Pairing invalido (len %u)", j->len);
        return;
    }

    uint8_t key[16], plain[16];
    bool ok = false;
    if (j->len == 80) {
        ok = derive_session_key(j->data + 16, key) && aes128_block(key, j->data, plain, false) &&
             plain[0] == 0x00 && memcmp(plain + 2, own, 6) == 0;
    } else {
        uint8_t cand[FP_MAX_KEYS][16];
        LOCK();
        int n = s_nkeys;
        memcpy(cand, s_keys, sizeof cand);
        UNLOCK();
        for (int i = n - 1; i >= 0 && !ok; i--) {
            if (aes128_block(cand[i], j->data, plain, false) && (plain[0] == 0x00 || plain[0] == 0x10) &&
                memcmp(plain + 2, own, 6) == 0) {
                memcpy(key, cand[i], 16);
                ok = true;
            }
        }
        memset(cand, 0, sizeof cand);
    }
    if (!ok) {
        ESP_LOGW(TAG, "Key-based Pairing recusado (chave/endereco nao confere)");
        return;
    }
    if (plain[0] != 0x00) {
        ESP_LOGW(TAG, "Action Request (0x%02X) nao suportado", plain[0]);
        return;
    }

    uint8_t flags = plain[1];
    session_end();
    LOCK();
    s_sess.active = true;
    memcpy(s_sess.key, key, 16);
    UNLOCK();
    esp_timer_start_once(s_sess_tmr, (uint64_t)FP_SESSION_MS * 1000);
    ESP_LOGI(TAG, "Sessao Fast Pair iniciada (flags 0x%02X, %s)", flags, j->len == 80 ? "ECDH" : "chave de conta");

    /* Resposta: 0x01 | endereco publico | 9 bytes aleatorios, cifrada */
    uint8_t resp[16], enc[16];
    resp[0] = 0x01;
    memcpy(resp + 1, own, 6);
    esp_fill_random(resp + 7, 9);
    if (aes128_block(key, resp, enc, true)) {
        notify(s_handles[IDX_KBP_VAL], s_notify_kbp, enc);
    }

    if (flags & KBP_FLAG_INITIATE_BONDING) {
        uint8_t seeker[6];
        memcpy(seeker, plain + 8, 6);
        ESP_LOGI(TAG, "Seeker pediu que o Provider inicie o bonding: " ESP_BD_ADDR_STR, ESP_BD_ADDR_HEX(seeker));
        struct hs_actor *act = bt_link_actor_get();
        if (act) {
            bt_cmd_connect_t cmd;
            cmd.slot = 0;
            memcpy(cmd.bda, seeker, 6);
            hs_actor_send(act, BT_CMD_CONNECT, &cmd, sizeof(cmd), 0);
        } else {
            bt_a2dp_connect(seeker);
        }
    }
    memset(key, 0, sizeof key);
}

static void handle_passkey(const fp_job_t *j)
{
    uint8_t key[16], plain[16];
    LOCK();
    bool active = s_sess.active;
    memcpy(key, s_sess.key, 16);
    UNLOCK();
    if (!active || j->len != 16 || !aes128_block(key, j->data, plain, false) || plain[0] != 0x02) {
        ESP_LOGW(TAG, "Passkey do Seeker invalido ou sem sessao");
        memset(key, 0, sizeof key);
        return;
    }
    memset(key, 0, sizeof key);
    uint32_t pk = (uint32_t)plain[1] << 16 | (uint32_t)plain[2] << 8 | plain[3];

    LOCK();
    s_sess.seeker_pk = pk;
    s_sess.seeker_pk_valid = true;
    bool have_provider = s_sess.provider_pk_valid && s_sess.peer_valid;
    bool ok = have_provider && s_sess.provider_pk == pk;
    uint8_t peer[6];
    memcpy(peer, s_sess.peer_bda, 6);
    if (ok) {
        s_sess.verified = true;
    }
    UNLOCK();

    if (have_provider) {
        ESP_LOGI(TAG, "Passkey %s: %s o pareamento", ok ? "confere" : "NAO confere", ok ? "aceitando" : "recusando");
        bt_gap_ssp_reply(peer, ok);
    }
}

static void handle_account_key(const fp_job_t *j)
{
    uint8_t key[16], plain[16];
    LOCK();
    bool ok = s_sess.active && s_sess.verified;
    memcpy(key, s_sess.key, 16);
    UNLOCK();
    if (!ok || j->len != 16) {
        ESP_LOGW(TAG, "Account Key ignorada (sessao inexistente ou passkey nao verificado)");
        memset(key, 0, sizeof key);
        return;
    }
    ok = aes128_block(key, j->data, plain, false);
    memset(key, 0, sizeof key);
    session_end();          /* a chave de sessao nao pode ser reutilizada */
    if (!ok) {
        return;
    }
    if (plain[0] == 0x04) {
        account_key_add(plain);
    } else if (plain[0] == 0xFF) {
        ESP_LOGI(TAG, "Sessao temporaria (0xFF): chave nao armazenada");
    } else {
        ESP_LOGW(TAG, "Account Key com tipo desconhecido 0x%02X", plain[0]);
    }
    memset(plain, 0, sizeof plain);
}

static TaskHandle_t s_fp_task_handle = NULL;
#define FP_CRYPTO_IDLE_TIMEOUT_MS 15000

static void fp_task(void *arg)
{
    fp_job_t job;
    ESP_LOGI(TAG, "Task fp_crypto sob demanda iniciada (8 KB de stack alocados)");
    for (;;) {
        /* Aguarda jobs com timeout; se ocioso e fora de pareamento/sessão, encerra para liberar 8 KB de stack */
        if (xQueueReceive(s_jobs, &job, pdMS_TO_TICKS(FP_CRYPTO_IDLE_TIMEOUT_MS)) != pdTRUE) {
            LOCK();
            bool busy = s_pairing || s_sess.active;
            if (!busy) {
                s_fp_task_handle = NULL;
                UNLOCK();
                ESP_LOGI(TAG, "Task fp_crypto ociosa: encerrando sob demanda (-8 KB liberados)");
                vTaskDelete(NULL);
                return;
            }
            UNLOCK();
            continue;
        }

        switch (job.op) {
        case JOB_KBP:         handle_kbp(&job); break;
        case JOB_PASSKEY:     handle_passkey(&job); break;
        case JOB_ACCOUNT_KEY: handle_account_key(&job); break;
        }
    }
}

/**
 * @brief Garante que a task fp_crypto esteja rodando sob demanda.
 */
static void ensure_crypto_task(void)
{
    LOCK();
    if (s_fp_task_handle == NULL) {
        BaseType_t ok = xTaskCreatePinnedToCore(fp_task, "fp_crypto", 8192, NULL, 3, &s_fp_task_handle, 0);
        if (ok != pdPASS) {
            ESP_LOGE(TAG, "Falha ao instanciar task fp_crypto sob demanda");
        }
    }
    UNLOCK();
}

static void enqueue_write(uint16_t handle, const uint8_t *data, uint16_t len)
{
    fp_job_t job = { .len = len };
    if (len > FP_MAX_WRITE) {
        return;
    }
    if (handle == s_handles[IDX_KBP_VAL]) {
        job.op = JOB_KBP;
    } else if (handle == s_handles[IDX_PK_VAL]) {
        job.op = JOB_PASSKEY;
    } else if (handle == s_handles[IDX_AK_VAL]) {
        job.op = JOB_ACCOUNT_KEY;
    } else {
        return;
    }
    memcpy(job.data, data, len);

    ensure_crypto_task();

    if (xQueueSend(s_jobs, &job, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Fila de jobs cheia; escrita descartada");
    }
}

/* ======================= GATT ======================= */

static const uint16_t u_primary = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t u_decl    = ESP_GATT_UUID_CHAR_DECLARE;
static const uint16_t u_ccc     = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
static const uint16_t u_fe2c    = 0xFE2C;
static const uint8_t  u_model[16] = FP_UUID128(0x33, 0x12);
static const uint8_t  u_kbp[16]   = FP_UUID128(0x34, 0x12);
static const uint8_t  u_pk[16]    = FP_UUID128(0x35, 0x12);
static const uint8_t  u_ak[16]    = FP_UUID128(0x36, 0x12);
static const uint8_t  p_read = ESP_GATT_CHAR_PROP_BIT_READ;
static const uint8_t  p_wn   = ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_NOTIFY;
static const uint8_t  p_w    = ESP_GATT_CHAR_PROP_BIT_WRITE;
static uint8_t        v_model[3];
static uint8_t        v_buf[FP_MAX_WRITE];
static uint8_t        v_ccc[2];

static esp_gatts_attr_db_t s_db[IDX_NB];

static void build_db(void)
{
    memcpy(v_model, s_model_id, 3);
    const esp_attr_control_t auto_rsp = { ESP_GATT_AUTO_RSP };
    const esp_attr_control_t app_rsp  = { ESP_GATT_RSP_BY_APP };

#define ATTR(i, ctl, ulen, uuid, perm, maxl, curl, val) \
    s_db[i] = (esp_gatts_attr_db_t){ ctl, { ulen, (uint8_t *)(uuid), perm, maxl, curl, (uint8_t *)(val) } }

    ATTR(IDX_SVC,        auto_rsp, ESP_UUID_LEN_16, &u_primary, ESP_GATT_PERM_READ, 2, 2, &u_fe2c);

    ATTR(IDX_MODEL_DECL, auto_rsp, ESP_UUID_LEN_16, &u_decl, ESP_GATT_PERM_READ, 1, 1, &p_read);
    ATTR(IDX_MODEL_VAL,  app_rsp,  ESP_UUID_LEN_128, u_model, ESP_GATT_PERM_READ, 3, 3, v_model);

    ATTR(IDX_KBP_DECL,   auto_rsp, ESP_UUID_LEN_16, &u_decl, ESP_GATT_PERM_READ, 1, 1, &p_wn);
    ATTR(IDX_KBP_VAL,    app_rsp,  ESP_UUID_LEN_128, u_kbp, ESP_GATT_PERM_WRITE, FP_MAX_WRITE, 0, v_buf);
    ATTR(IDX_KBP_CCC,    auto_rsp, ESP_UUID_LEN_16, &u_ccc, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, 2, 2, v_ccc);

    ATTR(IDX_PK_DECL,    auto_rsp, ESP_UUID_LEN_16, &u_decl, ESP_GATT_PERM_READ, 1, 1, &p_wn);
    ATTR(IDX_PK_VAL,     app_rsp,  ESP_UUID_LEN_128, u_pk, ESP_GATT_PERM_WRITE, 16, 0, v_buf);
    ATTR(IDX_PK_CCC,     auto_rsp, ESP_UUID_LEN_16, &u_ccc, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, 2, 2, v_ccc);

    ATTR(IDX_AK_DECL,    auto_rsp, ESP_UUID_LEN_16, &u_decl, ESP_GATT_PERM_READ, 1, 1, &p_w);
    ATTR(IDX_AK_VAL,     app_rsp,  ESP_UUID_LEN_128, u_ak, ESP_GATT_PERM_WRITE, 16, 0, v_buf);
#undef ATTR
}

static void gatts_cb(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *p)
{
    switch (event) {
    case ESP_GATTS_REG_EVT:
        if (p->reg.app_id != FP_APP_ID || p->reg.status != ESP_GATT_OK) {
            break;
        }
        s_gatts_if = gatts_if;
        esp_ble_gatt_set_local_mtu(FP_MTU);
        if (esp_ble_gatts_create_attr_tab(s_db, gatts_if, IDX_NB, 0) != ESP_OK) {
            ESP_LOGE(TAG, "create_attr_tab falhou");
        }
        break;

    case ESP_GATTS_CREAT_ATTR_TAB_EVT:
        if (gatts_if != s_gatts_if) {
            break;
        }
        if (p->add_attr_tab.status != ESP_GATT_OK || p->add_attr_tab.num_handle != IDX_NB) {
            ESP_LOGE(TAG, "Tabela GATT invalida (status %d, %d handles)", p->add_attr_tab.status,
                     p->add_attr_tab.num_handle);
            break;
        }
        memcpy(s_handles, p->add_attr_tab.handles, sizeof s_handles);
        esp_ble_gatts_start_service(s_handles[IDX_SVC]);
        ESP_LOGI(TAG, "Servico GATT 0xFE2C ativo");
        break;

    case ESP_GATTS_CONNECT_EVT:
        if (gatts_if == s_gatts_if) {
            s_conn_id = p->connect.conn_id;
        }
        break;

    case ESP_GATTS_DISCONNECT_EVT:
        if (gatts_if == s_gatts_if && p->disconnect.conn_id == s_conn_id) {
            s_conn_id = 0xFFFF;
            s_notify_kbp = s_notify_pk = false;
            s_prep_len = 0;
        }
        break;

    case ESP_GATTS_MTU_EVT:
        if (gatts_if == s_gatts_if) {
            ESP_LOGI(TAG, "MTU negociado: %u", p->mtu.mtu);
        }
        break;

    case ESP_GATTS_READ_EVT:
        if (gatts_if != s_gatts_if) {
            break;
        }
        if (p->read.handle == s_handles[IDX_MODEL_VAL]) {
            esp_gatt_rsp_t rsp = { 0 };
            rsp.attr_value.handle = p->read.handle;
            rsp.attr_value.len = 3;
            memcpy(rsp.attr_value.value, s_model_id, 3);
            esp_ble_gatts_send_response(gatts_if, p->read.conn_id, p->read.trans_id, ESP_GATT_OK, &rsp);
        } else {
            esp_ble_gatts_send_response(gatts_if, p->read.conn_id, p->read.trans_id, ESP_GATT_READ_NOT_PERMIT, NULL);
        }
        break;

    case ESP_GATTS_WRITE_EVT: {
        if (gatts_if != s_gatts_if) {
            break;
        }
        uint16_t h = p->write.handle;
        bool ours = h == s_handles[IDX_KBP_VAL] || h == s_handles[IDX_PK_VAL] || h == s_handles[IDX_AK_VAL];

        if (h == s_handles[IDX_KBP_CCC] || h == s_handles[IDX_PK_CCC]) {   /* resposta automatica da pilha */
            bool en = p->write.len >= 1 && (p->write.value[0] & 0x01);
            if (h == s_handles[IDX_KBP_CCC]) {
                s_notify_kbp = en;
            } else {
                s_notify_pk = en;
            }
            break;
        }
        if (!ours) {
            break;
        }
        if (p->write.is_prep) {                      /* escrita longa: remonta ate o EXEC_WRITE */
            esp_gatt_status_t st = ESP_GATT_OK;
            if (p->write.offset + p->write.len > sizeof s_prep) {
                st = ESP_GATT_INVALID_ATTR_LEN;
            } else {
                if (p->write.offset == 0) {
                    s_prep_len = 0;
                }
                memcpy(s_prep + p->write.offset, p->write.value, p->write.len);
                s_prep_len = p->write.offset + p->write.len;
                s_prep_handle = h;
            }
            if (p->write.need_rsp) {
                esp_gatt_rsp_t rsp = { 0 };
                rsp.attr_value.handle = h;
                rsp.attr_value.offset = p->write.offset;
                rsp.attr_value.len = p->write.len < sizeof rsp.attr_value.value ? p->write.len : sizeof rsp.attr_value.value;
                memcpy(rsp.attr_value.value, p->write.value, rsp.attr_value.len);
                esp_ble_gatts_send_response(gatts_if, p->write.conn_id, p->write.trans_id, st, &rsp);
            }
            break;
        }
        if (p->write.need_rsp) {
            esp_ble_gatts_send_response(gatts_if, p->write.conn_id, p->write.trans_id, ESP_GATT_OK, NULL);
        }
        enqueue_write(h, p->write.value, p->write.len);
        break;
    }

    case ESP_GATTS_EXEC_WRITE_EVT:
        if (gatts_if != s_gatts_if) {
            break;
        }
        esp_ble_gatts_send_response(gatts_if, p->exec_write.conn_id, p->exec_write.trans_id, ESP_GATT_OK, NULL);
        if (p->exec_write.exec_write_flag == ESP_GATT_PREP_WRITE_EXEC && s_prep_len) {
            enqueue_write(s_prep_handle, s_prep, s_prep_len);
        }
        s_prep_len = 0;
        break;

    default:
        break;
    }
}

/* ======================= init ======================= */

esp_err_t bt_fastpair_init(void)
{
    const char *model = CONFIG_HEADSET_FASTPAIR_MODEL_ID;
    if (model[0] == '\0') {
        ESP_LOGW(TAG, "Fast Pair DESATIVADO: CONFIG_HEADSET_FASTPAIR_MODEL_ID vazio (pareamento so manual)");
        return ESP_OK;
    }
    if (!parse_model_id(model, s_model_id)) {
        ESP_LOGE(TAG, "Model ID invalido \"%s\" (esperado 6 digitos hexa)", model);
        return ESP_ERR_INVALID_ARG;
    }
    if (CONFIG_HEADSET_FASTPAIR_ANTISPOOF_PRIVATE_KEY_B64[0] == '\0') {
        ESP_LOGE(TAG, "Fast Pair DESATIVADO: chave privada Anti-Spoofing nao configurada");
        return ESP_ERR_INVALID_STATE;
    }

    s_mtx  = xSemaphoreCreateMutex();
    s_jobs = xQueueCreate(4, sizeof(fp_job_t));
    ESP_RETURN_ON_FALSE(s_mtx && s_jobs, ESP_ERR_NO_MEM, TAG, "sem memoria");

    const esp_timer_create_args_t ta = { .callback = session_timeout_cb, .name = "fp_sess" };
    ESP_RETURN_ON_ERROR(esp_timer_create(&ta, &s_sess_tmr), TAG, "timer");

    ESP_RETURN_ON_FALSE(psa_crypto_init() == PSA_SUCCESS, ESP_FAIL, TAG, "psa_crypto_init");
    ESP_RETURN_ON_ERROR(load_antispoof_key(), TAG, "chave Anti-Spoofing");
    keys_load();

    /* A task fp_crypto (8 KB de stack) passa a ser criada sob demanda em ensure_crypto_task()
     * durante o pareamento ou recebimento de escritas GATT e encerra por inatividade, economizando RAM */

    build_db();
    s_enabled = true;
    bt_ble_set_adv_provider(adv_provider);
    ESP_RETURN_ON_ERROR(bt_ble_gatts_register(FP_APP_ID, gatts_cb), TAG, "registro GATT");

    ESP_LOGI(TAG, "Fast Pair ativo (Model ID %02X%02X%02X)", s_model_id[0], s_model_id[1], s_model_id[2]);
    return ESP_OK;
}

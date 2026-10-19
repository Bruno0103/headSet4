#include "bt_ble.h"

#include <stdbool.h>
#include <string.h>

#include "esp_bt.h"
#include "esp_check.h"
#include "esp_gap_ble_api.h"
#include "esp_gatt_common_api.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "bt_ble";

#define MAX_APPS         5
#define MAX_BLE_CONN     2          /* = CONFIG_BTDM_CTRL_BLE_MAX_CONN */
#define ADV_MAX_LEN      31

/* Intervalos em unidades de 0.625 ms */
#define ADV_PAIRING_MIN  0x0080     /*  80 ms */
#define ADV_PAIRING_MAX  0x00A0     /* 100 ms (limite do Fast Pair) */
#define ADV_ACTIVE_MIN   0x0320     /* 500 ms */
#define ADV_ACTIVE_MAX   0x03E0     /* 620 ms */
#define ADV_STREAM_MIN   0x0C80     /* 2.0 s  */
#define ADV_STREAM_MAX   0x0D00
#define ADV_SLEEP_MIN    0x0800     /* 1.28 s */
#define ADV_SLEEP_MAX    0x0800

#define CFG_ADV_BIT      (1 << 0)
#define CFG_SCAN_BIT     (1 << 1)

typedef struct {
    uint16_t       app_id;
    esp_gatt_if_t  gatts_if;
    esp_gatts_cb_t cb;
    bool           used;
} gatts_app_t;

static SemaphoreHandle_t     s_lock;
static gatts_app_t           s_apps[MAX_APPS];
static bt_ble_adv_provider_t s_provider;
static char                  s_name[32];

static bt_ble_profile_t s_want = BT_BLE_ADV_OFF;   /* pedido pelo link_mgr           */
static bt_ble_profile_t s_cur  = BT_BLE_ADV_OFF;   /* o que esta sendo configurado   */
static bool    s_ready;          /* privacidade local configurada */
static bool    s_running;        /* anunciando no controlador     */
static bool    s_busy;           /* cadeia stop->config->start em andamento */
static bool    s_dirty;          /* mudou o pedido durante a cadeia */
static bool    s_streaming;
static uint8_t s_cfg_pending;
static uint8_t s_conn_ids;       /* bitmask de conn_id ativos     */

static esp_ble_adv_params_t s_params = {
    .adv_type          = ADV_TYPE_IND,
    .own_addr_type     = BLE_ADDR_TYPE_PUBLIC,
    .channel_map       = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static const char *profile_name(bt_ble_profile_t p)
{
    switch (p) {
    case BT_BLE_ADV_PAIRING: return "PAIRING";
    case BT_BLE_ADV_ACTIVE:  return "ACTIVE";
    case BT_BLE_ADV_SLEEP:   return "SLEEP";
    default:                 return "OFF";
    }
}

static int conn_count(void)
{
    return __builtin_popcount(s_conn_ids);
}

static void pick_params(void)
{
    switch (s_cur) {
    case BT_BLE_ADV_PAIRING:
        s_params.adv_int_min = ADV_PAIRING_MIN;
        s_params.adv_int_max = ADV_PAIRING_MAX;
        s_params.own_addr_type = BLE_ADDR_TYPE_PUBLIC;       /* nao rotaciona durante o pareamento */
        break;
    case BT_BLE_ADV_ACTIVE:
        s_params.adv_int_min = s_streaming ? ADV_STREAM_MIN : ADV_ACTIVE_MIN;
        s_params.adv_int_max = s_streaming ? ADV_STREAM_MAX : ADV_ACTIVE_MAX;
        /* Utiliza endereco publico para coincidir com o BD_ADDR Classico (A2DP/HFP), permitindo
         * que o app Android conecte imediatamente ao GATT sem erro 133 quando o fone ja estiver pareado */
        s_params.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
        break;
    default:
        s_params.adv_int_min = ADV_SLEEP_MIN;
        s_params.adv_int_max = ADV_SLEEP_MAX;
        s_params.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
        break;
    }
}

static size_t build_adv(uint8_t *buf)
{
    size_t n = 0;
    if (s_cur == BT_BLE_ADV_PAIRING) {
        /* Flags BLE: LE General Discoverable Mode */
        buf[n++] = 2; buf[n++] = 0x01; buf[n++] = 0x02;
        /* TX Power (+3 dBm) */
        buf[n++] = 2; buf[n++] = 0x0A; buf[n++] = (uint8_t)BT_BLE_ADV_TX_POWER_DBM;

#if CONFIG_HEADSET_SWIFT_PAIR_ENABLE
        /* Microsoft Swift Pair Beacon (especificação oficial da Microsoft Hardware Guidelines)
         * O Windows NÃO faz active scan; exige o beacon no pacote principal de Advertising.
         * Estrutura do payload:
         * - Comprimento: 0x06 (6 bytes após este campo)
         * - Tipo: 0xFF (Manufacturer Specific Data)
         * - Company ID: 0x0006 (Microsoft Corporation, Little-Endian: 0x06, 0x00)
         * - Beacon ID: 0x03 (Swift Pair)
         * - Sub-scenario: 0x01 (BR/EDR Classic usando BLE para descoberta / 0x02 para Dual-Mode)
         * - RSSI Threshold: configuravel via Kconfig (padrao 0x80)
         */
        buf[n++] = 0x06;
        buf[n++] = 0xFF;
        buf[n++] = 0x06;
        buf[n++] = 0x00;
        buf[n++] = 0x03;
        buf[n++] = 0x01;
        buf[n++] = (uint8_t)CONFIG_HEADSET_SWIFT_PAIR_RSSI_THRESHOLD;
#endif
    }

    /* Provedor adicional (Google Fast Pair: Service Data 0xFE2C + Model ID em PAIRING, Bloom filter em SLEEP/ACTIVE) */
    if (s_provider) {
        n += s_provider(s_cur, buf + n, ADV_MAX_LEN - n);
    }
    return n;
}

static size_t build_scan_rsp(uint8_t *buf)
{
    size_t n = 0;
    /* WP-BAT.3: anuncia o Battery Service (UUID16 0x180F) na scan response para que
     * Android/Windows descubram o BAS. Fica aqui (e não no ADV principal) para não
     * competir por bytes com Swift Pair / Fast Pair. Custo: 4 bytes a menos no nome. */
    buf[n++] = 3;       /* tamanho */
    buf[n++] = 0x03;    /* Complete List of 16-bit Service UUIDs */
    buf[n++] = 0x0F;    /* 0x180F little-endian */
    buf[n++] = 0x18;

    /* Anexa o Nome do Dispositivo no restante do pacote de Scan Response */
    size_t len = strlen(s_name);
    size_t max_name_len = (ADV_MAX_LEN > n + 2) ? (ADV_MAX_LEN - n - 2) : 0;
    bool shortened = len > max_name_len;
    if (shortened) {
        len = max_name_len;
    }

    if (max_name_len > 0) {
        buf[n++] = (uint8_t)(len + 1);
        buf[n++] = shortened ? 0x08 : 0x09; /* 0x08 = Shortened Local Name, 0x09 = Complete Local Name */
        memcpy(buf + n, s_name, len);
        n += len;
    }

    return n;
}

/* ---- cadeia stop -> config -> start (chamar com s_lock) ---- */

static void configure_locked(void)
{
    if (s_cur == BT_BLE_ADV_OFF || conn_count() >= MAX_BLE_CONN) {
        s_busy = false;
        ESP_LOGI(TAG, "Advertising parado (perfil %s, %d conexoes)", profile_name(s_cur), conn_count());
        return;
    }
    pick_params();

    uint8_t adv[ADV_MAX_LEN], scan[ADV_MAX_LEN];
    size_t adv_len  = build_adv(adv);
    size_t scan_len = build_scan_rsp(scan);

    s_cfg_pending = CFG_ADV_BIT | CFG_SCAN_BIT;
    esp_err_t err = esp_ble_gap_config_adv_data_raw(adv, adv_len);
    if (err == ESP_OK) {
        err = esp_ble_gap_config_scan_rsp_data_raw(scan, scan_len);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao configurar dados de advertising: %s", esp_err_to_name(err));
        s_cfg_pending = 0;
        s_busy = false;
    }
}

static void apply_locked(void)
{
    if (!s_ready) {
        return;
    }
    if (s_busy) {
        s_dirty = true;
        return;
    }
    s_busy  = true;
    s_dirty = false;
    s_cur   = s_want;

    if (s_running) {
        esp_err_t err = esp_ble_gap_stop_advertising();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "stop_advertising: %s", esp_err_to_name(err));
            s_busy = false;
        }
        return;   /* continua em ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT */
    }
    configure_locked();
}

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    switch (event) {
    case ESP_GAP_BLE_SET_LOCAL_PRIVACY_COMPLETE_EVT:
        if (param->local_privacy_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "Privacidade local (RPA) ativa");
        } else {
            ESP_LOGE(TAG, "Falha ao ativar privacidade local: %d (advertising seguira com endereco publico)",
                     param->local_privacy_cmpl.status);
        }
        s_ready = true;
        apply_locked();
        break;

    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
    case ESP_GAP_BLE_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT: {
        s_cfg_pending &= ~(event == ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT ? CFG_ADV_BIT : CFG_SCAN_BIT);
        if (s_cfg_pending != 0) {
            break;
        }
        if (s_dirty) {           /* pedido mudou no meio: recomeca com o perfil novo */
            s_busy = false;
            apply_locked();
            break;
        }
        esp_err_t err = esp_ble_gap_start_advertising(&s_params);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "start_advertising: %s", esp_err_to_name(err));
            s_busy = false;
        }
        break;
    }

    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        s_busy = false;
        s_running = param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS;
        if (s_running) {
            ESP_LOGI(TAG, "Advertising %s: %u..%u (x0.625 ms), endereco %s", profile_name(s_cur),
                     s_params.adv_int_min, s_params.adv_int_max,
                     s_params.own_addr_type == BLE_ADDR_TYPE_PUBLIC ? "publico" : "RPA");
        } else {
            ESP_LOGE(TAG, "Falha ao iniciar advertising: %d", param->adv_start_cmpl.status);
        }
        if (s_dirty) {
            apply_locked();
        }
        break;

    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        s_running = false;
        s_dirty = false;
        s_cur = s_want;          /* usa sempre o pedido mais recente */
        configure_locked();
        break;

    /* ---- Eventos de Seguranca SMP (Security Manager Protocol - AGENTS.md WP 7.1 / R.4) ---- */
    case ESP_GAP_BLE_SEC_REQ_EVT:
        /* Resposta afirmativa e imediata a requisicoes de seguranca do peer (smartphone Android/iOS).
         * Sem isso, a sessao expira e o Android reporta erro GATT 0x0F (15 - INSUF_ENCRYPTION). */
        ESP_LOGI(TAG, "SMP: Requisicao de seguranca BLE recebida. Aceitando pareamento...");
        esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
        break;

    case ESP_GAP_BLE_AUTH_CMPL_EVT: {
        esp_ble_auth_cmpl_t *auth = &param->ble_security.auth_cmpl;
        if (auth->success) {
            ESP_LOGI(TAG, "SMP: Pareamento BLE concluido com sucesso! (auth_mode: 0x%02X)", auth->auth_mode);
        } else {
            ESP_LOGW(TAG, "SMP: Pareamento BLE nao concluido (motivo: 0x%02X)", auth->fail_reason);
        }
        break;
    }

    case ESP_GAP_BLE_KEY_EVT:
        ESP_LOGD(TAG, "SMP: Chave BLE trocada (tipo: %d)", param->ble_security.ble_key.key_type);
        break;

    case ESP_GAP_BLE_NC_REQ_EVT:
        /* Numeric Comparison: para IO_CAP_NONE, responde defensivamente */
        esp_ble_confirm_reply(param->ble_security.ble_req.bd_addr, false);
        break;

    case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
        ESP_LOGI(TAG, "Parametros de conexao BLE atualizados (status: %d, min_int: %u, max_int: %u, conn_int: %u, latency: %u, timeout: %u [*10ms])",
                 param->update_conn_params.status,
                 param->update_conn_params.min_int,
                 param->update_conn_params.max_int,
                 param->update_conn_params.conn_int,
                 param->update_conn_params.latency,
                 param->update_conn_params.timeout);
        break;

    default:
        break;
    }
    xSemaphoreGive(s_lock);
}

/* ---- GATTS: despacho por app + controle de conexoes ---- */

static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    bool restart_adv = false;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (event == ESP_GATTS_REG_EVT) {
        for (int i = 0; i < MAX_APPS; i++) {
            if (s_apps[i].used && s_apps[i].app_id == param->reg.app_id) {
                s_apps[i].gatts_if = (param->reg.status == ESP_GATT_OK) ? gatts_if : ESP_GATT_IF_NONE;
            }
        }
        if (param->reg.status != ESP_GATT_OK) {
            ESP_LOGE(TAG, "Registro do app GATT %u falhou (%d)", param->reg.app_id, param->reg.status);
        }
    } else if (event == ESP_GATTS_CONNECT_EVT && param->connect.conn_id < 8) {
        uint8_t before = s_conn_ids;
        s_conn_ids |= 1u << param->connect.conn_id;
        if (before != s_conn_ids) {
            ESP_LOGI(TAG, "BLE conectado (conn_id %u, %d/%d)", param->connect.conn_id, conn_count(), MAX_BLE_CONN);
            s_running = false;          /* a conexao interrompe o advertising */
            restart_adv = true;
        }
        /* Atualiza imediatamente os parâmetros de conexão BLE para elevar o supervision timeout para 6.0s */
        bt_ble_update_conn_params(param->connect.remote_bda);
    } else if (event == ESP_GATTS_DISCONNECT_EVT && param->disconnect.conn_id < 8) {
        uint8_t before = s_conn_ids;
        s_conn_ids &= ~(1u << param->disconnect.conn_id);
        if (before != s_conn_ids) {
            ESP_LOGI(TAG, "BLE desconectado (conn_id %u, motivo 0x%02X)", param->disconnect.conn_id,
                     param->disconnect.reason);
            s_running = false;
            restart_adv = true;
        }
    }
    if (restart_adv) {
        s_busy = false;
        apply_locked();
    }

    gatts_app_t apps[MAX_APPS];
    memcpy(apps, s_apps, sizeof apps);
    xSemaphoreGive(s_lock);

    /* Callbacks dos servicos fora do lock (podem chamar de volta o bt_ble) */
    for (int i = 0; i < MAX_APPS; i++) {
        if (!apps[i].used || !apps[i].cb) {
            continue;
        }
        if (gatts_if == ESP_GATT_IF_NONE || apps[i].gatts_if == gatts_if ||
            (event == ESP_GATTS_REG_EVT && apps[i].app_id == param->reg.app_id)) {
            apps[i].cb(event, gatts_if, param);
        }
    }
}

/* ---- API ---- */

esp_err_t bt_ble_gatts_register(uint16_t app_id, esp_gatts_cb_t cb)
{
    ESP_RETURN_ON_FALSE(s_lock && cb, ESP_ERR_INVALID_STATE, TAG, "bt_ble_init() nao chamado");
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int slot = -1;
    for (int i = 0; i < MAX_APPS; i++) {
        if (!s_apps[i].used) {
            slot = slot < 0 ? i : slot;
        } else if (s_apps[i].app_id == app_id) {
            xSemaphoreGive(s_lock);
            return ESP_ERR_INVALID_ARG;
        }
    }
    if (slot < 0) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_NO_MEM;
    }
    s_apps[slot] = (gatts_app_t){ .app_id = app_id, .gatts_if = ESP_GATT_IF_NONE, .cb = cb, .used = true };
    xSemaphoreGive(s_lock);

    esp_err_t err = esp_ble_gatts_app_register(app_id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gatts_app_register(%u): %s", app_id, esp_err_to_name(err));
    }
    return err;
}

esp_err_t bt_ble_init(const char *device_name)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_NO_MEM, TAG, "mutex");
    }
    strlcpy(s_name, device_name ? device_name : "HeadSet", sizeof s_name);

    ESP_RETURN_ON_ERROR(esp_ble_gap_register_callback(gap_event_handler), TAG, "gap cb");
    ESP_RETURN_ON_ERROR(esp_ble_gatts_register_callback(gatts_event_handler), TAG, "gatts cb");
    ESP_RETURN_ON_ERROR(esp_ble_gap_set_device_name(s_name), TAG, "nome BLE");

    /* TX power fixa: o mesmo valor e anunciado no campo TX Power do Fast Pair (+3 dBm). */
    esp_err_t err = esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P3);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "tx_power_set: %s", esp_err_to_name(err));
    }

    /* Configuracao de Seguranca SMP (Security Manager Protocol - AGENTS.md WP 7.1 / R.4):
     * Configura modo de autenticacao Just Works com Bonding persistente para fones de ouvido.
     * Como o HeadSet4 opera com endereco publico fixo alinhado ao Classic (A2DP), a chave LTK
     * (ESP_BLE_ENC_KEY_MASK) e suficiente para criptografia SC. Omitimos ESP_BLE_ID_KEY_MASK
     * para evitar que a pilha tente registrar IRK na resolving list do controlador via
     * opcode 0x2027 enquanto o advertising esta ativo, o que gerava erro HCI 0x0C (Cmd Disallowed). */
    esp_ble_auth_req_t auth_req = ESP_LE_AUTH_REQ_SC_BOND;
    esp_ble_io_cap_t iocap = ESP_IO_CAP_NONE;
    uint8_t key_size = 16;
    uint8_t init_key = ESP_BLE_ENC_KEY_MASK;
    uint8_t rsp_key  = ESP_BLE_ENC_KEY_MASK;

    esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, sizeof(auth_req));
    esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, sizeof(iocap));
    esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(key_size));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key, sizeof(init_key));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &rsp_key, sizeof(rsp_key));

    /* O primeiro advertising comeca quando o evento de confirmacao de privacidade chegar.
     * Definir como false desativa o RPA (Resolvable Private Address) rotativo e mantem o
     * endereco publico fixo alinhado com o perfil Classic (A2DP). */
    ESP_RETURN_ON_ERROR(esp_ble_gap_config_local_privacy(false), TAG, "privacidade local");
    ESP_LOGI(TAG, "BLE inicializado com endereco publico fixo (\"%s\")", s_name);
    return ESP_OK;
}

void bt_ble_set_adv_provider(bt_ble_adv_provider_t provider)
{
    s_provider = provider;
}

esp_err_t bt_ble_set_profile(bt_ble_profile_t profile)
{
    ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_INVALID_STATE, TAG, "bt_ble_init() nao chamado");
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (profile != s_want) {
        ESP_LOGI(TAG, "Perfil BLE: %s -> %s", profile_name(s_want), profile_name(profile));
    }
    s_want = profile;
    apply_locked();
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

bt_ble_profile_t bt_ble_get_profile(void)
{
    return s_want;
}

void bt_ble_set_streaming(bool streaming)
{
    if (!s_lock || streaming == s_streaming) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_streaming = streaming;
    if (s_want == BT_BLE_ADV_ACTIVE) {
        apply_locked();
    }
    xSemaphoreGive(s_lock);
}

void bt_ble_refresh_adv(void)
{
    if (!s_lock) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    apply_locked();
    xSemaphoreGive(s_lock);
}

esp_err_t bt_ble_update_conn_params(const esp_bd_addr_t bda)
{
    if (!bda) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_ble_conn_update_params_t conn_params = {0};
    memcpy(conn_params.bda, bda, sizeof(esp_bd_addr_t));
    /* Intervalo de conexão de 20 ms a 50 ms (em múltiplos de 1.25 ms: 16 e 40) */
    conn_params.min_int = 16;
    conn_params.max_int = 40;
    /* Latência de escravo: permite pular 2 eventos sem gerar desconexão */
    conn_params.latency = 2;
    /* Supervision Timeout: 600 * 10 ms = 6000 ms (6,0 segundos)
     * Garante que o link BLE não expire enquanto o rádio 2.4 GHz estiver em Paging Classic (~2,56 s) */
    conn_params.timeout = 600;

    esp_err_t err = esp_ble_gap_update_conn_params(&conn_params);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Falha ao solicitar atualizacao de parametros BLE para " ESP_BD_ADDR_STR ": %s",
                 ESP_BD_ADDR_HEX(bda), esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Solicitada atualizacao de parametros BLE para " ESP_BD_ADDR_STR " (timeout=6000ms, latency=2)",
                 ESP_BD_ADDR_HEX(bda));
    }
    return err;
}


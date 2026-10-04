#include "bt_gap.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_gap_bt_api.h"
#include "esp_log.h"

#include "bt_app_core.h"

static const char *TAG = "bt_gap";

static bt_gap_cbs_t s_cbs;

void bt_gap_register_cbs(const bt_gap_cbs_t *cbs)
{
    if (cbs) {
        s_cbs = *cbs;
    }
}

/* Roda na BtAppTask: o callback original vem da task do Bluedroid, onde nao se deve bloquear. */
static void gap_evt_hdl(uint16_t ev, void *p)
{
    esp_bt_gap_cb_param_t *prm = p;

    switch (ev) {
    case ESP_BT_GAP_ACL_CONN_CMPL_STAT_EVT:
        if (prm->acl_conn_cmpl_stat.stat == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "ACL aberto com " ESP_BD_ADDR_STR, ESP_BD_ADDR_HEX(prm->acl_conn_cmpl_stat.bda));
            if (s_cbs.on_acl) {
                s_cbs.on_acl(prm->acl_conn_cmpl_stat.bda, true, 0);
            }
        } else {
            ESP_LOGW(TAG, "Falha ao abrir ACL (stat %d)", prm->acl_conn_cmpl_stat.stat);
        }
        break;

    case ESP_BT_GAP_ACL_DISCONN_CMPL_STAT_EVT:
        ESP_LOGI(TAG, "ACL fechado com " ESP_BD_ADDR_STR " (motivo HCI 0x%02X)",
                 ESP_BD_ADDR_HEX(prm->acl_disconn_cmpl_stat.bda), prm->acl_disconn_cmpl_stat.reason);
        if (s_cbs.on_acl) {
            s_cbs.on_acl(prm->acl_disconn_cmpl_stat.bda, false, prm->acl_disconn_cmpl_stat.reason);
        }
        break;

    case ESP_BT_GAP_CFM_REQ_EVT: {
        bt_gap_cfm_decision_t d = BT_GAP_CFM_REJECT;   /* sem politica registrada: nunca aceita sozinho */
        if (s_cbs.on_ssp_confirm) {
            d = s_cbs.on_ssp_confirm(prm->cfm_req.bda, prm->cfm_req.num_val);
        }
        ESP_LOGI(TAG, "SSP confirm de " ESP_BD_ADDR_STR " (passkey %06" PRIu32 "): %s",
                 ESP_BD_ADDR_HEX(prm->cfm_req.bda), prm->cfm_req.num_val,
                 d == BT_GAP_CFM_ACCEPT ? "aceito" : d == BT_GAP_CFM_REJECT ? "recusado" : "adiado");
        if (d != BT_GAP_CFM_DEFER) {
            esp_bt_gap_ssp_confirm_reply(prm->cfm_req.bda, d == BT_GAP_CFM_ACCEPT);
        }
        break;
    }

    case ESP_BT_GAP_AUTH_CMPL_EVT: {
        bool ok = prm->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS;
        if (ok) {
            ESP_LOGI(TAG, "Pareado com %s (" ESP_BD_ADDR_STR ")", (char *)prm->auth_cmpl.device_name,
                     ESP_BD_ADDR_HEX(prm->auth_cmpl.bda));
        } else {
            ESP_LOGW(TAG, "Autenticacao falhou (%d) com " ESP_BD_ADDR_STR, prm->auth_cmpl.stat,
                     ESP_BD_ADDR_HEX(prm->auth_cmpl.bda));
        }
        if (s_cbs.on_auth_complete) {
            s_cbs.on_auth_complete(prm->auth_cmpl.bda, ok);
        }
        break;
    }

    case ESP_BT_GAP_KEY_REQ_EVT:
        /* Passkey digitada: o headset nao tem teclado. */
        esp_bt_gap_ssp_passkey_reply(prm->key_req.bda, false, 0);
        break;

    case ESP_BT_GAP_PIN_REQ_EVT:
        /* PIN legado (BT < 2.1): recusado para forcar SSP, que e mais seguro. */
        ESP_LOGW(TAG, "PIN legado recusado");
        esp_bt_gap_pin_reply(prm->pin_req.bda, false, 0, NULL);
        break;

    default:
        break;
    }
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
        ESP_LOGD(TAG, "Evento GAP %d ignorado", ev);
        break;
    }
}

esp_err_t bt_gap_start(const char *device_name)
{
    if (device_name) {
        ESP_RETURN_ON_ERROR(esp_bt_gap_set_device_name(device_name), TAG, "nome");
    }
    ESP_RETURN_ON_ERROR(esp_bt_gap_register_callback(gap_cb), TAG, "callback");

    esp_bt_cod_t cod = {
        .major   = ESP_BT_COD_MAJOR_DEV_AV,
        .minor   = 6,   /* Headphones */
        .service = ESP_BT_COD_SRVC_AUDIO | ESP_BT_COD_SRVC_RENDERING,
    };
    ESP_RETURN_ON_ERROR(esp_bt_gap_set_cod(cod, ESP_BT_INIT_COD), TAG, "cod");

    /* DisplayYesNo: o Fast Pair exige comparacao numerica (passkey conferida via GATT).
     * Fora de uma sessao Fast Pair / janela de pareamento o bt_link_mgr recusa tudo. */
    esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_IO;
    ESP_RETURN_ON_ERROR(esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &iocap, sizeof iocap), TAG, "iocap");

    /* Comeca invisivel/inconectavel; o bt_link_mgr abre o scan conforme o estado do slot. */
    ESP_RETURN_ON_ERROR(esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE), TAG, "scan");

    ESP_LOGI(TAG, "GAP pronto (\"%s\")", device_name ? device_name : "");
    return ESP_OK;
}

esp_err_t bt_gap_set_scan(bool connectable, bool discoverable)
{
    esp_err_t err = esp_bt_gap_set_scan_mode(connectable ? ESP_BT_CONNECTABLE : ESP_BT_NON_CONNECTABLE,
                                             discoverable ? ESP_BT_GENERAL_DISCOVERABLE : ESP_BT_NON_DISCOVERABLE);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "set_scan_mode: %s", esp_err_to_name(err));
    }
    return err;
}

void bt_gap_ssp_reply(const uint8_t *bda, bool accept)
{
    esp_bd_addr_t a;
    memcpy(a, bda, sizeof a);
    esp_bt_gap_ssp_confirm_reply(a, accept);
}

size_t bt_gap_get_bonds(esp_bd_addr_t *out, size_t max)
{
    int n = esp_bt_gap_get_bond_device_num();
    if (n <= 0 || max == 0) {
        return 0;
    }
    esp_bd_addr_t *list = malloc(n * sizeof *list);
    if (!list) {
        return 0;
    }
    size_t copied = 0;
    if (esp_bt_gap_get_bond_device_list(&n, list) == ESP_OK) {
        copied = (size_t)n < max ? (size_t)n : max;
        memcpy(out, list, copied * sizeof *list);
    }
    free(list);
    return copied;
}

bool bt_gap_is_bonded(const uint8_t *bda)
{
    int n = esp_bt_gap_get_bond_device_num();
    if (n <= 0) {
        return false;
    }
    esp_bd_addr_t *list = malloc(n * sizeof *list);
    if (!list) {
        return false;
    }
    bool found = false;
    if (esp_bt_gap_get_bond_device_list(&n, list) == ESP_OK) {
        for (int i = 0; i < n && !found; i++) {
            found = memcmp(list[i], bda, ESP_BD_ADDR_LEN) == 0;
        }
    }
    free(list);
    return found;
}

void bt_gap_remove_bond(const uint8_t *bda)
{
    esp_bd_addr_t a;
    memcpy(a, bda, sizeof a);
    esp_err_t err = esp_bt_gap_remove_bond_device(a);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "remove_bond_device: %s", esp_err_to_name(err));
    }
}

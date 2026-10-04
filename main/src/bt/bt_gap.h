/**
 * @file bt_gap.h
 * @brief GAP Classic "fino": configuracao, visibilidade, bonds e eventos de ACL/SSP.
 *
 * Nao ha politica aqui (quem reconecta, quem pode parear, qual slot): isso e do
 * bt_link_mgr. O bt_gap so executa e repassa eventos pelos callbacks registrados.
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

typedef enum {
    BT_GAP_CFM_ACCEPT = 0,   /**< aceita a comparacao numerica agora         */
    BT_GAP_CFM_REJECT,       /**< recusa agora                               */
    BT_GAP_CFM_DEFER,        /**< outro modulo respondera: bt_gap_ssp_reply() */
} bt_gap_cfm_decision_t;

typedef struct {
    /** Pedido de confirmacao SSP (BtAppTask). passkey = numero de 6 digitos. */
    bt_gap_cfm_decision_t (*on_ssp_confirm)(const uint8_t *bda, uint32_t passkey);
    /** Autenticacao concluida (sucesso ou falha). */
    void (*on_auth_complete)(const uint8_t *bda, bool success);
    /** Enlace ACL aberto/fechado (reason = codigo HCI, so valido ao fechar). */
    void (*on_acl)(const uint8_t *bda, bool connected, uint8_t reason);
} bt_gap_cbs_t;

/** Nome, CoD de headphones, IO capability (DisplayYesNo), callbacks GAP. O scan inicia DESLIGADO. */
esp_err_t bt_gap_start(const char *device_name);

/** Registra os callbacks de politica (bt_link_mgr). */
void bt_gap_register_cbs(const bt_gap_cbs_t *cbs);

/** Visibilidade Classic: connectable = page scan; discoverable = inquiry scan. */
esp_err_t bt_gap_set_scan(bool connectable, bool discoverable);

/** Resposta tardia ao pedido de confirmacao SSP (apos BT_GAP_CFM_DEFER). */
void bt_gap_ssp_reply(const uint8_t *bda, bool accept);

bool   bt_gap_is_bonded(const uint8_t *bda);
void   bt_gap_remove_bond(const uint8_t *bda);
/** Enderecos com bond na pilha; retorna a quantidade copiada para out. */
size_t bt_gap_get_bonds(esp_bd_addr_t *out, size_t max);

#ifdef __cplusplus
}
#endif

/**
 * @file bt_fastpair.h
 * @brief Google Fast Pair Service (GFPS) - lado Provider.
 *
 * Servico GATT 0xFE2C (Key-based Pairing, Passkey, Account Key), criptografia
 * ECDH secp256r1 + AES-128 via PSA/mbedTLS, chaves de conta em NVS e anuncio com
 * filtro de Bloom. Fica DESATIVADO (no-op) enquanto CONFIG_HEADSET_FASTPAIR_MODEL_ID
 * estiver vazio.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "bt_gap.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Carrega chaves, registra o provider de advertising e o servico GATT. Chamar apos bt_ble_init(). */
esp_err_t bt_fastpair_init(void);

/** true se Model ID e chave Anti-Spoofing validos foram configurados. */
bool bt_fastpair_enabled(void);

/** true enquanto ha uma sessao Fast Pair autenticada (Key-based Pairing aceito) em andamento. */
bool bt_fastpair_session_active(void);

/** Decide o pedido SSP durante uma sessao Fast Pair (compara o passkey com o do Seeker via GATT). */
bt_gap_cfm_decision_t bt_fastpair_ssp_confirm(const uint8_t *bda, uint32_t passkey);

/** Informa a janela de pareamento (so nela o Key-based Pairing e aceito e o Model ID e anunciado). */
void bt_fastpair_on_pairing_mode(bool active);

/** Remove todas as chaves de conta (factory reset do Fast Pair). */
esp_err_t bt_fastpair_clear_account_keys(void);

#ifdef __cplusplus
}
#endif

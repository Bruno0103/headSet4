#pragma once
/* GAP: pareamento seguro, reconexao automatica, dispositivos conhecidos e
 * visibilidade.
 *
 *   Sem conexao : visivel + conectavel, aceita novos pareamentos
 *   Conectado   : OCULTO, mas ainda conectavel (page scan) para que um
 *                 dispositivo ja pareado possa pedir a troca. Novos
 *                 pareamentos sao recusados.
 *
 * O modulo nao conhece A2DP/HFP: quem o usa fornece as funcoes de
 * conectar/desconectar o perfil (bt_gap_config_t). */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_bt_defs.h"
#include "esp_err.h"

#define BT_GAP_MAX_KNOWN 8      /* maximo de dispositivos pareados guardados */

typedef enum {
    BT_GAP_PAIRABLE = 0,        /* sem conexao: visivel, aceita novos pareamentos */
    BT_GAP_RECONNECTING,        /* sem conexao: visivel, tentando religar a um conhecido */
    BT_GAP_CONNECTED,           /* conectado: oculto, so aceita bonds conhecidos */
} bt_gap_state_t;

typedef esp_err_t (*bt_gap_link_fn_t)(esp_bd_addr_t bda);
typedef void (*bt_gap_state_cb_t)(bt_gap_state_t state);
/** Comparacao numerica: mostre 'passkey' (ex.: no LCD) e chame bt_gap_confirm_pairing(). */
typedef void (*bt_gap_confirm_cb_t)(esp_bd_addr_t bda, uint32_t passkey);

typedef struct {
    const char          *device_name;
    bt_gap_link_fn_t     connect;      /* obrigatorio: abre os perfis com o aparelho */
    bt_gap_link_fn_t     disconnect;   /* obrigatorio: fecha os perfis */
    bt_gap_state_cb_t    state_cb;     /* opcional: avisa mudanca de estado (UI/LED) */
    bt_gap_confirm_cb_t  confirm_cb;   /* opcional: sem ele, pareamento e aceito automaticamente */
} bt_gap_config_t;

/** Registra callbacks, SSP, carrega conhecidos e tenta reconectar. Chamar na task BT_APP,
 *  com a pilha ativa e DEPOIS de iniciar os perfis. */
esp_err_t bt_gap_start(const bt_gap_config_t *cfg);

/* ---- consulta (qualquer task) ---- */
bt_gap_state_t bt_gap_get_state(void);
bool           bt_gap_get_active(esp_bd_addr_t out);                /* false se nao ha ninguem conectado */
size_t         bt_gap_get_known(esp_bd_addr_t *out, size_t max);    /* do mais recente ao mais antigo */

/* ---- comandos (qualquer task; executam na task BT_APP) ---- */
void bt_gap_forget(esp_bd_addr_t bda);          /* apaga o pareamento (desconecta se ativo) */
void bt_gap_forget_all(void);
void bt_gap_pair_new(void);                     /* desconecta o atual e abre o modo de pareamento */
void bt_gap_confirm_pairing(bool accept);       /* resposta ao confirm_cb */

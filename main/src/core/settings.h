/**
 * @file settings.h
 * @brief Actor Settings - Dono único e exclusivo do NVS no firmware HeadSet4.
 *
 * Em conformidade com o AGENTS.md (WP 2.1) e arquitetura orientada a atores:
 * - O actor 'settings' roda fixo no Core 0 com prioridade 3 e fila dedicada.
 * - Centraliza e encapsula todas as leituras, gravações, flush (commit) e factory reset do NVS.
 * - Implementa debounce de gravação em Flash para evitar desgaste desnecessário de blocos.
 * - Publica eventos CFG_EVT_SETTING_CHANGED(key) no barramento central de eventos hs_events.
 * - Fornece interface assíncrona (settings_set_*) e síncrona/cacheada (settings_get_*).
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "hs_actor.h"
#include "hs_cmds.h"
#include "hs_events.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Versão atual do esquema de armazenamento NVS.
 * Utilizado para migração ou detecção de incompatibilidade de layout.
 */
#define SETTINGS_SCHEMA_VERSION  1

/**
 * @brief Namespace padrão do NVS utilizado pelo actor settings.
 */
#define SETTINGS_NVS_NAMESPACE   "headset_cfg"

/**
 * @brief Tempo padrão de debounce (em milissegundos) antes do commit no NVS.
 */
#define SETTINGS_DEBOUNCE_MS     1500

/* ============================================================================
 * CHAVES PADRONIZADAS DO SISTEMA (NVS KEYS - máx 15 caracteres + null)
 * ============================================================================ */

#define SETTINGS_KEY_SCHEMA_VER      "schema_ver"
#define SETTINGS_KEY_EQ_PRESET       "eq_preset"
#define SETTINGS_KEY_EQ_GAINS        "eq_gains"
#define SETTINGS_KEY_VOL_SLOT0       "vol_slot0"
#define SETTINGS_KEY_VOL_SLOT1       "vol_slot1"
#define SETTINGS_KEY_BT_SLOTS        "bt_slots"
#define SETTINGS_KEY_BT_SEL_SLOT     "bt_sel_slot"
#define SETTINGS_KEY_BT_AUTOSWITCH   "bt_autosw"
#define SETTINGS_KEY_DISP_BRIGHT     "disp_bright"
#define SETTINGS_KEY_DISP_TIMEOUT    "disp_to"
#define SETTINGS_KEY_SENS_PROX_EN    "sens_prox_en"
#define SETTINGS_KEY_SENS_THRESH_ON  "sens_th_on"
#define SETTINGS_KEY_SENS_THRESH_OFF "sens_th_off"
#define SETTINGS_KEY_HAPTIC_INTENS   "vib_intens"
#define SETTINGS_KEY_EARS_ANGLE      "ears_angle"

/* ============================================================================
 * INICIALIZAÇÃO E CICLO DE VIDA DO ACTOR
 * ============================================================================ */

/**
 * @brief Inicializa o subsistema de settings e inicializa a task do Actor.
 *
 * Configuração da Task FreeRTOS:
 * - Nome: "act_settings"
 * - Core: Core 0
 * - Prioridade: 3 (conforme AGENTS.md §4.2)
 * - Stack: 4096 bytes
 * - Fila: 16 mensagens
 * - Modo: Fixo (idle_ms = 0)
 *
 * @return esp_err_t ESP_OK em caso de sucesso ou código de erro IDF.
 */
esp_err_t settings_init(void);

/**
 * @brief Encerra e destrói o actor settings, efetuando flush de gravações pendentes.
 *
 * @return esp_err_t ESP_OK em caso de sucesso.
 */
esp_err_t settings_deinit(void);

/**
 * @brief Obtém o handle do ator settings para integração com o motor de actors.
 *
 * @return hs_actor_t* Ponteiro do ator settings (NULL se não inicializado).
 */
hs_actor_t *settings_get_actor(void);

/* ============================================================================
 * APIS DE GRAVAÇÃO COM DEBOUNCE (ASSÍNCRONAS VIA MENSAGERIA DE ATOR)
 * ============================================================================ */

/**
 * @brief Solicita a gravação de um valor uint8_t no NVS associado a uma chave.
 *
 * Envia um comando SETTINGS_CMD_SET para a fila do ator. O ator atualiza seu
 * cache de dirty-write, reinicia o timer de debounce e publica CFG_EVT_SETTING_CHANGED.
 *
 * @param key Chave identificadora (máx 15 caracteres).
 * @param val Valor a ser persistido.
 * @return esp_err_t ESP_OK se o comando foi enfileirado com sucesso.
 */
esp_err_t settings_set_u8(const char *key, uint8_t val);

/**
 * @brief Solicita a gravação de um valor uint16_t no NVS.
 *
 * @param key Chave identificadora.
 * @param val Valor uint16_t.
 * @return esp_err_t ESP_OK se enfileirado com sucesso.
 */
esp_err_t settings_set_u16(const char *key, uint16_t val);

/**
 * @brief Solicita a gravação de um valor uint32_t no NVS.
 *
 * @param key Chave identificadora.
 * @param val Valor uint32_t.
 * @return esp_err_t ESP_OK se enfileirado com sucesso.
 */
esp_err_t settings_set_u32(const char *key, uint32_t val);

/**
 * @brief Solicita a gravação de um blob de dados binários de tamanho pequeno/médio.
 *
 * @param key Chave identificadora.
 * @param blob Ponteiro para os dados binários.
 * @param len Tamanho do blob em bytes (máximo 32 bytes para cópia direta em hs_msg_t).
 * @return esp_err_t ESP_OK se enfileirado com sucesso.
 */
esp_err_t settings_set_blob(const char *key, const void *blob, size_t len);

/**
 * @brief Força a gravação imediata na Flash de todas as pendências em debounce (nvs_commit).
 *
 * @return esp_err_t ESP_OK se o comando foi enviado.
 */
esp_err_t settings_commit(void);

/**
 * @brief Dispara restauração para padrões de fábrica, apagando chaves e reiniciando o schema.
 *
 * @return esp_err_t ESP_OK se o comando foi enfileirado.
 */
esp_err_t settings_factory_reset(void);

/* ============================================================================
 * APIS DE LEITURA (SÍNCRONAS VIA ACTOR REQUEST OU NVS SEGURO)
 * ============================================================================ */

/**
 * @brief Lê um valor uint8_t persistido para uma chave.
 *
 * @param key Chave identificadora.
 * @param[out] out_val Ponteiro para receber o valor lido.
 * @param default_val Valor padrão atribuído caso a chave não exista no NVS.
 * @return esp_err_t ESP_OK se a chave foi encontrada ou ESP_ERR_NVS_NOT_FOUND (atribuindo o default).
 */
esp_err_t settings_get_u8(const char *key, uint8_t *out_val, uint8_t default_val);

/**
 * @brief Lê um valor uint16_t persistido para uma chave.
 *
 * @param key Chave identificadora.
 * @param[out] out_val Ponteiro para receber o valor.
 * @param default_val Valor padrão atribuído se a chave não existir.
 * @return esp_err_t ESP_OK ou código de erro.
 */
esp_err_t settings_get_u16(const char *key, uint16_t *out_val, uint16_t default_val);

/**
 * @brief Lê um valor uint32_t persistido para uma chave.
 *
 * @param key Chave identificadora.
 * @param[out] out_val Ponteiro para receber o valor.
 * @param default_val Valor padrão atribuído se a chave não existir.
 * @return esp_err_t ESP_OK ou código de erro.
 */
esp_err_t settings_get_u32(const char *key, uint32_t *out_val, uint32_t default_val);

/**
 * @brief Lê um blob de dados binários do NVS associado a uma chave.
 *
 * @param key Chave identificadora.
 * @param[out] out_blob Buffer para recepção dos bytes.
 * @param len Tamanho esperado ou tamanho máximo do buffer.
 * @return esp_err_t ESP_OK se lido com sucesso, erro caso contrário.
 */
esp_err_t settings_get_blob(const char *key, void *out_blob, size_t len);

#ifdef __cplusplus
}
#endif

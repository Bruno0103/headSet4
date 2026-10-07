/**
 * @file ui_bridge.h
 * @brief Camada de integração e comunicação entre LVGL e os recursos do
 * HeadSet.
 *
 * Fornece a API completa para a interface gráfica controlar e monitorar:
 * - Bluetooth (nomes dos dispositivos, desconexão, status, alternância)
 * - Sensor de Proximidade (status, ativação, sensibilidade)
 * - Vibracall (status, ativação, intensidade de vibração)
 * - Servos das Orelhas (status, ativação, ângulo máximo)
 * - Display e Galeria (status, ligar/desligar, brilho, timeout de tela,
 * imagens)
 *
 * Todos os métodos estão fartamente documentados e preparados para integração
 * direta com botões, sliders, labels e eventos do LVGL 9.
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Tamanho máximo padronizado para strings de nomes e status retornados para a
 * UI */
#define UI_BRIDGE_STR_MAX_LEN 64
#define UI_BRIDGE_MAX_GALLERY_ITEMS 8

/* ============================================================================
 * ESTRUTURA PARA GALERIA DE IMAGENS DO DISPLAY
 * ============================================================================
 */
typedef struct {
  const char *name; /**< Nome identificador ou caminho do arquivo da imagem */
  const void *img_src; /**< Ponteiro para lv_image_dsc_t ou caminho do asset */
} ui_bridge_gallery_item_t;

/* ============================================================================
 * INICIALIZAÇÃO DA CAMADA BRIDGE
 * ============================================================================
 */

/**
 * @brief Inicializa o módulo bridge de integração UI <-> Hardware.
 * Deve ser chamado durante o boot do sistema ou antes da criação dos widgets do
 * LVGL.
 *
 * @return esp_err_t ESP_OK em caso de sucesso.
 */
esp_err_t ui_bridge_init(void);

/* ============================================================================
 * 1. BLUETOOTH
 * ============================================================================
 */

/**
 * @brief Obtém o nome amigável ou identificador do dispositivo no Slot 1.
 *
 * @param[out] out_name Buffer de caracteres para receber o nome.
 * @param[in] max_len Tamanho máximo do buffer out_name.
 */
void ui_bridge_bt_get_nome_1(char *out_name, size_t max_len);

/**
 * @brief Obtém o nome amigável ou identificador do dispositivo no Slot 2.
 *
 * @param[out] out_name Buffer de caracteres para receber o nome.
 * @param[in] max_len Tamanho máximo do buffer out_name.
 */
void ui_bridge_bt_get_nome_2(char *out_name, size_t max_len);

/**
 * @brief Solicita a desconexão manual do dispositivo pareado no Slot 1.
 *
 * @param[in] disconnect Se true, executa o comando de desconexão do
 * dispositivo 1.
 * @return true se o comando foi aceito e executado com sucesso, false caso
 * contrário.
 */
bool ui_bridge_bt_set_desconectar_dispositivo_1(bool disconnect);

/**
 * @brief Solicita a desconexão manual do dispositivo pareado no Slot 2.
 *
 * @param[in] disconnect Se true, executa o comando de desconexão do
 * dispositivo 2.
 * @return true se o comando foi aceito e executado com sucesso, false caso
 * contrário.
 */
bool ui_bridge_bt_set_desconectar_dispositivo_2(bool disconnect);

/**
 * @brief Obtém a descrição do status atual da conexão do Dispositivo 1.
 * Exemplos de retorno: "Conectado", "Desconectado", "Conectando...", "Vazio".
 *
 * @param[out] out_status Buffer para receber a string com o status.
 * @param[in] max_len Tamanho do buffer.
 */
void ui_bridge_bt_get_status_dispositivo_1(char *out_status, size_t max_len);

/**
 * @brief Obtém a descrição do status atual da conexão do Dispositivo 2.
 * Exemplos de retorno: "Conectado", "Desconectado", "Conectando...", "Vazio".
 *
 * @param[out] out_status Buffer para receber a string com o status.
 * @param[in] max_len Tamanho do buffer.
 */
void ui_bridge_bt_get_status_dispositivo_2(char *out_status, size_t max_len);

/**
 * @brief Obtém o status da alternância de dispositivos Bluetooth.
 * Descreve se a alternância automática está ativa, qual slot está em
 * reprodução, etc. Exemplo: "Ativo (Dispositivo 1)", "Ativo (Dispositivo 2)",
 * "Pausado", "Desativado".
 *
 * @param[out] out_status Buffer para receber a string do status de alternância.
 * @param[in] max_len Tamanho do buffer.
 */
void ui_bridge_bt_get_status_alternancia(char *out_status, size_t max_len);

/**
 * @brief Ativa ou desativa a alternância automática/manual entre os slots
 * Bluetooth.
 *
 * @param[in] active true para ativar alternância, false para fixar no slot
 * atual.
 * @return true se o parâmetro foi alterado com sucesso.
 */
bool ui_bridge_bt_set_alternar_ativo(bool active);

/* ============================================================================
 * 2. PROXIMIDADE (Sensor APDS-9930)
 * ============================================================================
 */

/**
 * @brief Obtém a descrição em string do estado do sensor de proximidade.
 * Exemplo: "Fone no ouvido", "Fone fora do ouvido", "Desativado", "Calibrando".
 *
 * @param[out] out_status Buffer para receber a string do status.
 * @param[in] max_len Tamanho do buffer.
 */
void ui_bridge_proximidade_get_status(char *out_status, size_t max_len);

/**
 * @brief Ativa o funcionamento do sensor de proximidade.
 * Quando ativado, detecta se o fone está colocado na cabeça para pausar/retomar
 * áudio.
 *
 * @param[in] enable true para ativar o sensor.
 * @return true se ativado com sucesso.
 */
bool ui_bridge_proximidade_set_ativar(bool enable);

/**
 * @brief Desativa o funcionamento do sensor de proximidade.
 *
 * @param[in] disable true para desativar o sensor (forçando o estado de uso
 * contínuo).
 * @return true se desativado com sucesso.
 */
bool ui_bridge_proximidade_set_desativar(bool disable);

/**
 * @brief Obtém a distância / limiar de sensibilidade atual configurado para a
 * detecção.
 *
 * @return int Valor de sensibilidade (em unidades ADC/lux/contagens de limiar).
 */
int ui_bridge_proximidade_get_distancia_sensibilidade(void);

/**
 * @brief Ajusta a distância / limiar de sensibilidade do sensor de proximidade.
 *
 * @param[in] sensibilidade Novo valor de limiar de detecção (ex: 10 a 200).
 * @return true se o valor for válido e aplicado com sucesso.
 */
bool ui_bridge_proximidade_set_distancia_sensibilidade(int sensibilidade);

/* ============================================================================
 * 3. VIBRACALL (Motor Háptico)
 * ============================================================================
 */

/**
 * @brief Obtém o status do vibracall em formato de string.
 * Exemplo: "Ativo (Vibrando)", "Pronto (Inativo)", "Desativado".
 *
 * @param[out] out_status Buffer para receber a string do status.
 * @param[in] max_len Tamanho do buffer.
 */
void ui_bridge_vibracall_get_status(char *out_status, size_t max_len);

/**
 * @brief Ativa as notificações por vibração do fone.
 *
 * @param[in] enable true para habilitar o módulo de vibração.
 * @return true se a operação teve sucesso.
 */
bool ui_bridge_vibracall_set_ativar(bool enable);

/**
 * @brief Desativa as notificações por vibração do fone.
 *
 * @param[in] disable true para desabilitar o vibracall.
 * @return true se a operação teve sucesso.
 */
bool ui_bridge_vibracall_set_desativar(bool disable);

/**
 * @brief Retorna a intensidade configurada para a vibração.
 *
 * @return int Intensidade de 0 a 100%.
 */
int ui_bridge_vibracall_get_intensidade(void);

/**
 * @brief Ajusta a intensidade de vibração do vibracall via PWM/hardware.
 *
 * @param[in] intensidade Valor entre 0 e 100%.
 * @return true se a intensidade foi aplicada com sucesso.
 */
bool ui_bridge_vibracall_set_intensidade(int intensidade);

/* ============================================================================
 * 4. ORELHAS (Servomotores / Animações Físicas)
 * ============================================================================
 */

/**
 * @brief Obtém o status dos atuadores/servos das orelhas em formato string.
 * Exemplo: "Ativo (Expressivo)", "Posicao Neutra", "Desativado".
 *
 * @param[out] out_status Buffer para receber a string do status.
 * @param[in] max_len Tamanho do buffer.
 */
void ui_bridge_orelhas_get_status(char *out_status, size_t max_len);

/**
 * @brief Ativa o movimento motorizado das orelhas.
 *
 * @param[in] enable true para habilitar os servos/movimentos.
 * @return true se ativado com sucesso.
 */
bool ui_bridge_orelhas_set_ativar(bool enable);

/**
 * @brief Desativa o movimento motorizado das orelhas (recolhe ou desenergiza
 * servos).
 *
 * @param[in] disable true para desativar.
 * @return true se desativado com sucesso.
 */
bool ui_bridge_orelhas_set_desativar(bool disable);

/**
 * @brief Retorna o ângulo máximo de movimento permitido para as orelhas.
 *
 * @return int Ângulo máximo em graus (ex: 0 a 180).
 */
int ui_bridge_orelhas_get_angulo_maximo(void);

/**
 * @brief Define o limite máximo de abertura / rotação das orelhas.
 *
 * @param[in] angulo Ângulo limite em graus (ex: 0 a 180).
 * @return true se o ângulo for válido e gravado com sucesso.
 */
bool ui_bridge_orelhas_set_angulo_maximo(int angulo);

/* ============================================================================
 * 5. DISPLAY & INTERFACE
 * ============================================================================
 */

/**
 * @brief Retorna se o display está ligado (backlight e renderização ativos).
 *
 * @return true se o display estiver ativo/aceso, false se estiver em
 * repouso/apagado.
 */
bool ui_bridge_display_get_status(void);

/**
 * @brief Ativa / Acende o display e o backlight.
 *
 * @param[in] enable true para ligar a tela.
 * @return true se acionado com sucesso.
 */
bool ui_bridge_display_set_ativar(bool enable);

/**
 * @brief Desativa / Apaga o backlight da tela para economia de energia.
 *
 * @param[in] disable true para desligar a tela.
 * @return true se desligado com sucesso.
 */
bool ui_bridge_display_set_desativar(bool disable);

/**
 * @brief Retorna o nível de brilho configurado para o display.
 *
 * @return int Brilho de 0 a 100%.
 */
int ui_bridge_display_get_brilho(void);

/**
 * @brief Ajusta o nível de brilho do backlight do display.
 *
 * @param[in] brilho Valor percentual entre 0 e 100%.
 * @return true se o brilho foi ajustado com sucesso.
 */
bool ui_bridge_display_set_brilho(int brilho);

/**
 * @brief Retorna o tempo de inatividade configurado para o display desligar
 * (timeout).
 *
 * @return int Tempo em segundos (ex: 15, 30, 60; 0 para nunca desligar).
 */
int ui_bridge_display_get_tempo_tela(void);

/**
 * @brief Ajusta o tempo de inatividade da tela até desligar automaticamente.
 *
 * @param[in] segundos Tempo em segundos (0 = sempre ligada).
 * @return true se o tempo for gravado com sucesso.
 */
bool ui_bridge_display_set_tempo_tela(int segundos);

/**
 * @brief Obtém a lista de imagens disponíveis na galeria para personalização da
 * UI.
 *
 * @param[out] out_items Array de estruturas que receberá os itens da galeria.
 * @param[in] max_items Capacidade máxima do array out_items.
 * @return int Quantidade real de imagens disponíveis na galeria.
 */
int ui_bridge_display_get_galeria_imagens(ui_bridge_gallery_item_t *out_items,
                                          int max_items);

/**
 * @brief Obtém o ponteiro ou identificador da imagem principal selecionada
 * atualmente.
 *
 * @return const void* Ponteiro para o descritor de imagem LVGL (lv_image_dsc_t)
 * ou recurso selecionado.
 */
const void *ui_bridge_display_get_imagem_principal(void);

/**
 * @brief Define qual imagem da galeria será a imagem principal da tela.
 *
 * @param[in] index Índice da imagem na galeria (0 a N-1).
 * @return true se a imagem foi alterada com sucesso.
 */
bool ui_bridge_display_set_imagem_principal(int index);

/**
 * @brief Estrutura para itens da galeria de animações/GIFs do Display.
 */
typedef struct {
  const char *name;    /**< Nome amigável ou identificador do GIF/animação */
  const void *gif_src; /**< Ponteiro para o asset/descriptor do GIF */
} ui_bridge_gallery_gif_item_t;

/**
 * @brief Obtém a lista de GIFs disponíveis na galeria para animação na tela.
 *
 * @param[out] out_items Array de estruturas que receberá os itens de GIF.
 * @param[in] max_items Capacidade máxima do array out_items.
 * @return int Quantidade real de GIFs disponíveis na galeria.
 */
int ui_bridge_display_get_galeria_gifs(ui_bridge_gallery_gif_item_t *out_items,
                                       int max_items);

/**
 * @brief Obtém o ponteiro ou identificador do GIF principal selecionado
 * atualmente.
 *
 * @return const void* Ponteiro para o recurso ou descritor do GIF.
 */
const void *ui_bridge_display_get_gif_principal(void);

/**
 * @brief Define qual GIF da galeria será o GIF principal da tela.
 *
 * @param[in] index Índice do GIF na galeria (0 a N-1).
 * @return true se o GIF foi selecionado com sucesso.
 */
bool ui_bridge_display_set_gif_principal(int index);

/* ============================================================================
 * 6. BATERIA
 * ============================================================================
 */

/**
 * @brief [Bateria get status]
 * Obtém a descrição completa do status da bateria (nível percentual, tensão e estado).
 * Exemplos de retorno: "85% (3.95 V)", "100% (Carregando)", "15% (Bateria Fraca)".
 *
 * @param[out] out_status Buffer para receber a string com o status.
 * @param[in] max_len Tamanho máximo do buffer out_status.
 */
void ui_bridge_bateria_get_status(char *out_status, size_t max_len);

/**
 * @brief [Bateria get status porcentagem]
 * Retorna o nível de carga atual da bateria em porcentagem (0 a 100%).
 *
 * @return int Valor percentual de 0 a 100.
 */
int ui_bridge_bateria_get_status_porcentagem(void);

/**
 * @brief [Bateria get status tenção]
 * Retorna a tensão real medida da bateria em milivolts ou formato float/string (ex: 3950 mV = 3.95 V).
 *
 * @return int Tensão real da bateria em milivolts (mV).
 */
int ui_bridge_bateria_get_status_tencao(void);

#ifdef __cplusplus
}
#endif

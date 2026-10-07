/**
 * @file phone_app_bridge.h
 * @brief Camada de comunicação entre o aplicativo de celular (via
 * Bluetooth/BLE) e recursos do HeadSet.
 *
 * Esta API fornece todos os métodos necessários para o aplicativo mobile
 * consultar e configurar o headset remotamente via protocolo Bluetooth:
 *
 * - Bluetooth:
 *     - Dispositivo get nome 1 (String)
 *     - Dispositivo get nome 2 (String)
 *     - Desconectar set dispositivo 1 (Boolean)
 *     - Desconectar set dispositivo 2 (Boolean)
 *     - Dispositivo 1 get status (String)
 *     - Dispositivo 2 get status (String)
 *     - Dispositivo get status de alternancia (String)
 *     - Dispositivo set alternar ativo (Boolean)
 *
 * - Proximidade:
 *     - Proximidade get status (String)
 *     - Proximidade set ativar (Boolean)
 *     - Proximidade set desativar (Boolean)
 *     - Proximidade get distancia de sensibilidade (int)
 *     - Proximidade set distancia de sensibilidade (Boolean)
 *
 * - Vibracall:
 *     - Vibracall get status (String)
 *     - Vibracall set ativar (Boolean)
 *     - Vibracall set Desativar (Boolean)
 *     - Vibracall get Intencidade de vibração (int)
 *     - Vibracall set Intencidade de vibração (Boolean)
 *
 * - Orelhas:
 *     - Orelhas get status (String)
 *     - Orelhas set ativar (Boolean)
 *     - Orelhas set desativar (Boolean)
 *     - Orelhas get angulo maximo (int)
 *     - Orelhas set angulo maximo (Boolean)
 *
 * - Display:
 *     - Display get status (Boolean)
 *     - Display set ativar (Boolean)
 *     - Display set destivar (Boolean)
 *     - Display get brilho (int)
 *     - Display set brilho (Boolean)
 *     - Display get tempo de tela (int)
 *     - Display set tempo de tela (Boolean)
 *     - Display get galeria de imagens (lista / JSON)
 *     - Display get imagem principal (índice ou nome)
 *     - Display set imagem principal (Boolean)
 *     - Display get galeria de gifs (lista / JSON)
 *     - Display get gif principal (índice ou nome)
 *     - Display set gif principal (Boolean)
 *
 * Todos os métodos são thread-safe e fartamente comentados em português.
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PHONE_APP_STR_BUFFER_SIZE 64
#define PHONE_APP_JSON_BUFFER_SIZE 512

/* ============================================================================
 * INICIALIZAÇÃO
 * ============================================================================
 */

/**
 * @brief Inicializa a ponte de comunicação do aplicativo de celular.
 *
 * @return esp_err_t ESP_OK em caso de sucesso.
 */
esp_err_t phone_app_bridge_init(void);

/**
 * @brief Processa uma mensagem de comando JSON recebida do aplicativo via
 * Bluetooth. Permite que o app envie comandos de forma serializada/remota e
 * receba a resposta formatada.
 *
 * @param[in] json_cmd String contendo o comando recebido do celular (ex:
 * {"cmd":"get_status"}).
 * @param[out] json_resp Buffer para conter a resposta de status a ser enviada
 * ao celular.
 * @param[in] max_resp_len Tamanho máximo do buffer de resposta.
 * @return esp_err_t ESP_OK se o comando foi processado com sucesso.
 */
esp_err_t phone_app_bridge_process_command(const char *json_cmd,
                                           char *json_resp,
                                           size_t max_resp_len);

/* ============================================================================
 * 1. BLUETOOTH
 * ============================================================================
 */

/**
 * @brief [Dispositivo get nome 1 String]
 * Obtém o nome amigável do dispositivo registrado no Slot 1.
 *
 * @param[out] out_name Buffer que receberá a string com o nome do dispositivo.
 * @param[in] max_len Tamanho máximo do buffer de saída.
 */
void phone_app_bt_get_nome_1(char *out_name, size_t max_len);

/**
 * @brief [Dispositivo get nome 2 String]
 * Obtém o nome amigável do dispositivo registrado no Slot 2.
 *
 * @param[out] out_name Buffer que receberá a string com o nome do dispositivo.
 * @param[in] max_len Tamanho máximo do buffer de saída.
 */
void phone_app_bt_get_nome_2(char *out_name, size_t max_len);

/**
 * @brief [Desconectar set dispositivo 1 Boolean]
 * Solicita a desconexão forçada do dispositivo pareado no Slot 1.
 *
 * @param[in] disconnect Se true, comanda a desconexão do dispositivo 1.
 * @return true se o comando foi aceito e executado com sucesso.
 */
bool phone_app_bt_set_desconectar_dispositivo_1(bool disconnect);

/**
 * @brief [Desconectar set dispositivo 2 Boolean]
 * Solicita a desconexão forçada do dispositivo pareado no Slot 2.
 *
 * @param[in] disconnect Se true, comanda a desconexão do dispositivo 2.
 * @return true se o comando foi aceito e executado com sucesso.
 */
bool phone_app_bt_set_desconectar_dispositivo_2(bool disconnect);

/**
 * @brief [Dispositivo 1 get status]
 * Retorna uma string descritiva com o status da conexão do dispositivo no
 * Slot 1. Exemplo: "Conectado", "Desconectado", "Conectando...", "Vazio".
 *
 * @param[out] out_status Buffer para receber a string com o status.
 * @param[in] max_len Tamanho máximo do buffer.
 */
void phone_app_bt_get_status_dispositivo_1(char *out_status, size_t max_len);

/**
 * @brief [Dispositivo 2 get status]
 * Retorna uma string descritiva com o status da conexão do dispositivo no
 * Slot 2. Exemplo: "Conectado", "Desconectado", "Conectando...", "Vazio".
 *
 * @param[out] out_status Buffer para receber a string com o status.
 * @param[in] max_len Tamanho máximo do buffer.
 */
void phone_app_bt_get_status_dispositivo_2(char *out_status, size_t max_len);

/**
 * @brief [Dispositivo get status de alternacia String]
 * Obtém a descrição em texto do modo de alternância entre os dois dispositivos.
 * Exemplo: "Alternância Ativa (Dispositivo 1)", "Fixado no Slot 2".
 *
 * @param[out] out_status Buffer para receber a string de status de alternância.
 * @param[in] max_len Tamanho máximo do buffer.
 */
void phone_app_bt_get_status_alternancia(char *out_status, size_t max_len);

/**
 * @brief [Dispositivo set alternar ativo Boolean]
 * Ativa ou desativa a alternância inteligente de dispositivos no headset.
 *
 * @param[in] active true para ativar alternância automática, false para fixar.
 * @return true se a configuração foi aplicada com sucesso.
 */
bool phone_app_bt_set_alternar_ativo(bool active);

/* ============================================================================
 * 2. PROXIMIDADE (Sensor APDS-9930)
 * ============================================================================
 */

/**
 * @brief [Proximidade get status String]
 * Retorna em string o estado atual de detecção do sensor de proximidade.
 * Exemplo: "Fone colocado na cabeça", "Fone retirado", "Desativado".
 *
 * @param[out] out_status Buffer para receber a string do status.
 * @param[in] max_len Tamanho máximo do buffer.
 */
void phone_app_proximidade_get_status(char *out_status, size_t max_len);

/**
 * @brief [Proximidade set ativar Boolean]
 * Ativa a detecção automática de proximidade/uso do headset.
 *
 * @param[in] enable true para ativar.
 * @return true se a operação foi executada com sucesso.
 */
bool phone_app_proximidade_set_ativar(bool enable);

/**
 * @brief [Proximidade set desativar Boolean]
 * Desativa a detecção de proximidade, forçando o fone a permanecer em modo
 * ativo.
 *
 * @param[in] disable true para desativar o sensor.
 * @return true se a operação foi executada com sucesso.
 */
bool phone_app_proximidade_set_desativar(bool disable);

/**
 * @brief [Proximidade get distancia de sensibilidade int]
 * Retorna o valor numérico de limiar de sensibilidade configurado.
 *
 * @return int Limiar numérico de sensibilidade atual.
 */
int phone_app_proximidade_get_distancia_sensibilidade(void);

/**
 * @brief [Proximidade set distancia de sensibilidade Boolean]
 * Configura o limiar de sensibilidade do sensor de proximidade.
 *
 * @param[in] sensibilidade Valor numérico de limiar de detecção (ex: 10 a 200).
 * @return true se o valor for válido e aceito pelo sistema.
 */
bool phone_app_proximidade_set_distancia_sensibilidade(int sensibilidade);

/* ============================================================================
 * 3. VIBRACALL
 * ============================================================================
 */

/**
 * @brief [Vibracall get status String]
 * Retorna uma string descritiva do estado do vibracall.
 * Exemplo: "Ativo (Vibrando)", "Pronto (Inativo)", "Desativado".
 *
 * @param[out] out_status Buffer para receber a string.
 * @param[in] max_len Tamanho máximo do buffer.
 */
void phone_app_vibracall_get_status(char *out_status, size_t max_len);

/**
 * @brief [Vibracall set ativar Boolean]
 * Ativa as respostas hápticas/vibração do fone.
 *
 * @param[in] enable true para ativar.
 * @return true se ativado com sucesso.
 */
bool phone_app_vibracall_set_ativar(bool enable);

/**
 * @brief [Vibracall set Desativar Boolean]
 * Desativa as notificações por vibração do fone.
 *
 * @param[in] disable true para desativar.
 * @return true se desativado com sucesso.
 */
bool phone_app_vibracall_set_desativar(bool disable);

/**
 * @brief [Vibracall get Intencidade de vibração int]
 * Obtém a intensidade configurada para a vibração.
 *
 * @return int Intensidade percentual atual (0 a 100%).
 */
int phone_app_vibracall_get_intensidade(void);

/**
 * @brief [Vibracall set Intencidade de vibração Boolean]
 * Ajusta a intensidade de vibração do motor de feedback háptico.
 *
 * @param[in] intensidade Nível percentual desejado (0 a 100%).
 * @return true se a intensidade foi aplicada com sucesso.
 */
bool phone_app_vibracall_set_intensidade(int intensidade);

/* ============================================================================
 * 4. ORELHAS (Servomotores)
 * ============================================================================
 */

/**
 * @brief [Orelhas get status String]
 * Retorna o status operacional das orelhas motorizadas.
 * Exemplo: "Ativas", "Em movimento", "Desativadas".
 *
 * @param[out] out_status Buffer para receber a string.
 * @param[in] max_len Tamanho máximo do buffer.
 */
void phone_app_orelhas_get_status(char *out_status, size_t max_len);

/**
 * @brief [Orelhas set ativar Boolean]
 * Habilita os servomotores e animações das orelhas do headset.
 *
 * @param[in] enable true para ativar.
 * @return true se ativado com sucesso.
 */
bool phone_app_orelhas_set_ativar(bool enable);

/**
 * @brief [Orelhas set desativar Boolean]
 * Desativa os servomotores das orelhas (recolhe para repouso e desliga PWM).
 *
 * @param[in] disable true para desativar.
 * @return true se desativado com sucesso.
 */
bool phone_app_orelhas_set_desativar(bool disable);

/**
 * @brief [Orelhas get angulo maximo int]
 * Retorna o ângulo limite de abertura em graus configurado para os servos.
 *
 * @return int Ângulo máximo em graus (ex: 0 a 180).
 */
int phone_app_orelhas_get_angulo_maximo(void);

/**
 * @brief [Orelhas set angulo maximo Boolean]
 * Ajusta o ângulo limite máximo das orelhas.
 *
 * @param[in] angulo Ângulo em graus (0 a 180).
 * @return true se o ângulo for válido e configurado com sucesso.
 */
bool phone_app_orelhas_set_angulo_maximo(int angulo);

/* ============================================================================
 * 5. DISPLAY
 * ============================================================================
 */

/**
 * @brief [Display get status Boolean]
 * Informa se a tela do headset está ligada (backlight aceso).
 *
 * @return true se a tela estiver ligada, false se estiver apagada.
 */
bool phone_app_display_get_status(void);

/**
 * @brief [Display set ativar Boolean]
 * Liga a tela e acende o backlight do headset.
 *
 * @param[in] enable true para ligar a tela.
 * @return true se a tela foi ligada com sucesso.
 */
bool phone_app_display_set_ativar(bool enable);

/**
 * @brief [Display set destivar Boolean]
 * Apaga a tela e desliga o backlight para economizar energia.
 *
 * @param[in] disable true para desligar a tela.
 * @return true se a tela foi desligada com sucesso.
 */
bool phone_app_display_set_desativar(bool disable);

/**
 * @brief [Display get brilho int]
 * Retorna o brilho da tela em porcentagem (0 a 100%).
 *
 * @return int Nível de brilho atual.
 */
int phone_app_display_get_brilho(void);

/**
 * @brief [Display set brilho Boolean]
 * Ajusta o brilho da tela do headset.
 *
 * @param[in] brilho Valor percentual de brilho (0 a 100%).
 * @return true se o brilho foi ajustado com sucesso.
 */
bool phone_app_display_set_brilho(int brilho);

/**
 * @brief [Display get tempo de tela]
 * Retorna o tempo limite de inatividade configurado em segundos até a tela
 * apagar.
 *
 * @return int Tempo em segundos (0 = sempre ligada).
 */
int phone_app_display_get_tempo_tela(void);

/**
 * @brief [Display set tempo de tela Boolean]
 * Configura o tempo de inatividade da tela até desligar automaticamente.
 *
 * @param[in] segundos Tempo limite em segundos (0 = sem desligamento).
 * @return true se o valor for aceito e gravado com sucesso.
 */
bool phone_app_display_set_tempo_tela(int segundos);

/**
 * @brief [Display get galeria de imagens]
 * Retorna a lista de imagens disponíveis na galeria no formato de string JSON
 * ou lista. Exemplo: ["Headphones", "Bluetooth Icon", "CPU Chip", "Bateria
 * Normal"]
 *
 * @param[out] out_json Buffer para receber a string com a lista.
 * @param[in] max_len Tamanho máximo do buffer.
 */
void phone_app_display_get_galeria_imagens(char *out_json, size_t max_len);

/**
 * @brief [Display get imagem principal]
 * Retorna o identificador/nome da imagem principal atualmente selecionada no
 * display.
 *
 * @param[out] out_nome Buffer para receber o nome da imagem principal.
 * @param[in] max_len Tamanho máximo do buffer.
 */
void phone_app_display_get_imagem_principal(char *out_nome, size_t max_len);

/**
 * @brief [Display set imagem principal]
 * Define a imagem principal da tela através do índice da galeria.
 *
 * @param[in] index Índice da imagem na galeria (0 a N-1).
 * @return true se a imagem foi alterada com sucesso.
 */
bool phone_app_display_set_imagem_principal(int index);

/**
 * @brief [Display get galeria de gifs]
 * Retorna a lista de GIFs/animações disponíveis no display em formato de lista
 * / JSON. Exemplo: ["Animacao Padrao", "Ondas Sonoras", "Equalizador Grafico",
 * "Pulso de Energia"]
 *
 * @param[out] out_json Buffer para receber a lista formatada.
 * @param[in] max_len Tamanho máximo do buffer.
 */
void phone_app_display_get_galeria_gifs(char *out_json, size_t max_len);

/**
 * @brief [Display get gif principal]
 * Retorna o nome/identificador do GIF principal selecionado no display.
 *
 * @param[out] out_nome Buffer para receber o nome do GIF.
 * @param[in] max_len Tamanho máximo do buffer.
 */
void phone_app_display_get_gif_principal(char *out_nome, size_t max_len);

/**
 * @brief [Display set gif principal]
 * Define o GIF animado principal da tela através do índice da galeria de GIFs.
 *
 * @param[in] index Índice do GIF na galeria (0 a N-1).
 * @return true se o GIF foi configurado com sucesso.
 */
bool phone_app_display_set_gif_principal(int index);

/* ============================================================================
 * 6. BATERIA
 * ============================================================================
 */

/**
 * @brief [Bateria get status]
 * Retorna uma string descritiva com o status da bateria (percentual e tensão em Volts).
 * Exemplo: "85% (3.95 V)", "100% (Carregando)".
 *
 * @param[out] out_status Buffer para receber a string com o status.
 * @param[in] max_len Tamanho máximo do buffer.
 */
void phone_app_bateria_get_status(char *out_status, size_t max_len);

/**
 * @brief [Bateria get status porcentagem]
 * Retorna o nível de carga atual da bateria em porcentagem (0 a 100%).
 *
 * @return int Valor percentual de 0 a 100.
 */
int phone_app_bateria_get_status_porcentagem(void);

/**
 * @brief [Bateria get status tenção]
 * Retorna a tensão real medida da bateria em milivolts (mV).
 *
 * @return int Tensão real da bateria em milivolts.
 */
int phone_app_bateria_get_status_tencao(void);

#ifdef __cplusplus
}
#endif

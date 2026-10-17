/**
 * @file cover_art.h
 * @brief Gerenciador de Cover Art (AVRCP Basic Imaging Profile / BIP e Decodificador JPEG).
 *
 * Arquitetura de Áudio e Interface (AGENTS.md §2, §4.2, WP 6.1 e Reparo Q.8):
 * - Implementa o cliente AVRCP Cover Art (BIP) sobre o protocolo OBEX/L2CAP do ESP-IDF v6.x.
 * - Recebe thumbnails de capa de música enviadas pelo smartphone (Spotify, Apple Music, YouTube Music).
 * - Acumula os pacotes (chunks) fragmentados diretamente em memória PSRAM para não onerar o heap interno.
 * - Decodifica o JPEG utilizando a biblioteca Tiny JPEG Decompressor (ROM tjpgd do ESP32) para RGB565 (200x200).
 * - Gera um descritor lv_image_dsc_t compatível com o LVGL 9 e notifica o modelo reativo de UI.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define COVER_ART_WIDTH   200
#define COVER_ART_HEIGHT  200

/**
 * @brief Inicializa os buffers em PSRAM e descritores do subsistema de Cover Art.
 * Deve ser chamado durante o boot do sistema antes da inicialização do Bluetooth.
 *
 * @return esp_err_t ESP_OK em caso de sucesso.
 */
esp_err_t cover_art_init(void);

/**
 * @brief Abre o serviço de Cover Art localmente (preparação de estado).
 */
void cover_art_srv_open(void);

/**
 * @brief Fecha o serviço de Cover Art e libera os buffers de recepção de chunks.
 */
void cover_art_srv_close(void);

/**
 * @brief Dispara a conexão do canal OBEX/BIP com o peer remoto.
 *
 * @param mtu Unidade Máxima de Transmissão (usar ESP_AVRC_CA_MTU_MAX).
 */
void cover_art_srv_connect(uint16_t mtu);

/**
 * @brief Atualiza a flag de conexão do canal de Cover Art.
 *
 * @param connected true se o canal BIP OBEX estiver conectado.
 */
void cover_art_srv_set_connected(bool connected);

/**
 * @brief Solicita a capa da faixa atual se o canal estiver conectado e ocioso.
 */
void cover_art_srv_ca_req(void);

/**
 * @brief Reseta o estado de obtenção (getting) e libera buffers incompletos.
 * Chamado em caso de troca de faixa, erro no canal ou expiração de timeout.
 */
void cover_art_reset_getting(void);

/**
 * @brief Atualiza o identificador da imagem (image_handle) recebido nos metadados.
 * Se o identificador for novo e válido, requisita o thumbnail vinculado ao host.
 *
 * @param image_handle Ponteiro para string de identificador da imagem.
 * @param len Comprimento da string do identificador.
 */
void cover_art_srv_ct_metadata_update(const uint8_t *image_handle, int len);

/**
 * @brief Acumula um chunk de dados JPEG recebido da pilha Bluedroid.
 * NOTA CRÍTICA: Os dados de p_data são temporários no callback do stack e devem
 * ser copiados imediatamente para o buffer acumulador em PSRAM.
 *
 * @param p_data Ponteiro para os bytes brutos do fragmento.
 * @param data_len Tamanho em bytes do fragmento.
 */
void cover_art_save_chunk(const uint8_t *p_data, uint16_t data_len);

/**
 * @brief Sinaliza o término da transmissão do arquivo JPEG ou de um frame.
 * Quando final for true, dispara a decodificação do buffer em PSRAM e notifica a UI.
 *
 * @param final true se o arquivo JPEG foi integralmente recebido.
 */
void cover_art_set_image_final(bool final);

/**
 * @brief Decodifica diretamente um buffer JPEG completo e atualiza o bitmap de capa.
 * Útil para injeção via app mobile (BLE), testes unitários ou papéis de parede locais.
 *
 * @param jpeg_buf Ponteiro para o arquivo JPEG completo.
 * @param jpeg_len Tamanho total em bytes do JPEG.
 * @return esp_err_t ESP_OK se decodificado com sucesso.
 */
esp_err_t cover_art_decode_jpeg_buffer(const uint8_t *jpeg_buf, size_t jpeg_len);

/**
 * @brief Obtém o descritor LVGL (lv_image_dsc_t) da capa ativa.
 * Retorna o bitmap decodificado ou a imagem padrão de fallback caso não haja capa.
 *
 * @return const lv_image_dsc_t* Descritor de imagem válido para lv_image_set_src.
 */
const lv_image_dsc_t *cover_art_get_image_dsc(void);

/**
 * @brief Verifica se há uma imagem de capa customizada atualmente decodificada.
 *
 * @return true se houver imagem customizada válida, false se for o placeholder padrão.
 */
bool cover_art_has_image(void);

/**
 * @brief Restaura a capa para o placeholder padrão do sistema.
 */
void cover_art_reset_to_default(void);

#ifdef __cplusplus
}
#endif

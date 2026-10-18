/**
 * @file media_storage.h
 * @brief Gerenciador de Armazenamento Persistente de Imagens e GIFs em Memória Flash NVS e PSRAM.
 *
 * Em conformidade com o AGENTS.md (§0, §2, §3, WP 6.1 e Reparo Q.8):
 * - Utiliza a partição dedicada "nvs_media" (4 MB) para gravação não-volátil de 6 imagens e 6 GIFs.
 * - Todas as imagens e animações são padronizadas em 240 × 296 pixels (área total sob a barra de status de 24 px).
 * - Imagens estáticas são armazenadas em formato RGB565 Little-Endian (142.080 bytes).
 * - GIFs animados (GIF89a) são armazenados em blobs binários com limite de até 400 KB por slot.
 * - A imagem ativa e o GIF ativo são cacheados na PSRAM (8 MB) para acesso imediato e renderização a 60 fps
 *   sem bloquear as tasks de áudio ou o barramento de eventos (handlers < 2 ms).
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

#define MEDIA_STORAGE_MAX_IMAGES       6
#define MEDIA_STORAGE_MAX_GIFS         6

#define MEDIA_IMAGE_WIDTH              240
#define MEDIA_IMAGE_HEIGHT             296
#define MEDIA_IMAGE_RGB565_SIZE        (MEDIA_IMAGE_WIDTH * MEDIA_IMAGE_HEIGHT * 2) /* 142.080 bytes */
#define MEDIA_MAX_GIF_SIZE             (400 * 1024)                                  /* 400 KB */
#define MEDIA_NAME_MAX_LEN             32

/**
 * @brief Metadados descritivos de um slot individual de mídia.
 */
typedef struct {
    bool has_data;                     /**< Indica se o slot possui conteúdo válido gravado */
    uint8_t slot;                      /**< Índice do slot (0 a 5) */
    char name[MEDIA_NAME_MAX_LEN];     /**< Nome amigável de exibição na galeria */
    uint32_t size;                     /**< Tamanho em bytes do arquivo */
    uint32_t crc32;                    /**< Checksum CRC32 para integridade */
} media_slot_info_t;

/**
 * @brief Catálogo completo de mídia persistida no HeadSet4.
 */
typedef struct {
    uint8_t active_img_idx;                                /**< Índice da imagem de fundo ativa (0 a 5) */
    uint8_t active_gif_idx;                                /**< Índice do GIF de animação ativo (0 a 5) */
    media_slot_info_t images[MEDIA_STORAGE_MAX_IMAGES];    /**< Catálogo dos 6 slots de imagem */
    media_slot_info_t gifs[MEDIA_STORAGE_MAX_GIFS];        /**< Catálogo dos 6 slots de GIF */
} media_catalog_t;

/**
 * @brief Inicializa o subsistema de armazenamento de mídia na partição nvs_media.
 * Abre a partição NVS dedicada, lê o catálogo de metadados, aloca os buffers
 * na memória PSRAM e carrega a imagem ativa e o GIF ativo.
 *
 * @return esp_err_t ESP_OK em caso de sucesso ou código de erro IDF.
 */
esp_err_t media_storage_init(void);

/**
 * @brief Obtém uma cópia atômica e thread-safe do catálogo de mídias.
 *
 * @param[out] out_catalog Ponteiro para receber a estrutura do catálogo.
 * @return esp_err_t ESP_OK em caso de sucesso.
 */
esp_err_t media_storage_get_catalog(media_catalog_t *out_catalog);

/**
 * @brief Obtém o descritor nativo do LVGL 9 (lv_image_dsc_t) da imagem ativa em PSRAM.
 *
 * @return const lv_image_dsc_t* Ponteiro para o descritor de imagem na PSRAM.
 */
const lv_image_dsc_t *media_storage_get_active_image_dsc(void);

/**
 * @brief Obtém o ponteiro do buffer contíguo em PSRAM contendo os bytes do GIF ativo.
 *
 * @param[out] out_len Ponteiro opcional para receber o tamanho em bytes do GIF ativo.
 * @return const void* Ponteiro para os dados do GIF em PSRAM (ou NULL se vazio).
 */
const void *media_storage_get_active_gif_data(size_t *out_len);

/**
 * @brief Obtém o descritor nativo do LVGL 9 (lv_image_dsc_t) com formato RAW apontando para o GIF ativo na PSRAM.
 *
 * @return const lv_image_dsc_t* Descritor para uso com lv_gif_set_src.
 */
const lv_image_dsc_t *media_storage_get_active_gif_dsc(void);

/**
 * @brief Define qual slot de imagem (0 a 5) será o papel de parede principal.
 * Carrega os pixels do slot da NVS para a PSRAM e persiste a escolha.
 *
 * @param slot Índice do slot (0 a 5).
 * @return esp_err_t ESP_OK em caso de sucesso.
 */
esp_err_t media_storage_set_active_image(uint8_t slot);

/**
 * @brief Define qual slot de GIF (0 a 5) será a animação principal.
 * Carrega o arquivo do slot da NVS para a PSRAM e persiste a escolha.
 *
 * @param slot Índice do slot (0 a 5).
 * @return esp_err_t ESP_OK em caso de sucesso.
 */
esp_err_t media_storage_set_active_gif(uint8_t slot);

/**
 * @brief Grava uma nova imagem bitmap RGB565 em um dos 6 slots da partição NVS.
 *
 * @param slot Índice do slot (0 a 5).
 * @param name Nome identificador amigável (máx 31 caracteres).
 * @param data Ponteiro para o array de bytes RGB565 (deve ter 142.080 bytes).
 * @param len Tamanho dos dados em bytes.
 * @param crc32 Checksum esperado para validação antes de cometer na Flash.
 * @return esp_err_t ESP_OK em caso de sucesso.
 */
esp_err_t media_storage_save_image(uint8_t slot, const char *name, const uint8_t *data, size_t len, uint32_t crc32);

/**
 * @brief Grava um novo arquivo GIF animado em um dos 6 slots da partição NVS.
 *
 * @param slot Índice do slot (0 a 5).
 * @param name Nome identificador amigável (máx 31 caracteres).
 * @param data Ponteiro para os bytes do arquivo GIF89a.
 * @param len Tamanho dos dados em bytes (até 400 KB).
 * @param crc32 Checksum esperado para validação.
 * @return esp_err_t ESP_OK em caso de sucesso.
 */
esp_err_t media_storage_save_gif(uint8_t slot, const char *name, const uint8_t *data, size_t len, uint32_t crc32);

/**
 * @brief Apaga o conteúdo de um slot de imagem ou GIF na partição NVS.
 *
 * @param is_gif true para slot de GIF, false para slot de imagem.
 * @param slot Índice do slot (0 a 5).
 * @return esp_err_t ESP_OK em caso de sucesso.
 */
esp_err_t media_storage_delete(bool is_gif, uint8_t slot);

/**
 * @brief Apaga todos os slots de imagens da partição NVS, restaurando o padrão gráfico do sistema.
 *
 * Remove todas as chaves de imagens (img_blob_0 a img_blob_5), redefine o catálogo
 * deixando o slot 0 com o papel de parede padrão gerado e os slots 1 a 5 vazios,
 * redefine o índice ativo para 0 e atualiza o buffer em PSRAM.
 *
 * @return esp_err_t ESP_OK em caso de sucesso.
 */
esp_err_t media_storage_erase_all_images(void);

/**
 * @brief Formata completamente o armazenamento de mídia no NVS, apagando todas as imagens e GIFs.
 *
 * Limpa todo o namespace da partição nvs_media, recria o catálogo zerado de fábrica
 * e redefine os ponteiros e índices em PSRAM e no ator settings.
 *
 * @return esp_err_t ESP_OK em caso de sucesso.
 */
esp_err_t media_storage_format(void);

/**
 * @brief Calcula o checksum CRC32 padronizado (IEEE 802.3) de um bloco de dados.
 *
 * @param data Ponteiro para os dados.
 * @param len Comprimento em bytes.
 * @return uint32_t Valor do CRC32.
 */
uint32_t media_storage_calc_crc32(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

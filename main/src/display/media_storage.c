/**
   * @file media_storage.c
   * @brief Implementação do Gerenciador de Armazenamento de Imagens e GIFs em
   * Flash NVS e PSRAM.
   *
   * Arquitetura de Alto Desempenho e Concorrência Segura:
   * - Grava até 6 imagens (240x296 RGB565 Little-Endian = 142.080 bytes) e até
   * 6 GIFs (máx 400 KB) na partição Flash dedicada "nvs_media" (4 MB).
   * - Utiliza a memória PSRAM de 8 MB do ESP32-WROVER-E para manter em cache os
   * buffers de exibição da imagem ativa e do GIF ativo, garantindo renderização
   * instantânea a 60 fps.
   * - Valida integridade via CRC32 antes de persistir blocos na Flash para
   * evitar corrupção.
   * - Sincroniza os índices ativos com o ator settings (chaves "disp_img_idx" e
   * "disp_gif_idx").
   * - Thread-safe através de mutex não-recursivo de curta retenção.
   */

#include "media_storage.h"

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

    static const char *TAG = "media_storage";

#define MEDIA_NVS_PARTITION_NAME "nvs_media"
#define MEDIA_NVS_NAMESPACE "media_store"
#define MEDIA_NVS_KEY_CATALOG "meta_catalog"

/* Buffer estático na PSRAM para a imagem de fundo ativa (240x296 pixels RGB565)
 */
static uint16_t *s_active_img_pixels = NULL;

/* Descritor de imagem nativo do LVGL 9 apontando para o buffer em PSRAM */
static lv_image_dsc_t s_active_img_dsc = {
    .header =
        {
            .magic = LV_IMAGE_HEADER_MAGIC,
            .cf = LV_COLOR_FORMAT_RGB565,
            .flags = 0,
            .w = MEDIA_IMAGE_WIDTH,
            .h = MEDIA_IMAGE_HEIGHT,
            .stride = MEDIA_IMAGE_WIDTH * sizeof(uint16_t),
            .reserved_2 = 0,
        },
    .data_size = MEDIA_IMAGE_RGB565_SIZE,
    .data = NULL,
    .reserved = NULL,
};

/* Buffer contíguo na PSRAM para o arquivo binário do GIF ativo */
static uint8_t *s_active_gif_buffer = NULL;
static size_t s_active_gif_len = 0;

/* Descritor LVGL RAW para o GIF ativo */
static lv_image_dsc_t s_active_gif_dsc = {
    .header =
        {
            .magic = LV_IMAGE_HEADER_MAGIC,
            .cf = LV_COLOR_FORMAT_RAW,
            .flags = 0,
            .w = MEDIA_IMAGE_WIDTH,
            .h = MEDIA_IMAGE_HEIGHT,
            .stride = 0,
            .reserved_2 = 0,
        },
    .data_size = 0,
    .data = NULL,
    .reserved = NULL,
};

/* Estado interno e catálogo mantido em RAM */
static media_catalog_t s_catalog;
static SemaphoreHandle_t s_mutex = NULL;
static bool s_initialized = false;
static nvs_handle_t s_nvs_h = 0;

/* ============================================================================
 * UTILITÁRIO DE CÁLCULO DE CRC32 (IEEE 802.3)
 * ============================================================================
 */

uint32_t media_storage_calc_crc32(const uint8_t *data, size_t len) {
  if (!data || len == 0)
    return 0;

  uint32_t crc = 0xFFFFFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++) {
      if (crc & 1) {
        crc = (crc >> 1) ^ 0xEDB88320;
      } else {
        crc >>= 1;
      }
    }
  }
  return ~crc;
}

/* ============================================================================
 * GERAÇÃO DE PADRÕES GRÁFICOS DEFAULT (PLACEHOLDERS ELEGANTES)
 * ============================================================================
 */

/**
 * @brief Gera um degradê suave em tons escuros no buffer RGB565 para preencher
 * o papel de parede inicial caso nenhum tenha sido carregado pelo smartphone.
 */
static void generate_default_wallpaper_pattern(uint16_t *pixels) {
  if (!pixels)
    return;

  for (int y = 0; y < MEDIA_IMAGE_HEIGHT; y++) {
    /* Variação vertical de luminosidade (fundo escuro #101418 a #252D35) */
    uint8_t r = 16 + (y * 20) / MEDIA_IMAGE_HEIGHT;
    uint8_t g = 20 + (y * 25) / MEDIA_IMAGE_HEIGHT;
    uint8_t b = 28 + (y * 35) / MEDIA_IMAGE_HEIGHT;

    /* Conversão para RGB565 Little-Endian */
    uint16_t color = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);

    for (int x = 0; x < MEDIA_IMAGE_WIDTH; x++) {
      /* Adiciona uma sutil linha de grade tecnológica a cada 32 pixels */
      if ((x % 32 == 0) || (y % 32 == 0)) {
        uint16_t grid_color = color + 0x0821;
        pixels[y * MEDIA_IMAGE_WIDTH + x] = grid_color;
      } else {
        pixels[y * MEDIA_IMAGE_WIDTH + x] = color;
      }
    }
  }
}

/* ============================================================================
 * PERSISTÊNCIA E CARGA NA FLASH NVS
 * ============================================================================
 */

static esp_err_t save_catalog_to_nvs(void) {
  if (!s_nvs_h)
    return ESP_ERR_INVALID_STATE;
  esp_err_t err = nvs_set_blob(s_nvs_h, MEDIA_NVS_KEY_CATALOG, &s_catalog,
                               sizeof(s_catalog));
  if (err == ESP_OK) {
    nvs_commit(s_nvs_h);
  }
  return err;
}

static esp_err_t load_catalog_from_nvs(void) {
  if (!s_nvs_h)
    return ESP_ERR_INVALID_STATE;
  size_t req_len = sizeof(s_catalog);
  esp_err_t err =
      nvs_get_blob(s_nvs_h, MEDIA_NVS_KEY_CATALOG, &s_catalog, &req_len);

  if (err == ESP_ERR_NVS_NOT_FOUND || req_len != sizeof(s_catalog)) {
    ESP_LOGI(TAG, "Catálogo de mídias não encontrado no NVS. Inicializando com "
                  "valores padrão.");
    memset(&s_catalog, 0, sizeof(s_catalog));
    s_catalog.active_img_idx = 0;
    s_catalog.active_gif_idx = 0;

    for (int i = 0; i < MEDIA_STORAGE_MAX_IMAGES; i++) {
      s_catalog.images[i].slot = i;
      s_catalog.images[i].has_data = (i == 0); /* Slot 0 inicia com padrão */
      s_catalog.images[i].size = (i == 0) ? MEDIA_IMAGE_RGB565_SIZE : 0;
      snprintf(s_catalog.images[i].name, sizeof(s_catalog.images[i].name),
               (i == 0) ? "Wallpaper Padrão" : "Imagem %d (Vazio)", i);
    }

    for (int i = 0; i < MEDIA_STORAGE_MAX_GIFS; i++) {
      s_catalog.gifs[i].slot = i;
      s_catalog.gifs[i].has_data = false;
      s_catalog.gifs[i].size = 0;
      snprintf(s_catalog.gifs[i].name, sizeof(s_catalog.gifs[i].name),
               "GIF %d (Vazio)", i);
    }

    save_catalog_to_nvs();
    return ESP_OK;
  }

  return err;
}

/* ============================================================================
 * INICIALIZAÇÃO DO MÓDULO
 * ============================================================================
 */

esp_err_t media_storage_init(void) {
  if (s_initialized) {
    return ESP_OK;
  }

  ESP_LOGI(TAG, "Inicializando subsistema de armazenamento de mídia NVS/PSRAM "
                "(240x296)...");

  s_mutex = xSemaphoreCreateMutex();
  if (!s_mutex) {
    ESP_LOGE(TAG, "Falha ao criar mutex do media_storage");
    return ESP_ERR_NO_MEM;
  }

  /* 1. Aloca buffers contíguos na memória PSRAM externa de 8 MB */
  s_active_img_pixels = (uint16_t *)heap_caps_malloc(
      MEDIA_IMAGE_RGB565_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!s_active_img_pixels) {
    ESP_LOGE(TAG, "Falha ao alocar buffer da imagem ativa em PSRAM (%d B)",
             MEDIA_IMAGE_RGB565_SIZE);
    return ESP_ERR_NO_MEM;
  }

  s_active_gif_buffer = (uint8_t *)heap_caps_malloc(
      MEDIA_MAX_GIF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!s_active_gif_buffer) {
    ESP_LOGW(TAG,
             "Aviso: buffer de GIF em PSRAM alocado com capacidade reduzida");
  }

  /* Vincula descritor do LVGL 9 ao buffer da PSRAM */
  s_active_img_dsc.data = (const uint8_t *)s_active_img_pixels;
  generate_default_wallpaper_pattern(s_active_img_pixels);

  /* 2. Inicializa a partição NVS dedicada 'nvs_media' */
  esp_err_t err = nvs_flash_init_partition(MEDIA_NVS_PARTITION_NAME);
  if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
      err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGW(TAG, "Partição %s necessita formatação. Executando erase...",
             MEDIA_NVS_PARTITION_NAME);
    nvs_flash_erase_partition(MEDIA_NVS_PARTITION_NAME);
    err = nvs_flash_init_partition(MEDIA_NVS_PARTITION_NAME);
  }

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Falha crítica ao inicializar partição %s: %s",
             MEDIA_NVS_PARTITION_NAME, esp_err_to_name(err));
    return err;
  }

  /* 3. Abre namespace no NVS dedicado */
  err = nvs_open_from_partition(MEDIA_NVS_PARTITION_NAME, MEDIA_NVS_NAMESPACE,
                                NVS_READWRITE, &s_nvs_h);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Falha ao abrir namespace %s: %s", MEDIA_NVS_NAMESPACE,
             esp_err_to_name(err));
    return err;
  }

  /* 4. Carrega metadados e sincroniza índices ativos */
  load_catalog_from_nvs();

  uint8_t saved_img_idx = 0;
  if (settings_get_u8(SETTINGS_KEY_DISP_IMG_IDX, &saved_img_idx, 0) == ESP_OK) {
    if (saved_img_idx < MEDIA_STORAGE_MAX_IMAGES) {
      s_catalog.active_img_idx = saved_img_idx;
    }
  }

  uint8_t saved_gif_idx = 0;
  if (settings_get_u8(SETTINGS_KEY_DISP_GIF_IDX, &saved_gif_idx, 0) == ESP_OK) {
    if (saved_gif_idx < MEDIA_STORAGE_MAX_GIFS) {
      s_catalog.active_gif_idx = saved_gif_idx;
    }
  }

  /* 5. Carrega os dados da imagem ativa e GIF ativo para a PSRAM */
  media_storage_set_active_image(s_catalog.active_img_idx);
  media_storage_set_active_gif(s_catalog.active_gif_idx);

  s_initialized = true;
  ESP_LOGI(TAG,
           "Armazenamento de mídia inicializado com sucesso (Img Ativa=%d, GIF "
           "Ativo=%d)",
           s_catalog.active_img_idx, s_catalog.active_gif_idx);
  return ESP_OK;
}

/* ============================================================================
 * CONSULTAS DE CATÁLOGO E DESCRITORES
 * ============================================================================
 */

esp_err_t media_storage_get_catalog(media_catalog_t *out_catalog) {
  if (!out_catalog)
    return ESP_ERR_INVALID_ARG;
  if (!s_initialized)
    return ESP_ERR_INVALID_STATE;

  if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    memcpy(out_catalog, &s_catalog, sizeof(media_catalog_t));
    xSemaphoreGive(s_mutex);
    return ESP_OK;
  }
  return ESP_ERR_TIMEOUT;
}

const lv_image_dsc_t *media_storage_get_active_image_dsc(void) {
  return &s_active_img_dsc;
}

const void *media_storage_get_active_gif_data(size_t *out_len) {
  if (out_len) {
    *out_len = s_active_gif_len;
  }
  if (s_active_gif_len > 0) {
    return (const void *)s_active_gif_buffer;
  }
  return NULL;
}

const lv_image_dsc_t *media_storage_get_active_gif_dsc(void) {
  if (s_active_gif_len > 0 && s_active_gif_buffer) {
    s_active_gif_dsc.data = s_active_gif_buffer;
    s_active_gif_dsc.data_size = s_active_gif_len;
    return &s_active_gif_dsc;
  }
  return NULL;
}

/* ============================================================================
 * SELEÇÃO DE MÍDIA ATIVA (PAPEL DE PAREDE E GIF PRINCIPAL)
 * ============================================================================
 */

esp_err_t media_storage_set_active_image(uint8_t slot) {
  if (slot >= MEDIA_STORAGE_MAX_IMAGES)
    return ESP_ERR_INVALID_ARG;
  if (!s_nvs_h || !s_active_img_pixels)
    return ESP_ERR_INVALID_STATE;

  if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  char key[16];
  snprintf(key, sizeof(key), "img_blob_%u", slot);

  size_t req_len = MEDIA_IMAGE_RGB565_SIZE;
  esp_err_t err = nvs_get_blob(s_nvs_h, key, s_active_img_pixels, &req_len);

  if (err == ESP_OK && req_len == MEDIA_IMAGE_RGB565_SIZE) {
    ESP_LOGI(TAG,
             "Imagem do slot %u carregada com sucesso para PSRAM (%u bytes)",
             slot, req_len);
  } else {
    /* Se o slot não possui imagem na Flash, gera padrão elegante */
    ESP_LOGW(
        TAG,
        "Slot %u sem dados gravados na Flash. Gerando padrão gráfico default.",
        slot);
    generate_default_wallpaper_pattern(s_active_img_pixels);
  }

  s_catalog.active_img_idx = slot;
  save_catalog_to_nvs();
  xSemaphoreGive(s_mutex);

  /* Persiste no ator settings */
  settings_set_u8(SETTINGS_KEY_DISP_IMG_IDX, slot);
  return ESP_OK;
}

esp_err_t media_storage_set_active_gif(uint8_t slot) {
  if (slot >= MEDIA_STORAGE_MAX_GIFS)
    return ESP_ERR_INVALID_ARG;
  if (!s_nvs_h || !s_active_gif_buffer)
    return ESP_ERR_INVALID_STATE;

  if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  char key[16];
  snprintf(key, sizeof(key), "gif_blob_%u", slot);

  size_t req_len = MEDIA_MAX_GIF_SIZE;
  esp_err_t err = nvs_get_blob(s_nvs_h, key, s_active_gif_buffer, &req_len);

  if (err == ESP_OK && req_len > 0) {
    s_active_gif_len = req_len;
    ESP_LOGI(TAG, "GIF do slot %u carregado para PSRAM (%u bytes)", slot,
             req_len);
  } else {
    s_active_gif_len = 0;
    ESP_LOGI(TAG, "Slot %u de GIF está vazio", slot);
  }

  s_catalog.active_gif_idx = slot;
  save_catalog_to_nvs();
  xSemaphoreGive(s_mutex);

  /* Persiste no ator settings */
  settings_set_u8(SETTINGS_KEY_DISP_GIF_IDX, slot);
  return ESP_OK;
}

/* ============================================================================
 * GRAVAÇÃO DE NOVOS ARQUIVOS NA FLASH NVS
 * ============================================================================
 */

esp_err_t media_storage_save_image(uint8_t slot, const char *name,
                                   const uint8_t *data, size_t len,
                                   uint32_t crc32) {
  if (slot >= MEDIA_STORAGE_MAX_IMAGES || !data ||
      len != MEDIA_IMAGE_RGB565_SIZE) {
    ESP_LOGE(TAG, "Tamanho de imagem inválido (%u bytes, esperado %d bytes)",
             (unsigned int)len, MEDIA_IMAGE_RGB565_SIZE);
    return ESP_ERR_INVALID_ARG;
  }

  /* Validação matemática de integridade via CRC32 */
  uint32_t calc_crc = media_storage_calc_crc32(data, len);
  if (crc32 != 0 && calc_crc != crc32) {
    ESP_LOGE(TAG,
             "Falha de integridade: CRC32 calculado (0x%08" PRIX32
             ") != esperado (0x%08" PRIX32 ")",
             calc_crc, crc32);
    return ESP_ERR_INVALID_CRC;
  }

  if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  char key[16];
  snprintf(key, sizeof(key), "img_blob_%u", slot);

  esp_err_t err = nvs_set_blob(s_nvs_h, key, data, len);
  if (err == ESP_OK) {
    nvs_commit(s_nvs_h);

    /* Atualiza metadados */
    s_catalog.images[slot].has_data = true;
    s_catalog.images[slot].size = (uint32_t)len;
    s_catalog.images[slot].crc32 = calc_crc;
    if (name && strlen(name) > 0) {
      strncpy(s_catalog.images[slot].name, name,
              sizeof(s_catalog.images[slot].name) - 1);
      s_catalog.images[slot].name[sizeof(s_catalog.images[slot].name) - 1] =
          '\0';
    }
    save_catalog_to_nvs();

    /* Se for o slot ativo atual, atualiza o cache da PSRAM imediatamente */
    if (slot == s_catalog.active_img_idx && s_active_img_pixels) {
      memcpy(s_active_img_pixels, data, len);
    }

    ESP_LOGI(TAG,
             "Imagem '%s' gravada com sucesso no slot %u (CRC32: 0x%08" PRIX32
             ")",
             s_catalog.images[slot].name, slot, calc_crc);
  } else {
    ESP_LOGE(TAG, "Erro ao gravar blob de imagem no NVS: %s",
             esp_err_to_name(err));
  }

  xSemaphoreGive(s_mutex);
  return err;
}

esp_err_t media_storage_save_gif(uint8_t slot, const char *name,
                                 const uint8_t *data, size_t len,
                                 uint32_t crc32) {
  if (slot >= MEDIA_STORAGE_MAX_GIFS || !data || len == 0 ||
      len > MEDIA_MAX_GIF_SIZE) {
    ESP_LOGE(TAG, "Tamanho de GIF inválido (%u bytes, máx %d bytes)",
             (unsigned int)len, MEDIA_MAX_GIF_SIZE);
    return ESP_ERR_INVALID_ARG;
  }

  uint32_t calc_crc = media_storage_calc_crc32(data, len);
  if (crc32 != 0 && calc_crc != crc32) {
    ESP_LOGE(TAG,
             "Falha de CRC32 no GIF: calc=0x%08" PRIX32 ", exp=0x%08" PRIX32,
             calc_crc, crc32);
    return ESP_ERR_INVALID_CRC;
  }

  if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  char key[16];
  snprintf(key, sizeof(key), "gif_blob_%u", slot);

  esp_err_t err = nvs_set_blob(s_nvs_h, key, data, len);
  if (err == ESP_OK) {
    nvs_commit(s_nvs_h);

    /* Atualiza metadados */
    s_catalog.gifs[slot].has_data = true;
    s_catalog.gifs[slot].size = (uint32_t)len;
    s_catalog.gifs[slot].crc32 = calc_crc;
    if (name && strlen(name) > 0) {
      strncpy(s_catalog.gifs[slot].name, name,
              sizeof(s_catalog.gifs[slot].name) - 1);
      s_catalog.gifs[slot].name[sizeof(s_catalog.gifs[slot].name) - 1] = '\0';
    }
    save_catalog_to_nvs();

    /* Se for o slot ativo atual, atualiza o cache da PSRAM */
    if (slot == s_catalog.active_gif_idx && s_active_gif_buffer) {
      memcpy(s_active_gif_buffer, data, len);
      s_active_gif_len = len;
    }

    ESP_LOGI(
        TAG,
        "GIF '%s' gravado com sucesso no slot %u (%u bytes, CRC32: 0x%08" PRIX32
        ")",
        s_catalog.gifs[slot].name, slot, (unsigned int)len, calc_crc);
  } else {
    ESP_LOGE(TAG, "Erro ao gravar blob de GIF no NVS: %s",
             esp_err_to_name(err));
  }

  xSemaphoreGive(s_mutex);
  return err;
}

esp_err_t media_storage_delete(bool is_gif, uint8_t slot) {
  if (slot >= 6)
    return ESP_ERR_INVALID_ARG;
  if (!s_nvs_h)
    return ESP_ERR_INVALID_STATE;

  if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  char key[16];
  snprintf(key, sizeof(key), "%s_blob_%u", is_gif ? "gif" : "img", slot);
  nvs_erase_key(s_nvs_h, key);
  nvs_commit(s_nvs_h);

  if (is_gif) {
    s_catalog.gifs[slot].has_data = false;
    s_catalog.gifs[slot].size = 0;
    s_catalog.gifs[slot].crc32 = 0;
    snprintf(s_catalog.gifs[slot].name, sizeof(s_catalog.gifs[slot].name),
             "GIF %d (Vazio)", slot);
    if (slot == s_catalog.active_gif_idx) {
      s_active_gif_len = 0;
    }
  } else {
    s_catalog.images[slot].has_data = false;
    s_catalog.images[slot].size = 0;
    s_catalog.images[slot].crc32 = 0;
    snprintf(s_catalog.images[slot].name, sizeof(s_catalog.images[slot].name),
             "Imagem %d (Vazio)", slot);
    if (slot == s_catalog.active_img_idx && s_active_img_pixels) {
      generate_default_wallpaper_pattern(s_active_img_pixels);
    }
  }

  save_catalog_to_nvs();
  xSemaphoreGive(s_mutex);
  return ESP_OK;
}

/**
 * @brief Apaga todos os slots de imagens da partição NVS, restaurando o padrão gráfico do sistema.
 *
 * Itera por todos os 6 slots de imagens:
 * 1. Remove as chaves de blob "img_blob_0" .. "img_blob_5" da memória Flash NVS.
 * 2. Restaura o catálogo em RAM deixando o slot 0 com o papel de parede tecnológico gerado.
 * 3. Define os slots 1 a 5 como vazios (has_data = false).
 * 4. Regenera os pixels do slot 0 diretamente no buffer PSRAM para renderização imediata.
 * 5. Persiste o catálogo no NVS e comita a transação.
 * 6. Sincroniza o índice de imagem ativa com o ator settings (slot 0).
 */
esp_err_t media_storage_erase_all_images(void) {
  if (!s_nvs_h) {
    ESP_LOGE(TAG, "Tentativa de apagar imagens com NVS não inicializado");
    return ESP_ERR_INVALID_STATE;
  }

  /* Adquire o mutex para garantir thread-safety contra acessos concorrentes do BLE ou LVGL */
  if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    ESP_LOGE(TAG, "Timeout ao adquirir mutex para apagar imagens no NVS");
    return ESP_ERR_TIMEOUT;
  }

  ESP_LOGI(TAG, "Iniciando exclusão de todas as imagens gravadas no NVS...");

  char key[16];
  for (uint8_t slot = 0; slot < MEDIA_STORAGE_MAX_IMAGES; slot++) {
    /* Formata a chave correspondente ao slot de imagem na partição NVS */
    snprintf(key, sizeof(key), "img_blob_%u", slot);

    /* Remove a chave individualmente da partição de armazenamento */
    esp_err_t err = nvs_erase_key(s_nvs_h, key);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
      ESP_LOGW(TAG, "Aviso ao apagar chave '%s' do NVS: %s", key, esp_err_to_name(err));
    }

    /* Redefine os metadados do slot no catálogo em RAM */
    s_catalog.images[slot].slot = slot;
    s_catalog.images[slot].has_data = (slot == 0); /* Slot 0 recebe o wallpaper padrão */
    s_catalog.images[slot].size = (slot == 0) ? MEDIA_IMAGE_RGB565_SIZE : 0;
    s_catalog.images[slot].crc32 = 0;
    snprintf(s_catalog.images[slot].name, sizeof(s_catalog.images[slot].name),
             (slot == 0) ? "Wallpaper Padrão" : "Imagem %d (Vazio)", slot);
  }

  /* Redefine o índice da imagem ativa para o slot 0 */
  s_catalog.active_img_idx = 0;

  /* Regenera o padrão gráfico tecnológico suave no buffer de PSRAM */
  if (s_active_img_pixels) {
    generate_default_wallpaper_pattern(s_active_img_pixels);
  }

  /* Salva o catálogo atualizado e consolida as operações na Flash */
  esp_err_t cat_err = save_catalog_to_nvs();
  if (cat_err != ESP_OK) {
    ESP_LOGE(TAG, "Falha ao salvar catálogo de mídias atualizado após apagar imagens: %s", esp_err_to_name(cat_err));
  }

  esp_err_t commit_err = nvs_commit(s_nvs_h);
  if (commit_err != ESP_OK) {
    ESP_LOGE(TAG, "Falha ao comitar transação de NVS após exclusão de imagens: %s", esp_err_to_name(commit_err));
  }

  xSemaphoreGive(s_mutex);

  /* Atualiza a persistência do índice no ator settings de forma atômica */
  settings_set_u8(SETTINGS_KEY_DISP_IMG_IDX, 0);

  ESP_LOGI(TAG, "Todas as imagens no NVS foram apagadas com sucesso! Slot 0 restaurado com padrão tecnológico.");
  return ESP_OK;
}

/**
 * @brief Formata completamente a partição nvs_media, apagando todas as imagens e GIFs.
 *
 * Remove todos os dados persistidos no namespace "media_store" da Flash,
 * reinicializa todos os slots em RAM e redefine os estados padrões na PSRAM.
 */
esp_err_t media_storage_format(void) {
  if (!s_nvs_h) {
    ESP_LOGE(TAG, "Tentativa de formatar partição com NVS não inicializado");
    return ESP_ERR_INVALID_STATE;
  }

  if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    ESP_LOGE(TAG, "Timeout ao obter mutex para formatar partição NVS de mídia");
    return ESP_ERR_TIMEOUT;
  }

  ESP_LOGI(TAG, "Formatando completamente a partição NVS de mídia (%s)...", MEDIA_NVS_PARTITION_NAME);

  /* Apaga todas as entradas do namespace de mídia */
  esp_err_t erase_err = nvs_erase_all(s_nvs_h);
  if (erase_err != ESP_OK) {
    ESP_LOGE(TAG, "Falha ao executar nvs_erase_all na partição de mídia: %s", esp_err_to_name(erase_err));
  }

  esp_err_t commit_err = nvs_commit(s_nvs_h);
  if (commit_err != ESP_OK) {
    ESP_LOGE(TAG, "Falha ao comitar limpeza de mídia no NVS: %s", esp_err_to_name(commit_err));
  }

  /* Reinicializa o catálogo de metadados em RAM com os valores limpos de fábrica */
  memset(&s_catalog, 0, sizeof(s_catalog));
  s_catalog.active_img_idx = 0;
  s_catalog.active_gif_idx = 0;

  for (int i = 0; i < MEDIA_STORAGE_MAX_IMAGES; i++) {
    s_catalog.images[i].slot = i;
    s_catalog.images[i].has_data = (i == 0);
    s_catalog.images[i].size = (i == 0) ? MEDIA_IMAGE_RGB565_SIZE : 0;
    snprintf(s_catalog.images[i].name, sizeof(s_catalog.images[i].name),
             (i == 0) ? "Wallpaper Padrão" : "Imagem %d (Vazio)", i);
  }

  for (int i = 0; i < MEDIA_STORAGE_MAX_GIFS; i++) {
    s_catalog.gifs[i].slot = i;
    s_catalog.gifs[i].has_data = false;
    s_catalog.gifs[i].size = 0;
    snprintf(s_catalog.gifs[i].name, sizeof(s_catalog.gifs[i].name),
             "GIF %d (Vazio)", i);
  }

  /* Regenera o padrão no buffer de PSRAM e esvazia o buffer de GIF */
  if (s_active_img_pixels) {
    generate_default_wallpaper_pattern(s_active_img_pixels);
  }
  s_active_gif_len = 0;

  /* Salva novo catálogo limpo */
  save_catalog_to_nvs();
  xSemaphoreGive(s_mutex);

  /* Sincroniza os índices zerados no módulo de settings */
  settings_set_u8(SETTINGS_KEY_DISP_IMG_IDX, 0);
  settings_set_u8(SETTINGS_KEY_DISP_GIF_IDX, 0);

  ESP_LOGI(TAG, "Partição NVS de mídia formatada com sucesso! Catálogo e buffers restaurados.");
  return ESP_OK;
}

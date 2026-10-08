/**
 * @file ui.c
 */

/*********************
 *      INCLUDES
 *********************/

#include "ui.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/*
 * Símbolos das fontes binárias completas geradas pelo LVGL Pro e embutidas na
 * Flash via CMake EMBED_FILES. Essas fontes contêm a faixa Unicode Latin-1
 * (0xA0-0xFF, Á, É, Í, Ó, Ú, ç, ã, õ, etc.) essencial para a correta exibição
 * do idioma Português!
 */
extern const uint8_t _binary_font_montserrat_regular_12_bin_start[];
extern const uint8_t _binary_font_montserrat_regular_12_bin_end[];
extern const uint8_t _binary_font_montserrat_regular_14_bin_start[];
extern const uint8_t _binary_font_montserrat_regular_14_bin_end[];
extern const uint8_t _binary_font_montserrat_regular_16_bin_start[];
extern const uint8_t _binary_font_montserrat_regular_16_bin_end[];
extern const uint8_t _binary_font_montserrat_regular_20_bin_start[];
extern const uint8_t _binary_font_montserrat_regular_20_bin_end[];
extern const uint8_t _binary_font_montserrat_regular_24_bin_start[];
extern const uint8_t _binary_font_montserrat_regular_24_bin_end[];

/**
 * @brief Helper para carregar com segurança uma fonte de buffer em memória
 * Flash.
 *
 * Se a fonte for carregada com sucesso a partir dos bytes embutidos, ela trará
 * todos os glifos acentuados do português. Se por qualquer motivo falhar,
 * utiliza a fonte padrão nativa de fallback para evitar que a aplicação trave.
 *
 * @param start Ponteiro inicial do buffer binário da fonte na Flash
 * @param end Ponteiro final do buffer binário da fonte na Flash
 * @param fallback_font Fonte nativa do LVGL usada como garantia
 * @param name Nome da fonte para fins de log
 * @return lv_font_t* Ponteiro válido para a fonte
 */
static lv_font_t *
load_font_with_portuguese_support(const uint8_t *start, const uint8_t *end,
                                  const lv_font_t *fallback_font,
                                  const char *name) {
  lv_font_t *font = NULL;
  if (start && end && (end > start)) {
    uint32_t size = (uint32_t)(end - start);
    font = lv_binfont_create_from_buffer((void *)start, size);
  }

  if (!font) {
    LV_LOG_WARN("Fonte '%s' embutida nao carregada. Usando fallback nativo.",
                name);
    font = (lv_font_t *)fallback_font;
  } else {
    LV_LOG_INFO("Fonte '%s' com suporte a acentos (PT-BR) carregada da Flash.",
                name);
  }
  return font;
}

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

/**
 * @brief Inicializa a biblioteca de interface gráfica e associa as fontes com
 * acentuação.
 *
 * Para suportar acentuação em Português (Á, É, Í, Ó, Ú, ç, ã, õ, etc.),
 * carregamos as fontes binárias embutidas que possuem a faixa Latin-1
 * (0xA0-0xFF). Caso alguma não esteja disponível, usa a fonte nativa compilada
 * no firmware.
 *
 * @param asset_path Caminho base dos assets (não utilizado quando embutido em
 * Flash)
 */
void ui_init(const char *asset_path) {
  LV_LOG("Initializing custom C code using LVGL v%d.%d.%d", LVGL_VERSION_MAJOR,
         LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);

  /* Carrega cada fonte a partir da memória Flash com suporte aos caracteres do
   * Português */
  font_montserrat_regular_12 = load_font_with_portuguese_support(
      _binary_font_montserrat_regular_12_bin_start,
      _binary_font_montserrat_regular_12_bin_end, &lv_font_montserrat_12,
      "font_montserrat_regular_12");

  font_montserrat_regular_14 = load_font_with_portuguese_support(
      _binary_font_montserrat_regular_14_bin_start,
      _binary_font_montserrat_regular_14_bin_end, &lv_font_montserrat_14,
      "font_montserrat_regular_14");

  font_montserrat_regular_16 = load_font_with_portuguese_support(
      _binary_font_montserrat_regular_16_bin_start,
      _binary_font_montserrat_regular_16_bin_end, &lv_font_montserrat_16,
      "font_montserrat_regular_16");

  font_montserrat_regular_20 = load_font_with_portuguese_support(
      _binary_font_montserrat_regular_20_bin_start,
      _binary_font_montserrat_regular_20_bin_end, &lv_font_montserrat_20,
      "font_montserrat_regular_20");

  font_montserrat_regular_24 = load_font_with_portuguese_support(
      _binary_font_montserrat_regular_24_bin_start,
      _binary_font_montserrat_regular_24_bin_end, &lv_font_montserrat_24,
      "font_montserrat_regular_24");

  /* Inicialização dos estilos, temas e bindings gerados automaticamente */
  ui_init_gen(asset_path);

  /* Garantia de fallback seguro para evitar qualquer ponteiro nulo */
  if (!font_montserrat_regular_12)
    font_montserrat_regular_12 = (lv_font_t *)LV_FONT_DEFAULT;
  if (!font_montserrat_regular_14)
    font_montserrat_regular_14 = (lv_font_t *)LV_FONT_DEFAULT;
  if (!font_montserrat_regular_16)
    font_montserrat_regular_16 = (lv_font_t *)LV_FONT_DEFAULT;
  if (!font_montserrat_regular_20)
    font_montserrat_regular_20 = (lv_font_t *)LV_FONT_DEFAULT;
  if (!font_montserrat_regular_24)
    font_montserrat_regular_24 = (lv_font_t *)LV_FONT_DEFAULT;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/
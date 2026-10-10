/**
 * @file ui_gen.h
 */

#ifndef LVGL_PRO_UI_GEN_H
#define LVGL_PRO_UI_GEN_H

#ifndef UI_SUBJECT_STRING_LENGTH
#define UI_SUBJECT_STRING_LENGTH 256
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
    #include "lvgl.h"
    #include "lvgl_private.h"
#else
    #include "lvgl/lvgl.h"
    #include "lvgl/lvgl_private.h"
#endif

#if defined(LV_USE_XML) && LV_USE_XML
    #include "lv_xml/lv_xml.h"
#endif



/* Prototypes for target functions, needed by responsive const definitions */

void ui_set_target(uint32_t target);
uint32_t ui_get_target(void);
bool ui_check_target(uint32_t target);

/*********************
 *      DEFINES
 *********************/

#define UI_TARGET_UNDEFINED  (0 << 1)
#define UI_TARGET_TARGET1    (1 << 1)
#define UI_TARGET_ALL        0x0FFFFFFF

/* By default compile for all targets, allowing to switch to any targets at runtime */
#ifndef UI_COMPILE_TARGET
#define UI_COMPILE_TARGET UI_TARGET_ALL
#endif

#define UI_CHECK_COMPILE_TARGET(target) (UI_COMPILE_TARGET & (target) ? 1 : 0)

#define CONST_BATERIA_STATUS_TEXTO_GLOBAL "85% (3.95 V)"
#define CONST_BATERIA_PORCENTAGEM_GLOBAL 85
#define CONST_BATERIA_TENSAO_MV_GLOBAL 3950
#define CONST_BLUETOOTH_NOME_DISPOSITIVO_1_GLOBAL "dispositivo_1"
#define CONST_BLUETOOTH_STATUS_DISPOSITIVO_1_GLOBAL "Desconectado"
#define CONST_BLUETOOTH_DESCONECTAR_1_GLOBAL 0
#define CONST_BLUETOOTH_NOME_DISPOSITIVO_2_GLOBAL "dispositivo_2"
#define CONST_BLUETOOTH_STATUS_DISPOSITIVO_2_GLOBAL "Desconectado"
#define CONST_BLUETOOTH_DESCONECTAR_2_GLOBAL 0
#define CONST_BLUETOOTH_STATUS_ALTERNANCIA_GLOBAL "Ativo (Dispositivo 1)"
#define CONST_BLUETOOTH_ALTERNANCIA_ATIVA_GLOBAL 1
#define CONST_PROXIMIDADE_STATUS_TEXTO_GLOBAL "Fone no ouvido"
#define CONST_PROXIMIDADE_ATIVO_GLOBAL 1
#define CONST_PROXIMIDADE_SENSIBILIDADE_GLOBAL 100
#define CONST_VIBRACALL_STATUS_TEXTO_GLOBAL "Ativo (Inativo)"
#define CONST_VIBRACALL_ATIVO_GLOBAL 1
#define CONST_VIBRACALL_INTENSIDADE_GLOBAL 70
#define CONST_ORELHAS_STATUS_TEXTO_GLOBAL "Ativo (Expressivo)"
#define CONST_ORELHAS_ATIVO_GLOBAL 1
#define CONST_ORELHAS_ANGULO_MAXIMO_GLOBAL 120
#define CONST_DISPLAY_LIGADO_GLOBAL 1
#define CONST_DISPLAY_BRILHO_GLOBAL 80
#define CONST_DISPLAY_TIMEOUT_SEGUNDOS_GLOBAL 30
#define CONST_DISPLAY_IMAGEM_PRINCIPAL_NOME_GLOBAL "img_padrao_01"
#define CONST_DISPLAY_GIF_PRINCIPAL_NOME_GLOBAL "gif_idle_01"
#define CONST_BASELIB_ACCENT_GLOBAL lv_color_hex(0x70609C)
#define CONST_BASELIB_ACCENT_TEXT_GLOBAL lv_color_hex(0xFFFFFF)
#define CONST_BASELIB_RADIUS_GLOBAL 8
#define CONST_BASELIB_UNIT_SM_GLOBAL 4


#ifndef LV_XML_EVAL_STRING_BUF_SIZE
    #define LV_XML_EVAL_STRING_BUF_SIZE 256
#endif

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 * GLOBAL VARIABLES
 **********************/

/*-------------------
 * Permanent screens
 *------------------*/

/*----------------
 * Global styles
 *----------------*/

extern lv_style_t style_screen_base_global;

/*----------------
 * Fonts
 *----------------*/

/* Targets: any */
extern lv_font_t * font_montserrat_regular_12;
extern lv_font_t * font_montserrat_regular_16;
extern lv_font_t * font_montserrat_regular_14;
extern lv_font_t * font_montserrat_regular_20;
extern lv_font_t * font_montserrat_regular_24;


/*----------------
 * Images
 *----------------*/



/*----------------
 * Subjects
 *----------------*/

extern lv_subject_t subject_g_subj_bateria_tensao_mv;
extern lv_subject_t subject_g_subj_bt_active_slot;
extern lv_subject_t subject_g_subj_bt_alternancia_ativa;
extern lv_subject_t subject_g_subj_bt_nome_1;
extern lv_subject_t subject_g_subj_bt_nome_2;
extern lv_subject_t subject_g_subj_bt_status_1;
extern lv_subject_t subject_g_subj_bt_status_2;
extern lv_subject_t subject_g_subj_bt_status_alternancia;
extern lv_subject_t subject_g_subj_display_brilho;
extern lv_subject_t subject_g_subj_display_ligado;
extern lv_subject_t subject_g_subj_display_timeout_segundos;
extern lv_subject_t subject_g_subj_orelhas_angulo_maximo;
extern lv_subject_t subject_g_subj_orelhas_status_texto;
extern lv_subject_t subject_g_subj_proximidade_sensibilidade;
extern lv_subject_t subject_g_subj_proximidade_status_texto;
extern lv_subject_t subject_g_subj_vibracall_intensidade;
extern lv_subject_t subject_g_subj_vibracall_status_texto;
extern lv_subject_t subject_subj_battery_percent;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/*----------------
 * Event Callbacks
 *----------------*/

/**
 * Initialize the component library
 */

void ui_init_gen(const char * asset_path);

/**********************
 *      MACROS
 **********************/

/**********************
 *   POST INCLUDES
 **********************/

/*Include all the widgets, components and screens of this library*/
#include "components/base/button_subtle/button_subtle_gen.h"
#include "components/base/button/button_gen.h"
#include "components/base/card/card_gen.h"
#include "components/base/column/column_gen.h"
#include "components/base/container/container_gen.h"
#include "components/base/label/label_gen.h"
#include "components/base/panel/panel_gen.h"
#include "components/base/row/row_gen.h"
#include "components/base/slider/slider_gen.h"
#include "components/cont_slider_toque_b/cont_slider_toque_b_gen.h"
#include "components/cont_slider_toque_c/cont_slider_toque_c_gen.h"
#include "components/cont_slider_toque_d/cont_slider_toque_d_gen.h"
#include "components/cont_slider_toque_e/cont_slider_toque_e_gen.h"
#include "components/cont_slider_toque/cont_slider_toque_gen.h"
#include "components/derived/tile_2/tile_2_gen.h"
#include "components/derived/tile_3/tile_3_gen.h"
#include "components/derived/tile_4/tile_4_gen.h"
#include "components/derived/tile_5/tile_5_gen.h"
#include "components/derived/tile/tile_gen.h"
#include "screens/screen_apps_gen.h"
#include "screens/screen_bluetooth_gen.h"
#include "screens/screen_display_gen.h"
#include "screens/screen_galeria_gifs_gen.h"
#include "screens/screen_galeria_imagens_gen.h"
#include "screens/screen_main_gen.h"
#include "screens/screen_main_gif_gen.h"
#include "screens/screen_orelhas_gen.h"
#include "screens/screen_proximidade_gen.h"
#include "screens/screen_settings_gen.h"
#include "screens/screen_status_gen.h"
#include "screens/screen_vibracall_gen.h"

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*LVGL_PRO_UI_GEN_H*/
/**
 * @file testeFinal_gen.h
 */

#ifndef LVGL_PRO_TESTEFINAL_GEN_H
#define LVGL_PRO_TESTEFINAL_GEN_H

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

void testeFinal_set_target(uint32_t target);
uint32_t testeFinal_get_target(void);
bool testeFinal_check_target(uint32_t target);

/*********************
 *      DEFINES
 *********************/

#define TESTEFINAL_TARGET_UNDEFINED  (0 << 1)
#define TESTEFINAL_TARGET_TARGET1    (1 << 1)
#define TESTEFINAL_TARGET_ALL        0x0FFFFFFF

/* By default compile for all targets, allowing to switch to any targets at runtime */
#ifndef TESTEFINAL_COMPILE_TARGET
#define TESTEFINAL_COMPILE_TARGET TESTEFINAL_TARGET_ALL
#endif

#define TESTEFINAL_CHECK_COMPILE_TARGET(target) (TESTEFINAL_COMPILE_TARGET & (target) ? 1 : 0)

#define CONST_BASELIB_ACCENT_GLOBAL lv_color_hex(0x70609C)
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
extern lv_font_t * font_inter_bold_7;
extern lv_font_t * font_inter_bold_6_5;
extern lv_font_t * font_inter_extra_bold_11_5;
extern lv_font_t * font_inter_regular_8;
extern lv_font_t * font_inter_bold_8_5;


/*----------------
 * Images
 *----------------*/

/* Targets: any */
extern const void * image_linkedin_image_7_1;
extern const void * image_headphones_1109;
extern const void * image_cpu_1111;
extern const void * image_bluetooth_1115;
extern const void * image_battery_1105;
extern const void * image_battery_charging_1113;

/*----------------
 * Subjects
 *----------------*/

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/*----------------
 * Event Callbacks
 *----------------*/

/**
 * Initialize the component library
 */

void testeFinal_init_gen(const char * asset_path);

/**********************
 *      MACROS
 **********************/

/**********************
 *   POST INCLUDES
 **********************/

/*Include all the widgets, components and screens of this library*/
#include "components/base/card/card_gen.h"
#include "components/base/container/container_gen.h"
#include "components/base/image/image_gen.h"
#include "components/base/panel/panel_gen.h"
#include "components/battery_charging/battery_charging_gen.h"
#include "components/battery/battery_gen.h"
#include "components/bluetooth/bluetooth_gen.h"
#include "components/cpu/cpu_gen.h"
#include "components/headphones/headphones_gen.h"
#include "screens/app_gen.h"

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*LVGL_PRO_TESTEFINAL_GEN_H*/
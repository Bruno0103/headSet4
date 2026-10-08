/**
 * @file button_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "button_gen.h"
#include "../../../ui.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/***********************
 *  STATIC VARIABLES
 **********************/

/***********************
 *  STATIC PROTOTYPES
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

lv_obj_t * button_create(lv_obj_t * parent)
{
    LV_TRACE_OBJ_CREATE("begin");

    static lv_style_t style_filled;

    static bool style_inited = false;

    if (!style_inited) {
        /*Init all styles*/
        lv_style_init(&style_filled);

        lv_style_set_bg_color(&style_filled, CONST_BASELIB_ACCENT_GLOBAL);
        lv_style_set_bg_opa(&style_filled, (255 * 100 / 100));
        lv_style_set_text_color(&style_filled, CONST_BASELIB_ACCENT_TEXT_GLOBAL);
        lv_style_set_radius(&style_filled, CONST_BASELIB_RADIUS_GLOBAL);

        style_inited = true;
    }


    lv_obj_t * the_root = NULL;

    #if UI_CHECK_COMPILE_TARGET(UI_TARGET_ALL)
    if (ui_check_target(UI_TARGET_ALL)) {
        lv_obj_t * button_subtle_0 = button_subtle_create(parent);
        lv_obj_set_name_static(button_subtle_0, "button_#");

        lv_obj_add_style(button_subtle_0, &style_filled, 0);

        the_root = button_subtle_0;
    }
    #endif

    LV_TRACE_OBJ_CREATE("finished");

    return the_root;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/


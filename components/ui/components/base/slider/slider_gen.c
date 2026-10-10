/**
 * @file slider_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "slider_gen.h"
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

lv_obj_t * slider_create(lv_obj_t * parent)
{
    LV_TRACE_OBJ_CREATE("begin");

    static lv_style_t style_main;
    static lv_style_t style_indicator;
    static lv_style_t style_knob;

    static bool style_inited = false;

    if (!style_inited) {
        /*Init all styles*/
        lv_style_init(&style_main);
        lv_style_init(&style_indicator);
        lv_style_init(&style_knob);

        lv_style_set_bg_color(&style_main, CONST_BASELIB_ACCENT_GLOBAL);
        lv_style_set_bg_opa(&style_main, (255 * 16 / 100));
        lv_style_set_radius(&style_main, CONST_BASELIB_RADIUS_GLOBAL);
        lv_style_set_bg_color(&style_indicator, CONST_BASELIB_ACCENT_GLOBAL);
        lv_style_set_bg_opa(&style_indicator, (255 * 100 / 100));
        lv_style_set_radius(&style_indicator, CONST_BASELIB_RADIUS_GLOBAL);
        lv_style_set_bg_color(&style_knob, CONST_BASELIB_ACCENT_TEXT_GLOBAL);
        lv_style_set_bg_opa(&style_knob, (255 * 100 / 100));
        lv_style_set_radius(&style_knob, 32767);

        style_inited = true;
    }


    lv_obj_t * the_root = NULL;

    #if UI_CHECK_COMPILE_TARGET(UI_TARGET_ALL)
    if (ui_check_target(UI_TARGET_ALL)) {
        lv_obj_t * lv_slider_0 = lv_slider_create(parent);
        lv_obj_set_name_static(lv_slider_0, "slider_#");

        lv_obj_remove_style_all(lv_slider_0);
        lv_obj_add_style(lv_slider_0, &style_main, 0);
        lv_obj_add_style(lv_slider_0, &style_indicator, LV_PART_INDICATOR);
        lv_obj_add_style(lv_slider_0, &style_knob, LV_PART_KNOB);

        the_root = lv_slider_0;
    }
    #endif

    LV_TRACE_OBJ_CREATE("finished");

    return the_root;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/


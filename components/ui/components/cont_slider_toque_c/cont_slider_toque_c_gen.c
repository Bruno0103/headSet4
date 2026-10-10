/**
 * @file cont_slider_toque_c_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "cont_slider_toque_c_gen.h"
#include "../../ui.h"

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

lv_obj_t * cont_slider_toque_c_create(lv_obj_t * parent, lv_subject_t * value)
{
    LV_TRACE_OBJ_CREATE("begin");

    static lv_style_t style_base;

    static bool style_inited = false;

    if (!style_inited) {
        /*Init all styles*/
        lv_style_init(&style_base);

        lv_style_set_bg_color(&style_base, lv_color_hex(0x252D35));
        lv_style_set_radius(&style_base, 4);

        style_inited = true;
    }


    lv_obj_t * the_root = NULL;

    #if UI_CHECK_COMPILE_TARGET(UI_TARGET_ALL)
    if (ui_check_target(UI_TARGET_ALL)) {
        lv_obj_t * slider_0 = slider_create(parent);
        lv_obj_set_name_static(slider_0, "cont_slider_toque_c_#");
        lv_obj_set_width(slider_0, 216);
        lv_obj_set_height(slider_0, 12);
        lv_slider_set_min_value(slider_0, 10);
        lv_slider_set_max_value(slider_0, 200);
        lv_slider_bind_value(slider_0, value);

        lv_obj_add_style(slider_0, &style_base, 0);

        the_root = slider_0;
    }
    #endif

    LV_TRACE_OBJ_CREATE("finished");

    return the_root;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/


/**
 * @file tile_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "tile_gen.h"
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

lv_obj_t * tile_create(lv_obj_t * parent, lv_flex_align_t main_align, lv_flex_align_t cross_align, bool scrollable)
{
    LV_TRACE_OBJ_CREATE("begin");

    static lv_style_t style_base;

    static bool style_inited = false;

    if (!style_inited) {
        /*Init all styles*/
        lv_style_init(&style_base);

        lv_style_set_layout(&style_base, LV_LAYOUT_FLEX);
        lv_style_set_flex_flow(&style_base, LV_FLEX_FLOW_ROW);
        lv_style_set_bg_color(&style_base, lv_color_hex(0x252D35));
        lv_style_set_bg_opa(&style_base, 255);
        lv_style_set_radius(&style_base, 4);
        lv_style_set_border_width(&style_base, 0);
        lv_style_set_pad_all(&style_base, 0);
        lv_style_set_pad_left(&style_base, 8);
        lv_style_set_pad_right(&style_base, 8);
        lv_style_set_pad_row(&style_base, 0);
        lv_style_set_pad_column(&style_base, 0);

        style_inited = true;
    }


    lv_obj_t * the_root = NULL;

    #if UI_CHECK_COMPILE_TARGET(UI_TARGET_ALL)
    if (ui_check_target(UI_TARGET_ALL)) {
        lv_obj_t * lv_obj_0 = lv_obj_create(parent);
        lv_obj_set_name_static(lv_obj_0, "tile_#");
        lv_obj_set_flag(lv_obj_0, LV_OBJ_FLAG_SCROLLABLE, scrollable);
        lv_obj_set_style_flex_main_place(lv_obj_0, main_align, 0);
        lv_obj_set_style_flex_cross_place(lv_obj_0, cross_align, 0);
        lv_obj_set_style_flex_track_place(lv_obj_0, cross_align, 0);

        lv_obj_add_style(lv_obj_0, &style_base, 0);

        the_root = lv_obj_0;
    }
    #endif

    LV_TRACE_OBJ_CREATE("finished");

    return the_root;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/


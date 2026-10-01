/**
 * @file app_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "app_gen.h"
#include "../testeFinal.h"

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

lv_obj_t * app_create(void)
{
    LV_TRACE_OBJ_CREATE("begin");


    lv_obj_t * the_root = NULL;

    #if TESTEFINAL_CHECK_COMPILE_TARGET(TESTEFINAL_TARGET_ALL)
    if (testeFinal_check_target(TESTEFINAL_TARGET_ALL)) {
        lv_obj_t * lv_obj_0 = lv_obj_create(NULL);
        lv_obj_set_name_static(lv_obj_0, "app_#");
        lv_obj_set_flag(lv_obj_0, LV_OBJ_FLAG_SCROLLABLE, false);
        lv_obj_set_style_bg_opa(lv_obj_0, 0, 0);
        lv_obj_set_style_pad_all(lv_obj_0, 0, 0);

        lv_obj_add_style(lv_obj_0, &style_screen_base_global, 0);
        lv_obj_t * container_0 = container_create(lv_obj_0);
        lv_obj_set_align(container_0, LV_ALIGN_BOTTOM_MID);
        lv_obj_set_x(container_0, 0);
        lv_obj_set_y(container_0, 2);
        lv_obj_set_width(container_0, 128);
        lv_obj_set_height(container_0, 144);
        lv_obj_t * image_0 = image_create(container_0);
        lv_obj_set_x(image_0, 0);
        lv_obj_set_y(image_0, 0);
        lv_obj_set_width(image_0, 128);
        lv_obj_set_height(image_0, 144);
        lv_image_set_src(image_0, image_linkedin_image_7_1);

        lv_obj_t * card_0 = card_create(lv_obj_0);
        lv_obj_set_flex_flow(card_0, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_flex_main_place(card_0, LV_FLEX_ALIGN_CENTER, 0);
        lv_obj_set_style_flex_cross_place(card_0, LV_FLEX_ALIGN_CENTER, 0);
        lv_obj_set_style_flex_track_place(card_0, LV_FLEX_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_hor(card_0, 4, 0);
        lv_obj_set_style_pad_ver(card_0, 0, 0);
        lv_obj_set_style_pad_column(card_0, 4, 0);
        lv_obj_set_align(card_0, LV_ALIGN_TOP_MID);
        lv_obj_set_x(card_0, 0);
        lv_obj_set_y(card_0, 0);
        lv_obj_set_width(card_0, 128);
        lv_obj_set_height(card_0, 18);
        lv_obj_set_style_bg_color(card_0, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_opa(card_0, 4, 0);
        lv_obj_set_style_border_color(card_0, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_width(card_0, 1, 0);
        lv_obj_set_style_radius(card_0, 5, 0);
        headphones_create(card_0);

        cpu_create(card_0);

        bluetooth_create(card_0);

        lv_obj_t * container_1 = container_create(card_0);
        lv_obj_set_width(container_1, 16);
        lv_obj_set_height(container_1, 16);
        lv_obj_t * battery_0 = battery_create(container_1);
        lv_obj_set_x(battery_0, 0);
        lv_obj_set_y(battery_0, 0);

        lv_obj_t * battery_charging_0 = battery_charging_create(container_1);
        lv_obj_set_x(battery_charging_0, 0);
        lv_obj_set_y(battery_charging_0, 0);

        the_root = lv_obj_0;
    }
    #endif

    LV_TRACE_OBJ_CREATE("finished");

    return the_root;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/


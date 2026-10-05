/**
 * @file bluetooth_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "bluetooth_gen.h"
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

lv_obj_t * bluetooth_create(lv_obj_t * parent)
{
    LV_TRACE_OBJ_CREATE("begin");


    lv_obj_t * the_root = NULL;

    #if ui_CHECK_COMPILE_TARGET(ui_TARGET_ALL)
    if (ui_check_target(ui_TARGET_ALL)) {
        lv_obj_t * image_0 = image_create(parent);
        lv_obj_set_name_static(image_0, "bluetooth_#");
        lv_obj_set_width(image_0, 16);
        lv_obj_set_height(image_0, 16);
        lv_image_set_src(image_0, image_bluetooth_1115);
        lv_obj_set_flag(image_0, LV_OBJ_FLAG_SCROLLABLE, false);

        the_root = image_0;
    }
    #endif

    LV_TRACE_OBJ_CREATE("finished");

    return the_root;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/


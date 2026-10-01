/**
 * @file card_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "card_gen.h"
#include "../../../testeFinal.h"

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

lv_obj_t * card_create(lv_obj_t * parent)
{
    LV_TRACE_OBJ_CREATE("begin");

    static lv_style_t style_card;

    static bool style_inited = false;

    if (!style_inited) {
        /*Init all styles*/
        lv_style_init(&style_card);

        lv_style_set_clip_corner(&style_card, true);

        style_inited = true;
    }


    lv_obj_t * the_root = NULL;

    #if TESTEFINAL_CHECK_COMPILE_TARGET(TESTEFINAL_TARGET_ALL)
    if (testeFinal_check_target(TESTEFINAL_TARGET_ALL)) {
        lv_obj_t * panel_0 = panel_create(parent);
        lv_obj_set_name_static(panel_0, "card_#");

        lv_obj_add_style(panel_0, &style_card, 0);

        the_root = panel_0;
    }
    #endif

    LV_TRACE_OBJ_CREATE("finished");

    return the_root;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/


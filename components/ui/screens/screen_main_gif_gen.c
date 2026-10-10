/**
 * @file screen_main_gif_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "screen_main_gif_gen.h"
#include "../ui.h"

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

lv_obj_t * screen_main_gif_create(void)
{
    LV_TRACE_OBJ_CREATE("begin");


    lv_obj_t * the_root = NULL;

    #if UI_CHECK_COMPILE_TARGET(UI_TARGET_ALL)
    if (ui_check_target(UI_TARGET_ALL)) {
        lv_obj_t * lv_obj_0 = lv_obj_create(NULL);
        lv_obj_set_name_static(lv_obj_0, "screen_main_gif_#");
        lv_obj_set_flex_flow(lv_obj_0, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_all(lv_obj_0, 0, 0);
        lv_obj_set_style_pad_row(lv_obj_0, 0, 0);
        lv_obj_set_style_bg_color(lv_obj_0, lv_color_hex(0x101418), 0);
        lv_obj_set_flag(lv_obj_0, LV_OBJ_FLAG_SCROLLABLE, false);

        lv_obj_add_style(lv_obj_0, &style_screen_base_global, 0);
        lv_obj_t * panel_0 = panel_create(lv_obj_0);
        lv_obj_set_flex_flow(panel_0, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_flex_cross_place(panel_0, LV_FLEX_ALIGN_CENTER, 0);
        lv_obj_set_style_flex_track_place(panel_0, LV_FLEX_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_hor(panel_0, 12, 0);
        lv_obj_set_style_pad_ver(panel_0, 4, 0);
        lv_obj_set_style_pad_column(panel_0, 8, 0);
        lv_obj_set_width(panel_0, lv_pct(100));
        lv_obj_set_height(panel_0, 24);
        lv_obj_set_style_bg_color(panel_0, lv_color_hex(0x1A2026), 0);
        lv_obj_t * row_0 = row_create(panel_0, 0, 0, 0, 0, 4, 1, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_width(row_0, 0);
        lv_obj_set_height(row_0, LV_SIZE_CONTENT);
        lv_obj_t * label_0 = label_create(row_0);
        lv_label_set_text(label_0, "BT");
        lv_obj_set_style_text_font(label_0, font_montserrat_regular_12, 0);
        lv_obj_set_style_text_color(label_0, lv_color_hex(0x67D5E8), 0);

        lv_obj_t * label_1 = label_create(row_0);
        lv_label_bind_text(label_1, &subject_g_subj_bt_active_slot, NULL);
        lv_obj_set_style_text_font(label_1, font_montserrat_regular_12, 0);
        lv_obj_set_style_text_color(label_1, lv_color_hex(0x67D5E8), 0);

        lv_obj_t * row_1 = row_create(panel_0, 0, 0, 0, 0, 4, 0, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_width(row_1, LV_SIZE_CONTENT);
        lv_obj_set_height(row_1, LV_SIZE_CONTENT);
        lv_obj_t * label_2 = label_create(row_1);
        lv_label_set_text(label_2, "BAT");
        lv_obj_set_style_text_font(label_2, font_montserrat_regular_12, 0);
        lv_obj_set_style_text_color(label_2, lv_color_hex(0xABB6BF), 0);

        lv_obj_t * label_3 = label_create(row_1);
        lv_label_bind_text(label_3, &subject_subj_battery_percent, "%d%%");
        lv_obj_set_style_text_font(label_3, font_montserrat_regular_12, 0);
        lv_obj_set_style_text_color(label_3, lv_color_hex(0xF2F5F7), 0);

        lv_obj_t * column_0 = column_create(lv_obj_0, 12, 12, 12, 12, 8, 0, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_width(column_0, lv_pct(100));
        lv_obj_set_height(column_0, 296);
        lv_obj_t * button_0 = button_create(column_0);
        lv_obj_set_flex_flow(button_0, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_flex_main_place(button_0, LV_FLEX_ALIGN_CENTER, 0);
        lv_obj_set_style_flex_cross_place(button_0, LV_FLEX_ALIGN_CENTER, 0);
        lv_obj_set_style_flex_track_place(button_0, LV_FLEX_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_all(button_0, 12, 0);
        lv_obj_set_style_pad_row(button_0, 8, 0);
        lv_obj_set_width(button_0, lv_pct(100));
        lv_obj_set_height(button_0, 0);
        lv_obj_set_flex_grow(button_0, 1);
        lv_obj_set_style_bg_color(button_0, lv_color_hex(0x1A2026), 0);
        lv_obj_set_style_border_color(button_0, lv_color_hex(0x39444E), 0);
        lv_obj_set_style_border_width(button_0, 1, 0);
        lv_obj_set_style_radius(button_0, 4, 0);
        lv_obj_set_style_clip_corner(button_0, true, 0);
        lv_obj_t * label_4 = label_create(button_0);
        lv_obj_set_height(label_4, 20);
        lv_label_set_text(label_4, "ESTADO ESTÁTICO");
        lv_obj_set_style_text_font(label_4, font_montserrat_regular_12, 0);
        lv_obj_set_style_text_color(label_4, lv_color_hex(0xABB6BF), 0);
        lv_obj_set_style_pad_top(label_4, 4, 0);

        lv_obj_t * label_5 = label_create(button_0);
        lv_obj_set_height(label_5, 20);
        lv_label_set_text(label_5, "GIF selecionado");
        lv_obj_set_style_text_font(label_5, font_montserrat_regular_16, 0);
        lv_obj_set_style_text_color(label_5, lv_color_hex(0xF2F5F7), 0);
        lv_obj_set_style_text_align(label_5, LV_TEXT_ALIGN_CENTER, 0);

        lv_obj_t * label_6 = label_create(button_0);
        lv_obj_set_width(label_6, lv_pct(100));
        lv_label_set_text(label_6, "Placeholder\nSem recurso fornecido");
        lv_obj_set_style_text_font(label_6, font_montserrat_regular_12, 0);
        lv_obj_set_style_text_line_space(label_6, 4, 0);
        lv_obj_set_style_text_color(label_6, lv_color_hex(0xABB6BF), 0);
        lv_obj_set_style_text_align(label_6, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_top(label_6, 4, 0);

        lv_obj_t * label_7 = label_create(button_0);
        lv_obj_set_height(label_7, 20);
        lv_label_set_text(label_7, "Sem reprodução nesta prancha");
        lv_obj_set_style_text_font(label_7, font_montserrat_regular_12, 0);
        lv_obj_set_style_text_color(label_7, lv_color_hex(0xABB6BF), 0);
        lv_obj_set_style_text_align(label_7, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_top(label_7, 4, 0);

        lv_obj_add_screen_create_event(button_0, LV_EVENT_CLICKED, screen_main_create, LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, 300, 0);

        lv_obj_t * button_1 = button_create(column_0);
        lv_obj_set_flex_flow(button_1, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_flex_main_place(button_1, LV_FLEX_ALIGN_CENTER, 0);
        lv_obj_set_style_flex_cross_place(button_1, LV_FLEX_ALIGN_CENTER, 0);
        lv_obj_set_style_flex_track_place(button_1, LV_FLEX_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_hor(button_1, 8, 0);
        lv_obj_set_style_pad_ver(button_1, 0, 0);
        lv_obj_set_style_pad_column(button_1, 0, 0);
        lv_obj_set_flag(button_1, LV_OBJ_FLAG_IGNORE_LAYOUT, true);
        lv_obj_set_align(button_1, LV_ALIGN_BOTTOM_RIGHT);
        lv_obj_set_x(button_1, 8);
        lv_obj_set_y(button_1, 8);
        lv_obj_set_width(button_1, 64);
        lv_obj_set_height(button_1, 36);
        lv_obj_set_style_bg_color(button_1, lv_color_hex(0x252D35), 0);
        lv_obj_set_style_radius(button_1, 4, 0);
        lv_obj_set_style_clip_corner(button_1, true, 0);
        lv_obj_t * label_8 = label_create(button_1);
        lv_label_set_text(label_8, "Apps");
        lv_obj_set_style_text_font(label_8, font_montserrat_regular_14, 0);
        lv_obj_set_style_text_color(label_8, lv_color_hex(0xF2F5F7), 0);

        lv_obj_add_screen_create_event(button_1, LV_EVENT_CLICKED, screen_apps_create, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 300, 0);

        the_root = lv_obj_0;
    }
    #endif

    LV_TRACE_OBJ_CREATE("finished");

    return the_root;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/


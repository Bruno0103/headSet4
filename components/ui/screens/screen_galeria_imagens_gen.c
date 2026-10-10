/**
 * @file screen_galeria_imagens_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "screen_galeria_imagens_gen.h"
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

lv_obj_t * screen_galeria_imagens_create(void)
{
    LV_TRACE_OBJ_CREATE("begin");


    lv_obj_t * the_root = NULL;

    #if UI_CHECK_COMPILE_TARGET(UI_TARGET_ALL)
    if (ui_check_target(UI_TARGET_ALL)) {
        lv_obj_t * lv_obj_0 = lv_obj_create(NULL);
        lv_obj_set_name_static(lv_obj_0, "screen_galeria_imagens_#");
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

        lv_obj_t * row_2 = row_create(lv_obj_0, 12, 12, 4, 4, 8, 0, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_width(row_2, lv_pct(100));
        lv_obj_set_height(row_2, 44);
        lv_obj_t * button_0 = button_create(row_2);
        lv_obj_set_flex_flow(button_0, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_flex_main_place(button_0, LV_FLEX_ALIGN_CENTER, 0);
        lv_obj_set_style_flex_cross_place(button_0, LV_FLEX_ALIGN_CENTER, 0);
        lv_obj_set_style_flex_track_place(button_0, LV_FLEX_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_hor(button_0, 8, 0);
        lv_obj_set_style_pad_ver(button_0, 0, 0);
        lv_obj_set_style_pad_column(button_0, 0, 0);
        lv_obj_set_width(button_0, 36);
        lv_obj_set_height(button_0, 36);
        lv_obj_set_style_bg_color(button_0, lv_color_hex(0x252D35), 0);
        lv_obj_set_style_radius(button_0, 4, 0);
        lv_obj_set_style_clip_corner(button_0, true, 0);
        lv_obj_t * label_4 = label_create(button_0);
        lv_label_set_text(label_4, "<");
        lv_obj_set_style_text_font(label_4, font_montserrat_regular_14, 0);
        lv_obj_set_style_text_color(label_4, lv_color_hex(0xF2F5F7), 0);

        lv_obj_add_screen_create_event(button_0, LV_EVENT_CLICKED, screen_apps_create, LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, 300, 0);

        lv_obj_t * label_5 = label_create(row_2);
        lv_obj_set_width(label_5, 0);
        lv_obj_set_flex_grow(label_5, 1);
        lv_label_set_text(label_5, "Imagens");
        lv_obj_set_style_text_font(label_5, font_montserrat_regular_20, 0);
        lv_obj_set_style_text_color(label_5, lv_color_hex(0xF2F5F7), 0);

        lv_obj_t * column_0 = column_create(lv_obj_0, 12, 12, 4, 4, 4, 0, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_width(column_0, lv_pct(100));
        lv_obj_set_height(column_0, 252);
        lv_obj_t * row_3 = row_create(column_0, 0, 0, 0, 0, 8, 0, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_width(row_3, lv_pct(100));
        lv_obj_set_height(row_3, 16);
        lv_obj_t * label_6 = label_create(row_3);
        lv_obj_set_width(label_6, 0);
        lv_obj_set_height(label_6, 16);
        lv_obj_set_flex_grow(label_6, 1);
        lv_label_set_text(label_6, "Principal: —");
        lv_obj_set_style_text_font(label_6, font_montserrat_regular_12, 0);
        lv_obj_set_style_text_color(label_6, lv_color_hex(0xABB6BF), 0);

        lv_obj_t * label_7 = label_create(row_3);
        lv_obj_set_height(label_7, 16);
        lv_label_set_text(label_7, "0 / 8");
        lv_obj_set_style_text_font(label_7, font_montserrat_regular_12, 0);
        lv_obj_set_style_text_color(label_7, lv_color_hex(0xABB6BF), 0);

        lv_obj_t * column_1 = column_create(column_0, 0, 0, 0, 0, 0, 0, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_width(column_1, lv_pct(100));
        lv_obj_set_height(column_1, LV_SIZE_CONTENT);
        lv_obj_t * row_4 = row_create(column_1, 8, 8, 0, 0, 8, 0, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_width(row_4, lv_pct(100));
        lv_obj_set_height(row_4, 28);
        lv_obj_t * label_8 = label_create(row_4);
        lv_obj_set_width(label_8, 0);
        lv_obj_set_flex_grow(label_8, 1);
        lv_label_set_text(label_8, "Imagem 0 · sem recurso");
        lv_obj_set_style_text_font(label_8, font_montserrat_regular_12, 0);
        lv_obj_set_style_text_color(label_8, lv_color_hex(0xABB6BF), 0);

        lv_obj_t * label_9 = label_create(row_4);
        lv_label_set_text(label_9, "—");
        lv_obj_set_style_text_font(label_9, font_montserrat_regular_12, 0);
        lv_obj_set_style_text_color(label_9, lv_color_hex(0xABB6BF), 0);

        lv_obj_t * row_5 = row_create(column_1, 8, 8, 0, 0, 8, 0, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_width(row_5, lv_pct(100));
        lv_obj_set_height(row_5, 28);
        lv_obj_t * label_10 = label_create(row_5);
        lv_obj_set_width(label_10, 0);
        lv_obj_set_flex_grow(label_10, 1);
        lv_label_set_text(label_10, "Imagem 1 · sem recurso");
        lv_obj_set_style_text_font(label_10, font_montserrat_regular_12, 0);
        lv_obj_set_style_text_color(label_10, lv_color_hex(0xABB6BF), 0);

        lv_obj_t * label_11 = label_create(row_5);
        lv_label_set_text(label_11, "—");
        lv_obj_set_style_text_font(label_11, font_montserrat_regular_12, 0);
        lv_obj_set_style_text_color(label_11, lv_color_hex(0xABB6BF), 0);

        lv_obj_t * row_6 = row_create(column_1, 8, 8, 0, 0, 8, 0, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_width(row_6, lv_pct(100));
        lv_obj_set_height(row_6, 28);
        lv_obj_t * label_12 = label_create(row_6);
        lv_obj_set_width(label_12, 0);
        lv_obj_set_flex_grow(label_12, 1);
        lv_label_set_text(label_12, "Imagem 2 · sem recurso");
        lv_obj_set_style_text_font(label_12, font_montserrat_regular_12, 0);
        lv_obj_set_style_text_color(label_12, lv_color_hex(0xABB6BF), 0);

        lv_obj_t * label_13 = label_create(row_6);
        lv_label_set_text(label_13, "—");
        lv_obj_set_style_text_font(label_13, font_montserrat_regular_12, 0);
        lv_obj_set_style_text_color(label_13, lv_color_hex(0xABB6BF), 0);

        lv_obj_t * row_7 = row_create(column_1, 8, 8, 0, 0, 8, 0, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_width(row_7, lv_pct(100));
        lv_obj_set_height(row_7, 28);
        lv_obj_t * label_14 = label_create(row_7);
        lv_obj_set_width(label_14, 0);
        lv_obj_set_flex_grow(label_14, 1);
        lv_label_set_text(label_14, "Imagem 3 · sem recurso");
        lv_obj_set_style_text_font(label_14, font_montserrat_regular_12, 0);
        lv_obj_set_style_text_color(label_14, lv_color_hex(0xABB6BF), 0);

        lv_obj_t * label_15 = label_create(row_7);
        lv_label_set_text(label_15, "—");
        lv_obj_set_style_text_font(label_15, font_montserrat_regular_12, 0);
        lv_obj_set_style_text_color(label_15, lv_color_hex(0xABB6BF), 0);

        lv_obj_t * row_8 = row_create(column_1, 8, 8, 0, 0, 8, 0, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_width(row_8, lv_pct(100));
        lv_obj_set_height(row_8, 28);
        lv_obj_t * label_16 = label_create(row_8);
        lv_obj_set_width(label_16, 0);
        lv_obj_set_flex_grow(label_16, 1);
        lv_label_set_text(label_16, "Imagem 4 · sem recurso");
        lv_obj_set_style_text_font(label_16, font_montserrat_regular_12, 0);
        lv_obj_set_style_text_color(label_16, lv_color_hex(0xABB6BF), 0);

        lv_obj_t * label_17 = label_create(row_8);
        lv_label_set_text(label_17, "—");
        lv_obj_set_style_text_font(label_17, font_montserrat_regular_12, 0);
        lv_obj_set_style_text_color(label_17, lv_color_hex(0xABB6BF), 0);

        lv_obj_t * row_9 = row_create(column_1, 8, 8, 0, 0, 8, 0, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_width(row_9, lv_pct(100));
        lv_obj_set_height(row_9, 28);
        lv_obj_t * label_18 = label_create(row_9);
        lv_obj_set_width(label_18, 0);
        lv_obj_set_flex_grow(label_18, 1);
        lv_label_set_text(label_18, "Imagem 5 · sem recurso");
        lv_obj_set_style_text_font(label_18, font_montserrat_regular_12, 0);
        lv_obj_set_style_text_color(label_18, lv_color_hex(0xABB6BF), 0);

        lv_obj_t * label_19 = label_create(row_9);
        lv_label_set_text(label_19, "—");
        lv_obj_set_style_text_font(label_19, font_montserrat_regular_12, 0);
        lv_obj_set_style_text_color(label_19, lv_color_hex(0xABB6BF), 0);

        lv_obj_t * row_10 = row_create(column_1, 8, 8, 0, 0, 8, 0, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_width(row_10, lv_pct(100));
        lv_obj_set_height(row_10, 28);
        lv_obj_t * label_20 = label_create(row_10);
        lv_obj_set_width(label_20, 0);
        lv_obj_set_flex_grow(label_20, 1);
        lv_label_set_text(label_20, "Imagem 6 · sem recurso");
        lv_obj_set_style_text_font(label_20, font_montserrat_regular_12, 0);
        lv_obj_set_style_text_color(label_20, lv_color_hex(0xABB6BF), 0);

        lv_obj_t * label_21 = label_create(row_10);
        lv_label_set_text(label_21, "—");
        lv_obj_set_style_text_font(label_21, font_montserrat_regular_12, 0);
        lv_obj_set_style_text_color(label_21, lv_color_hex(0xABB6BF), 0);

        lv_obj_t * row_11 = row_create(column_1, 8, 8, 0, 0, 8, 0, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_width(row_11, lv_pct(100));
        lv_obj_set_height(row_11, 28);
        lv_obj_t * label_22 = label_create(row_11);
        lv_obj_set_width(label_22, 0);
        lv_obj_set_flex_grow(label_22, 1);
        lv_label_set_text(label_22, "Imagem 7 · sem recurso");
        lv_obj_set_style_text_font(label_22, font_montserrat_regular_12, 0);
        lv_obj_set_style_text_color(label_22, lv_color_hex(0xABB6BF), 0);

        lv_obj_t * label_23 = label_create(row_11);
        lv_label_set_text(label_23, "—");
        lv_obj_set_style_text_font(label_23, font_montserrat_regular_12, 0);
        lv_obj_set_style_text_color(label_23, lv_color_hex(0xABB6BF), 0);

        the_root = lv_obj_0;
    }
    #endif

    LV_TRACE_OBJ_CREATE("finished");

    return the_root;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/


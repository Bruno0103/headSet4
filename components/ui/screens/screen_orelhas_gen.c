/**
 * @file screen_orelhas_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "screen_orelhas_gen.h"
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

lv_obj_t * screen_orelhas_create(void)
{
    LV_TRACE_OBJ_CREATE("begin");


    lv_obj_t * the_root = NULL;

    #if UI_CHECK_COMPILE_TARGET(UI_TARGET_ALL)
    if (ui_check_target(UI_TARGET_ALL)) {
        lv_obj_t * lv_obj_0 = lv_obj_create(NULL);
        lv_obj_set_name_static(lv_obj_0, "screen_orelhas_#");
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

        lv_obj_add_screen_create_event(button_0, LV_EVENT_CLICKED, screen_settings_create, LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, 300, 0);

        lv_obj_t * label_5 = label_create(row_2);
        lv_obj_set_width(label_5, 0);
        lv_obj_set_flex_grow(label_5, 1);
        lv_label_set_text(label_5, "Orelhas");
        lv_obj_set_style_text_font(label_5, font_montserrat_regular_20, 0);
        lv_obj_set_style_text_color(label_5, lv_color_hex(0xF2F5F7), 0);

        lv_obj_t * column_0 = column_create(lv_obj_0, 12, 12, 8, 8, 8, 0, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_width(column_0, lv_pct(100));
        lv_obj_set_height(column_0, 252);
        lv_obj_t * row_3 = row_create(column_0, 0, 0, 0, 0, 8, 0, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_width(row_3, lv_pct(100));
        lv_obj_set_height(row_3, 28);
        lv_obj_t * label_6 = label_create(row_3);
        lv_obj_set_width(label_6, 0);
        lv_obj_set_flex_grow(label_6, 1);
        lv_label_set_text(label_6, "Status");
        lv_obj_set_style_text_font(label_6, font_montserrat_regular_12, 0);
        lv_obj_set_style_text_color(label_6, lv_color_hex(0xABB6BF), 0);

        lv_obj_t * label_7 = label_create(row_3);
        lv_label_bind_text(label_7, &subject_g_subj_orelhas_status_texto, "%s");
        lv_obj_set_style_text_font(label_7, font_montserrat_regular_14, 0);
        lv_obj_set_style_text_color(label_7, lv_color_hex(0xF2F5F7), 0);

        lv_obj_t * row_4 = row_create(column_0, 0, 0, 0, 0, 8, 0, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_width(row_4, lv_pct(100));
        lv_obj_set_height(row_4, LV_SIZE_CONTENT);
        lv_obj_t * tile_0 = tile_create(row_4, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, false);
        lv_obj_set_style_pad_column(tile_0, 0, 0);
        lv_obj_set_width(tile_0, 0);
        lv_obj_set_height(tile_0, 36);
        lv_obj_set_flex_grow(tile_0, 1);
        lv_obj_t * label_8 = label_create(tile_0);
        lv_label_set_text(label_8, "Ativar");
        lv_obj_set_style_text_font(label_8, font_montserrat_regular_14, 0);
        lv_obj_set_style_text_color(label_8, lv_color_hex(0xF2F5F7), 0);

        lv_obj_t * tile_1 = tile_create(row_4, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, false);
        lv_obj_set_style_pad_column(tile_1, 0, 0);
        lv_obj_set_width(tile_1, 0);
        lv_obj_set_height(tile_1, 36);
        lv_obj_set_flex_grow(tile_1, 1);
        lv_obj_t * label_9 = label_create(tile_1);
        lv_label_set_text(label_9, "Desativar");
        lv_obj_set_style_text_font(label_9, font_montserrat_regular_14, 0);
        lv_obj_set_style_text_color(label_9, lv_color_hex(0xF2F5F7), 0);

        lv_obj_t * container_0 = container_create(column_0);
        lv_obj_set_flex_flow(container_0, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_hor(container_0, 0, 0);
        lv_obj_set_style_pad_ver(container_0, 4, 0);
        lv_obj_set_style_pad_row(container_0, 8, 0);
        lv_obj_set_width(container_0, lv_pct(100));
        lv_obj_set_height(container_0, LV_SIZE_CONTENT);
        lv_obj_t * container_1 = container_create(container_0);
        lv_obj_set_flex_flow(container_1, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_all(container_1, 0, 0);
        lv_obj_set_style_pad_column(container_1, 8, 0);
        lv_obj_set_width(container_1, lv_pct(100));
        lv_obj_set_height(container_1, LV_SIZE_CONTENT);
        lv_obj_t * label_10 = label_create(container_1);
        lv_obj_set_width(label_10, 0);
        lv_obj_set_height(label_10, 18);
        lv_obj_set_flex_grow(label_10, 1);
        lv_label_set_text(label_10, "Ângulo máximo");
        lv_obj_set_style_text_font(label_10, font_montserrat_regular_14, 0);
        lv_obj_set_style_text_color(label_10, lv_color_hex(0xF2F5F7), 0);

        lv_obj_t * label_11 = label_create(container_1);
        lv_obj_set_height(label_11, 18);
        lv_label_bind_text(label_11, &subject_g_subj_orelhas_angulo_maximo, "%d°");
        lv_obj_set_style_text_font(label_11, font_montserrat_regular_14, 0);
        lv_obj_set_style_text_color(label_11, lv_color_hex(0x67D5E8), 0);

        cont_slider_toque_create(container_0, &subject_g_subj_orelhas_angulo_maximo);

        lv_obj_t * container_2 = container_create(container_0);
        lv_obj_set_flex_flow(container_2, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_all(container_2, 0, 0);
        lv_obj_set_style_pad_column(container_2, 8, 0);
        lv_obj_set_width(container_2, lv_pct(100));
        lv_obj_set_height(container_2, LV_SIZE_CONTENT);
        lv_obj_t * label_12 = label_create(container_2);
        lv_obj_set_width(label_12, 0);
        lv_obj_set_height(label_12, 16);
        lv_obj_set_flex_grow(label_12, 1);
        lv_label_set_text(label_12, "0°");
        lv_obj_set_style_text_font(label_12, font_montserrat_regular_12, 0);
        lv_obj_set_style_text_color(label_12, lv_color_hex(0xABB6BF), 0);

        lv_obj_t * label_13 = label_create(container_2);
        lv_obj_set_height(label_13, 16);
        lv_label_set_text(label_13, "90°");
        lv_obj_set_style_text_font(label_13, font_montserrat_regular_12, 0);
        lv_obj_set_style_text_color(label_13, lv_color_hex(0xABB6BF), 0);

        the_root = lv_obj_0;
    }
    #endif

    LV_TRACE_OBJ_CREATE("finished");

    return the_root;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/


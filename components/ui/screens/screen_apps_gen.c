/**
 * @file screen_apps_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "screen_apps_gen.h"
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

lv_obj_t * screen_apps_create(void)
{
    LV_TRACE_OBJ_CREATE("begin");


    lv_obj_t * the_root = NULL;

    #if UI_CHECK_COMPILE_TARGET(UI_TARGET_ALL)
    if (ui_check_target(UI_TARGET_ALL)) {
        lv_obj_t * lv_obj_0 = lv_obj_create(NULL);
        lv_obj_set_name_static(lv_obj_0, "screen_apps_#");
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
        lv_label_set_text(label_1, "Conectado");
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
        lv_label_set_text(label_3, "82%");
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

        lv_obj_add_screen_create_event(button_0, LV_EVENT_CLICKED, screen_main_create, LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, 300, 0);

        lv_obj_t * label_5 = label_create(row_2);
        lv_obj_set_width(label_5, 0);
        lv_obj_set_flex_grow(label_5, 1);
        lv_label_set_text(label_5, "Apps");
        lv_obj_set_style_text_font(label_5, font_montserrat_regular_20, 0);
        lv_obj_set_style_text_color(label_5, lv_color_hex(0xF2F5F7), 0);

        lv_obj_t * column_0 = column_create(lv_obj_0, 12, 12, 12, 12, 8, 0, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_width(column_0, lv_pct(100));
        lv_obj_set_height(column_0, 252);
        lv_obj_t * card_0 = card_create(column_0);
        lv_obj_set_flex_flow(card_0, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_flex_cross_place(card_0, LV_FLEX_ALIGN_CENTER, 0);
        lv_obj_set_style_flex_track_place(card_0, LV_FLEX_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_hor(card_0, 12, 0);
        lv_obj_set_style_pad_ver(card_0, 4, 0);
        lv_obj_set_style_pad_column(card_0, 8, 0);
        lv_obj_set_width(card_0, lv_pct(100));
        lv_obj_set_height(card_0, 56);
        lv_obj_set_style_bg_color(card_0, lv_color_hex(0x1A2026), 0);
        lv_obj_set_style_radius(card_0, 4, 0);
        lv_obj_t * column_1 = column_create(card_0, 0, 0, 0, 0, 0, 1, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_width(column_1, 0);
        lv_obj_set_height(column_1, LV_SIZE_CONTENT);
        lv_obj_set_flag(column_1, LV_OBJ_FLAG_CLICKABLE, false);
        lv_obj_set_flag(column_1, LV_OBJ_FLAG_EVENT_BUBBLE, true);
        lv_obj_t * label_6 = label_create(column_1);
        lv_obj_set_height(label_6, 18);
        lv_label_set_text(label_6, "Imagem setada");
        lv_obj_set_style_text_font(label_6, font_montserrat_regular_14, 0);
        lv_obj_set_style_text_color(label_6, lv_color_hex(0xF2F5F7), 0);

        lv_obj_t * label_7 = label_create(column_1);
        lv_obj_set_height(label_7, 16);
        lv_label_set_text(label_7, "Nenhuma · abrir galeria");
        lv_obj_set_style_text_font(label_7, font_montserrat_regular_12, 0);
        lv_obj_set_style_text_color(label_7, lv_color_hex(0xABB6BF), 0);

        lv_obj_t * label_8 = label_create(card_0);
        lv_label_set_text(label_8, ">");
        lv_obj_set_style_text_font(label_8, font_montserrat_regular_14, 0);
        lv_obj_set_style_text_color(label_8, lv_color_hex(0xABB6BF), 0);

        lv_obj_add_screen_create_event(card_0, LV_EVENT_CLICKED, screen_galeria_imagens_create, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 300, 0);

        lv_obj_t * card_1 = card_create(column_0);
        lv_obj_set_flex_flow(card_1, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_flex_cross_place(card_1, LV_FLEX_ALIGN_CENTER, 0);
        lv_obj_set_style_flex_track_place(card_1, LV_FLEX_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_hor(card_1, 12, 0);
        lv_obj_set_style_pad_ver(card_1, 4, 0);
        lv_obj_set_style_pad_column(card_1, 8, 0);
        lv_obj_set_width(card_1, lv_pct(100));
        lv_obj_set_height(card_1, 56);
        lv_obj_set_style_bg_color(card_1, lv_color_hex(0x1A2026), 0);
        lv_obj_set_style_radius(card_1, 4, 0);
        lv_obj_t * column_2 = column_create(card_1, 0, 0, 0, 0, 0, 1, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_width(column_2, 0);
        lv_obj_set_height(column_2, LV_SIZE_CONTENT);
        lv_obj_set_flag(column_2, LV_OBJ_FLAG_CLICKABLE, false);
        lv_obj_set_flag(column_2, LV_OBJ_FLAG_EVENT_BUBBLE, true);
        lv_obj_t * label_9 = label_create(column_2);
        lv_obj_set_height(label_9, 18);
        lv_label_set_text(label_9, "GIF setado");
        lv_obj_set_style_text_font(label_9, font_montserrat_regular_14, 0);
        lv_obj_set_style_text_color(label_9, lv_color_hex(0xF2F5F7), 0);

        lv_obj_t * label_10 = label_create(column_2);
        lv_obj_set_height(label_10, 16);
        lv_label_set_text(label_10, "Nenhum · abrir galeria");
        lv_obj_set_style_text_font(label_10, font_montserrat_regular_12, 0);
        lv_obj_set_style_text_color(label_10, lv_color_hex(0xABB6BF), 0);

        lv_obj_t * label_11 = label_create(card_1);
        lv_label_set_text(label_11, ">");
        lv_obj_set_style_text_font(label_11, font_montserrat_regular_14, 0);
        lv_obj_set_style_text_color(label_11, lv_color_hex(0xABB6BF), 0);

        lv_obj_add_screen_create_event(card_1, LV_EVENT_CLICKED, screen_galeria_gifs_create, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 300, 0);

        lv_obj_t * card_2 = card_create(column_0);
        lv_obj_set_flex_flow(card_2, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_flex_cross_place(card_2, LV_FLEX_ALIGN_CENTER, 0);
        lv_obj_set_style_flex_track_place(card_2, LV_FLEX_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_hor(card_2, 12, 0);
        lv_obj_set_style_pad_ver(card_2, 4, 0);
        lv_obj_set_style_pad_column(card_2, 8, 0);
        lv_obj_set_width(card_2, lv_pct(100));
        lv_obj_set_height(card_2, 56);
        lv_obj_set_style_bg_color(card_2, lv_color_hex(0x1A2026), 0);
        lv_obj_set_style_radius(card_2, 4, 0);
        lv_obj_t * column_3 = column_create(card_2, 0, 0, 0, 0, 0, 1, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_width(column_3, 0);
        lv_obj_set_height(column_3, LV_SIZE_CONTENT);
        lv_obj_set_flag(column_3, LV_OBJ_FLAG_CLICKABLE, false);
        lv_obj_set_flag(column_3, LV_OBJ_FLAG_EVENT_BUBBLE, true);
        lv_obj_t * label_12 = label_create(column_3);
        lv_obj_set_height(label_12, 18);
        lv_label_set_text(label_12, "Configurações");
        lv_obj_set_style_text_font(label_12, font_montserrat_regular_14, 0);
        lv_obj_set_style_text_color(label_12, lv_color_hex(0xF2F5F7), 0);

        lv_obj_t * label_13 = label_create(column_3);
        lv_obj_set_height(label_13, 16);
        lv_label_set_text(label_13, "Display e dispositivos");
        lv_obj_set_style_text_font(label_13, font_montserrat_regular_12, 0);
        lv_obj_set_style_text_color(label_13, lv_color_hex(0xABB6BF), 0);

        lv_obj_t * label_14 = label_create(card_2);
        lv_label_set_text(label_14, ">");
        lv_obj_set_style_text_font(label_14, font_montserrat_regular_14, 0);
        lv_obj_set_style_text_color(label_14, lv_color_hex(0xABB6BF), 0);

        lv_obj_add_screen_create_event(card_2, LV_EVENT_CLICKED, screen_settings_create, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 300, 0);

        lv_obj_t * label_15 = label_create(column_0);
        lv_obj_set_width(label_15, lv_pct(100));
        lv_label_set_text(label_15, "Recursos de mídia ainda\nnão fornecidos.");
        lv_obj_set_style_text_font(label_15, font_montserrat_regular_12, 0);
        lv_obj_set_style_text_line_space(label_15, 4, 0);
        lv_obj_set_style_text_color(label_15, lv_color_hex(0xABB6BF), 0);

        the_root = lv_obj_0;
    }
    #endif

    LV_TRACE_OBJ_CREATE("finished");

    return the_root;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/


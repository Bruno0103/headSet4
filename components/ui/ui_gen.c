/**
 * @file ui_gen.c
 */

/*********************
 *      INCLUDES
 *********************/

#include "ui_gen.h"

#if defined(LV_USE_XML) && LV_USE_XML
#endif /* LV_USE_XML */

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void check_font(lv_font_t ** font, const char * name);

/**********************
 *  STATIC VARIABLES
 **********************/

static uint32_t ui_target = UI_TARGET_ALL;

/*----------------
 * Translations
 *----------------*/

#ifndef LV_EDITOR_PREVIEW
    static const char * translation_languages[] = {"en", "de", NULL};
    static const char * translation_tags[] = {"dog", "cat", "house", NULL};
    static const char * translation_texts[] = {
        "This is a dog", "Das ist ein Hund", /* dog */
        "A curious little cat", "Eine neugierige kleine Katze", /* cat */
        "The house is cozy and warm", "Das Haus ist gemütlich und warm", /* house */
    };
#endif

/**********************
 *  GLOBAL VARIABLES
 **********************/

/*--------------------
 *  Permanent screens
 *-------------------*/

/*----------------
 * Fonts
 *----------------*/

lv_font_t * font_montserrat_regular_12;
lv_font_t * font_montserrat_regular_16;
lv_font_t * font_montserrat_regular_14;
lv_font_t * font_montserrat_regular_20;
lv_font_t * font_montserrat_regular_24;

/*----------------
 * Images
 *----------------*/



/*----------------
 * Global styles
 *----------------*/

lv_style_t style_screen_base_global;

/*----------------
 * Subjects
 *----------------*/

lv_subject_t subject_g_subj_bateria_tensao_mv;
lv_subject_t subject_g_subj_bt_active_slot;
lv_subject_t subject_g_subj_bt_alternancia_ativa;
lv_subject_t subject_g_subj_bt_nome_1;
lv_subject_t subject_g_subj_bt_nome_2;
lv_subject_t subject_g_subj_bt_status_1;
lv_subject_t subject_g_subj_bt_status_2;
lv_subject_t subject_g_subj_bt_status_alternancia;
lv_subject_t subject_g_subj_display_brilho;
lv_subject_t subject_g_subj_display_ligado;
lv_subject_t subject_g_subj_display_timeout_segundos;
lv_subject_t subject_g_subj_orelhas_angulo_maximo;
lv_subject_t subject_g_subj_orelhas_status_texto;
lv_subject_t subject_g_subj_proximidade_sensibilidade;
lv_subject_t subject_g_subj_proximidade_status_texto;
lv_subject_t subject_g_subj_vibracall_intensidade;
lv_subject_t subject_g_subj_vibracall_status_texto;
lv_subject_t subject_subj_battery_percent;

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void ui_init_gen(const char * asset_path)
{
    char buf[256];

    /* When running from the editor the theme set from the XML should overwrite this */
#if !defined(LV_EDITOR_PREVIEW)
#if LV_USE_THEME_SIMPLE
    lv_display_t * disp = lv_display_get_default();
    lv_theme_t * th = lv_theme_simple_init(disp);
    lv_display_set_theme(disp, th);
#else
    LV_LOG_WARN("Simple theme is selected in project.xml but LV_USE_THEME_SIMPLE is disabled");
#endif
#endif /*LV_EDITOR_PREVIEW*/


    /*----------------
     * Fonts
     *----------------*/

    /* Targets: any */

    #if UI_CHECK_COMPILE_TARGET(UI_TARGET_ALL)
    if (ui_check_target(UI_TARGET_ALL)) {
        if (!font_montserrat_regular_12) {
            /* font_montserrat_regular_12 */
            /* create bin font 'font_montserrat_regular_12' from file */
            lv_snprintf(buf, 256, "%s%s", asset_path, "fonts/font_montserrat_regular_12.bin");
            font_montserrat_regular_12 = lv_binfont_create(buf);

        }
        if (!font_montserrat_regular_16) {
            /* font_montserrat_regular_16 */
            /* create bin font 'font_montserrat_regular_16' from file */
            lv_snprintf(buf, 256, "%s%s", asset_path, "fonts/font_montserrat_regular_16.bin");
            font_montserrat_regular_16 = lv_binfont_create(buf);

        }
        if (!font_montserrat_regular_14) {
            /* font_montserrat_regular_14 */
            /* create bin font 'font_montserrat_regular_14' from file */
            lv_snprintf(buf, 256, "%s%s", asset_path, "fonts/font_montserrat_regular_14.bin");
            font_montserrat_regular_14 = lv_binfont_create(buf);

        }
        if (!font_montserrat_regular_20) {
            /* font_montserrat_regular_20 */
            /* create bin font 'font_montserrat_regular_20' from file */
            lv_snprintf(buf, 256, "%s%s", asset_path, "fonts/font_montserrat_regular_20.bin");
            font_montserrat_regular_20 = lv_binfont_create(buf);

        }
        if (!font_montserrat_regular_24) {
            /* font_montserrat_regular_24 */
            /* create bin font 'font_montserrat_regular_24' from file */
            lv_snprintf(buf, 256, "%s%s", asset_path, "fonts/font_montserrat_regular_24.bin");
            font_montserrat_regular_24 = lv_binfont_create(buf);

        }
    }
    #endif

    /*----------------
     * Images
     *----------------*/



    /*----------------
     * Global styles
     *----------------*/

    static bool style_inited = false;

    if (!style_inited) {
        /*Init all styles*/
        lv_style_init(&style_screen_base_global);

        lv_style_set_border_width(&style_screen_base_global, 0);
        lv_style_set_radius(&style_screen_base_global, 0);
        lv_style_set_shadow_width(&style_screen_base_global, 0);
        lv_style_set_shadow_opa(&style_screen_base_global, 0);

        style_inited = true;
    }

    /*----------------
     * Subjects
     *----------------*/
    lv_subject_init_int(&subject_g_subj_bateria_tensao_mv, 3980);
    static char subject_g_subj_bt_active_slot_buf[UI_SUBJECT_STRING_LENGTH];
    static char subject_g_subj_bt_active_slot_prev_buf[UI_SUBJECT_STRING_LENGTH];
    lv_subject_init_string(&subject_g_subj_bt_active_slot,
                           subject_g_subj_bt_active_slot_buf,
                           subject_g_subj_bt_active_slot_prev_buf,
                           UI_SUBJECT_STRING_LENGTH,
                           "Conectado"
                          );
    static char subject_g_subj_bt_alternancia_ativa_buf[UI_SUBJECT_STRING_LENGTH];
    static char subject_g_subj_bt_alternancia_ativa_prev_buf[UI_SUBJECT_STRING_LENGTH];
    lv_subject_init_string(&subject_g_subj_bt_alternancia_ativa,
                           subject_g_subj_bt_alternancia_ativa_buf,
                           subject_g_subj_bt_alternancia_ativa_prev_buf,
                           UI_SUBJECT_STRING_LENGTH,
                           "Manual"
                          );
    static char subject_g_subj_bt_nome_1_buf[UI_SUBJECT_STRING_LENGTH];
    static char subject_g_subj_bt_nome_1_prev_buf[UI_SUBJECT_STRING_LENGTH];
    lv_subject_init_string(&subject_g_subj_bt_nome_1,
                           subject_g_subj_bt_nome_1_buf,
                           subject_g_subj_bt_nome_1_prev_buf,
                           UI_SUBJECT_STRING_LENGTH,
                           "Fone A"
                          );
    static char subject_g_subj_bt_nome_2_buf[UI_SUBJECT_STRING_LENGTH];
    static char subject_g_subj_bt_nome_2_prev_buf[UI_SUBJECT_STRING_LENGTH];
    lv_subject_init_string(&subject_g_subj_bt_nome_2,
                           subject_g_subj_bt_nome_2_buf,
                           subject_g_subj_bt_nome_2_prev_buf,
                           UI_SUBJECT_STRING_LENGTH,
                           "Fone B"
                          );
    static char subject_g_subj_bt_status_1_buf[UI_SUBJECT_STRING_LENGTH];
    static char subject_g_subj_bt_status_1_prev_buf[UI_SUBJECT_STRING_LENGTH];
    lv_subject_init_string(&subject_g_subj_bt_status_1,
                           subject_g_subj_bt_status_1_buf,
                           subject_g_subj_bt_status_1_prev_buf,
                           UI_SUBJECT_STRING_LENGTH,
                           "Conectado"
                          );
    static char subject_g_subj_bt_status_2_buf[UI_SUBJECT_STRING_LENGTH];
    static char subject_g_subj_bt_status_2_prev_buf[UI_SUBJECT_STRING_LENGTH];
    lv_subject_init_string(&subject_g_subj_bt_status_2,
                           subject_g_subj_bt_status_2_buf,
                           subject_g_subj_bt_status_2_prev_buf,
                           UI_SUBJECT_STRING_LENGTH,
                           "Desconectado"
                          );
    static char subject_g_subj_bt_status_alternancia_buf[UI_SUBJECT_STRING_LENGTH];
    static char subject_g_subj_bt_status_alternancia_prev_buf[UI_SUBJECT_STRING_LENGTH];
    lv_subject_init_string(&subject_g_subj_bt_status_alternancia,
                           subject_g_subj_bt_status_alternancia_buf,
                           subject_g_subj_bt_status_alternancia_prev_buf,
                           UI_SUBJECT_STRING_LENGTH,
                           "Alternar ativo"
                          );
    lv_subject_init_int(&subject_g_subj_display_brilho, 70);
    static char subject_g_subj_display_ligado_buf[UI_SUBJECT_STRING_LENGTH];
    static char subject_g_subj_display_ligado_prev_buf[UI_SUBJECT_STRING_LENGTH];
    lv_subject_init_string(&subject_g_subj_display_ligado,
                           subject_g_subj_display_ligado_buf,
                           subject_g_subj_display_ligado_prev_buf,
                           UI_SUBJECT_STRING_LENGTH,
                           "Ligado"
                          );
    lv_subject_init_int(&subject_g_subj_display_timeout_segundos, 70);
    lv_subject_init_int(&subject_g_subj_orelhas_angulo_maximo, 90);
    static char subject_g_subj_orelhas_status_texto_buf[UI_SUBJECT_STRING_LENGTH];
    static char subject_g_subj_orelhas_status_texto_prev_buf[UI_SUBJECT_STRING_LENGTH];
    lv_subject_init_string(&subject_g_subj_orelhas_status_texto,
                           subject_g_subj_orelhas_status_texto_buf,
                           subject_g_subj_orelhas_status_texto_prev_buf,
                           UI_SUBJECT_STRING_LENGTH,
                           "Pronto"
                          );
    lv_subject_init_int(&subject_g_subj_proximidade_sensibilidade, 80);
    static char subject_g_subj_proximidade_status_texto_buf[UI_SUBJECT_STRING_LENGTH];
    static char subject_g_subj_proximidade_status_texto_prev_buf[UI_SUBJECT_STRING_LENGTH];
    lv_subject_init_string(&subject_g_subj_proximidade_status_texto,
                           subject_g_subj_proximidade_status_texto_buf,
                           subject_g_subj_proximidade_status_texto_prev_buf,
                           UI_SUBJECT_STRING_LENGTH,
                           "Pronto"
                          );
    lv_subject_init_int(&subject_g_subj_vibracall_intensidade, 60);
    static char subject_g_subj_vibracall_status_texto_buf[UI_SUBJECT_STRING_LENGTH];
    static char subject_g_subj_vibracall_status_texto_prev_buf[UI_SUBJECT_STRING_LENGTH];
    lv_subject_init_string(&subject_g_subj_vibracall_status_texto,
                           subject_g_subj_vibracall_status_texto_buf,
                           subject_g_subj_vibracall_status_texto_prev_buf,
                           UI_SUBJECT_STRING_LENGTH,
                           "Pronto"
                          );
    lv_subject_init_int(&subject_subj_battery_percent, 82);

    /*----------------
     * Translations
     *----------------*/

    #ifndef LV_EDITOR_PREVIEW
        lv_translation_add_static(translation_languages, translation_tags, translation_texts);
        lv_translation_set_language(translation_languages[0]);
    #endif

#if defined(LV_USE_XML) && LV_USE_XML
    /* Register widgets */

    /* Check all fonts / default if needed. This prevents fonts that are used in one target but
       defined in another from causing assertion failures during rendering of the Preview. */
    check_font(&font_montserrat_regular_12, "font_montserrat_regular_12");
    check_font(&font_montserrat_regular_16, "font_montserrat_regular_16");
    check_font(&font_montserrat_regular_14, "font_montserrat_regular_14");
    check_font(&font_montserrat_regular_20, "font_montserrat_regular_20");
    check_font(&font_montserrat_regular_24, "font_montserrat_regular_24");

    /* Register fonts */
    lv_xml_register_font(NULL, "font_montserrat_regular_12", font_montserrat_regular_12);
    lv_xml_register_font(NULL, "font_montserrat_regular_16", font_montserrat_regular_16);
    lv_xml_register_font(NULL, "font_montserrat_regular_14", font_montserrat_regular_14);
    lv_xml_register_font(NULL, "font_montserrat_regular_20", font_montserrat_regular_20);
    lv_xml_register_font(NULL, "font_montserrat_regular_24", font_montserrat_regular_24);

    /* Register subjects */
    lv_xml_register_subject(NULL, "subject_g_subj_bateria_tensao_mv", &subject_g_subj_bateria_tensao_mv);
    lv_xml_register_subject(NULL, "subject_g_subj_bt_active_slot", &subject_g_subj_bt_active_slot);
    lv_xml_register_subject(NULL, "subject_g_subj_bt_alternancia_ativa", &subject_g_subj_bt_alternancia_ativa);
    lv_xml_register_subject(NULL, "subject_g_subj_bt_nome_1", &subject_g_subj_bt_nome_1);
    lv_xml_register_subject(NULL, "subject_g_subj_bt_nome_2", &subject_g_subj_bt_nome_2);
    lv_xml_register_subject(NULL, "subject_g_subj_bt_status_1", &subject_g_subj_bt_status_1);
    lv_xml_register_subject(NULL, "subject_g_subj_bt_status_2", &subject_g_subj_bt_status_2);
    lv_xml_register_subject(NULL, "subject_g_subj_bt_status_alternancia", &subject_g_subj_bt_status_alternancia);
    lv_xml_register_subject(NULL, "subject_g_subj_display_brilho", &subject_g_subj_display_brilho);
    lv_xml_register_subject(NULL, "subject_g_subj_display_ligado", &subject_g_subj_display_ligado);
    lv_xml_register_subject(NULL, "subject_g_subj_display_timeout_segundos", &subject_g_subj_display_timeout_segundos);
    lv_xml_register_subject(NULL, "subject_g_subj_orelhas_angulo_maximo", &subject_g_subj_orelhas_angulo_maximo);
    lv_xml_register_subject(NULL, "subject_g_subj_orelhas_status_texto", &subject_g_subj_orelhas_status_texto);
    lv_xml_register_subject(NULL, "subject_g_subj_proximidade_sensibilidade", &subject_g_subj_proximidade_sensibilidade);
    lv_xml_register_subject(NULL, "subject_g_subj_proximidade_status_texto", &subject_g_subj_proximidade_status_texto);
    lv_xml_register_subject(NULL, "subject_g_subj_vibracall_intensidade", &subject_g_subj_vibracall_intensidade);
    lv_xml_register_subject(NULL, "subject_g_subj_vibracall_status_texto", &subject_g_subj_vibracall_status_texto);
    lv_xml_register_subject(NULL, "subject_subj_battery_percent", &subject_subj_battery_percent);

    /* Register callbacks */
#endif

    /* Register all the global assets so that they won't be created again when globals.xml is parsed.
     * While running in the editor skip this step to update the preview when the XML changes */
#if defined(LV_USE_XML) && LV_USE_XML && !defined(LV_EDITOR_PREVIEW)
    /* Register images */
#endif

#if !defined(LV_USE_XML) || LV_USE_XML == 0
    /*--------------------
     *  Permanent screens
     *-------------------*/
    /* If XML is enabled it's assumed that the permanent screens are created
     * manually from XML using lv_xml_create() */
#endif
}

void ui_set_target(uint32_t target)
{
    ui_target = target;
}

uint32_t ui_get_target(void)
{
    return ui_target;
}

bool ui_check_target(uint32_t target)
{
    return (ui_target & target) ? true : false;
}

/* Callbacks */

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void check_font(lv_font_t ** font, const char * name)
{
    if (!(*font)) {
        *font = (lv_font_t *)LV_FONT_DEFAULT;
        LV_LOG_WARN("font `%s` was not set. Using `LV_FONT_DEFAULT` instead", name);
    }
}
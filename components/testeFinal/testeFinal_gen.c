/**
 * @file testeFinal_gen.c
 */

/*********************
 *      INCLUDES
 *********************/

#include "testeFinal_gen.h"

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

static uint32_t testeFinal_target = TESTEFINAL_TARGET_ALL;

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

lv_font_t * font_inter_bold_7;
lv_font_t * font_inter_bold_6_5;
lv_font_t * font_inter_extra_bold_11_5;
lv_font_t * font_inter_regular_8;
lv_font_t * font_inter_bold_8_5;

/*----------------
 * Images
 *----------------*/

/* Targets: any */
const void * image_linkedin_image_7_1 = NULL;
extern const void * image_linkedin_image_7_1_data;
const void * image_headphones_1109 = NULL;
extern const void * image_headphones_1109_data;
const void * image_cpu_1111 = NULL;
extern const void * image_cpu_1111_data;
const void * image_bluetooth_1115 = NULL;
extern const void * image_bluetooth_1115_data;
const void * image_battery_1105 = NULL;
extern const void * image_battery_1105_data;
const void * image_battery_charging_1113 = NULL;
extern const void * image_battery_charging_1113_data;

/*----------------
 * Global styles
 *----------------*/

lv_style_t style_screen_base_global;

/*----------------
 * Subjects
 *----------------*/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void testeFinal_init_gen(const char * asset_path)
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

    #if TESTEFINAL_CHECK_COMPILE_TARGET(TESTEFINAL_TARGET_ALL)
    if (testeFinal_check_target(TESTEFINAL_TARGET_ALL)) {
        if (!font_inter_bold_7) {
            /* font_inter_bold_7 */
            /* create bin font 'font_inter_bold_7' from file */
            lv_snprintf(buf, 256, "%s%s", asset_path, "fonts/font_inter_bold_7.bin");
            font_inter_bold_7 = lv_binfont_create(buf);

        }
        if (!font_inter_bold_6_5) {
            /* font_inter_bold_6_5 */
            /* create bin font 'font_inter_bold_6_5' from file */
            lv_snprintf(buf, 256, "%s%s", asset_path, "fonts/font_inter_bold_6_5.bin");
            font_inter_bold_6_5 = lv_binfont_create(buf);

        }
        if (!font_inter_extra_bold_11_5) {
            /* font_inter_extra_bold_11_5 */
            /* create bin font 'font_inter_extra_bold_11_5' from file */
            lv_snprintf(buf, 256, "%s%s", asset_path, "fonts/font_inter_extra_bold_11_5.bin");
            font_inter_extra_bold_11_5 = lv_binfont_create(buf);

        }
        if (!font_inter_regular_8) {
            /* font_inter_regular_8 */
            /* create bin font 'font_inter_regular_8' from file */
            lv_snprintf(buf, 256, "%s%s", asset_path, "fonts/font_inter_regular_8.bin");
            font_inter_regular_8 = lv_binfont_create(buf);

        }
        if (!font_inter_bold_8_5) {
            /* font_inter_bold_8_5 */
            /* create bin font 'font_inter_bold_8_5' from file */
            lv_snprintf(buf, 256, "%s%s", asset_path, "fonts/font_inter_bold_8_5.bin");
            font_inter_bold_8_5 = lv_binfont_create(buf);

        }
    }
    #endif

    /*----------------
     * Images
     *----------------*/

    /* Targets: any */
    #if TESTEFINAL_CHECK_COMPILE_TARGET(TESTEFINAL_TARGET_ALL)
    if (testeFinal_check_target(TESTEFINAL_TARGET_ALL)) {
        /* image_linkedin_image_7_1 */
        if (!image_linkedin_image_7_1) {
            image_linkedin_image_7_1 = &image_linkedin_image_7_1_data;
        }
        /* image_headphones_1109 */
        if (!image_headphones_1109) {
            image_headphones_1109 = &image_headphones_1109_data;
        }
        /* image_cpu_1111 */
        if (!image_cpu_1111) {
            image_cpu_1111 = &image_cpu_1111_data;
        }
        /* image_bluetooth_1115 */
        if (!image_bluetooth_1115) {
            image_bluetooth_1115 = &image_bluetooth_1115_data;
        }
        /* image_battery_1105 */
        if (!image_battery_1105) {
            image_battery_1105 = &image_battery_1105_data;
        }
        /* image_battery_charging_1113 */
        if (!image_battery_charging_1113) {
            image_battery_charging_1113 = &image_battery_charging_1113_data;
        }
    }
    #endif

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
    check_font(&font_inter_bold_7, "font_inter_bold_7");
    check_font(&font_inter_bold_6_5, "font_inter_bold_6_5");
    check_font(&font_inter_extra_bold_11_5, "font_inter_extra_bold_11_5");
    check_font(&font_inter_regular_8, "font_inter_regular_8");
    check_font(&font_inter_bold_8_5, "font_inter_bold_8_5");

    /* Register fonts */
    lv_xml_register_font(NULL, "font_inter_bold_7", font_inter_bold_7);
    lv_xml_register_font(NULL, "font_inter_bold_6_5", font_inter_bold_6_5);
    lv_xml_register_font(NULL, "font_inter_extra_bold_11_5", font_inter_extra_bold_11_5);
    lv_xml_register_font(NULL, "font_inter_regular_8", font_inter_regular_8);
    lv_xml_register_font(NULL, "font_inter_bold_8_5", font_inter_bold_8_5);

    /* Register subjects */

    /* Register callbacks */
#endif

    /* Register all the global assets so that they won't be created again when globals.xml is parsed.
     * While running in the editor skip this step to update the preview when the XML changes */
#if defined(LV_USE_XML) && LV_USE_XML && !defined(LV_EDITOR_PREVIEW)
    /* Register images */
    lv_xml_register_image(NULL, "image_linkedin_image_7_1", image_linkedin_image_7_1);
    lv_xml_register_image(NULL, "image_headphones_1109", image_headphones_1109);
    lv_xml_register_image(NULL, "image_cpu_1111", image_cpu_1111);
    lv_xml_register_image(NULL, "image_bluetooth_1115", image_bluetooth_1115);
    lv_xml_register_image(NULL, "image_battery_1105", image_battery_1105);
    lv_xml_register_image(NULL, "image_battery_charging_1113", image_battery_charging_1113);
#endif

#if !defined(LV_USE_XML) || LV_USE_XML == 0
    /*--------------------
     *  Permanent screens
     *-------------------*/
    /* If XML is enabled it's assumed that the permanent screens are created
     * manually from XML using lv_xml_create() */
#endif
}

void testeFinal_set_target(uint32_t target)
{
    testeFinal_target = target;
}

uint32_t testeFinal_get_target(void)
{
    return testeFinal_target;
}

bool testeFinal_check_target(uint32_t target)
{
    return (testeFinal_target & target) ? true : false;
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
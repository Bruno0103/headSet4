# O audio agora e proprio (src/audio): nao usamos mais a2dp_sink_int/ext_codec_utils.
idf_component_register(
    SRCS "main.c"
         "src/display.c"
         "src/bluetooth.c"
         "src/bt/bt_a2dp.c"
         "src/bt/bt_avrcp.c"
         "src/bt/bt_hfp.c"
         "src/audio/audio.c"
         "src/audio/audio_codec.c"
         "src/audio/audio_io.c"
         "src/audio/voice_nr.c"
         "src/audio/wm8960.c"
    INCLUDE_DIRS "."
                 "src"
                 "src/bt"
                 "src/audio"
    PRIV_REQUIRES
        bt_app_core_utils
        bredr_app_common_utils
        a2dp_sink_common_utils
        esp_driver_i2c
        esp_driver_i2s
        esp_ringbuf
    REQUIRES
        bt
        testeFinal
        esp_lcd_st7735
        esp_lvgl_port
        driver
        esp_timer
)
 
idf_component_register(
    SRCS
        "board.c"
        "board_waveshare_43b.c"
        "board_yd_s3.c"
        "io_expander_ch422g.c"
        "rtc_pcf85063.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_common
        esp_driver_gpio
        esp_driver_i2c
        cjson
        freertos
)

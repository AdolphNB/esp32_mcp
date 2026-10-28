idf_component_register(
    SRCS
        "ui_scene.c"
        "ui_service.c"
        "ui_backend.c"
        "ui_backend_mock.c"
        "ui_backend_lvgl.c"
        "provisioning_ui.c"
        "boot_logo.c"
    INCLUDE_DIRS "include"
    EMBED_FILES "assets/logo_boot.bin"
    REQUIRES
        app_board
        cjson
        esp_common
        esp_driver_gpio
        esp_driver_i2c
        esp_lcd
        esp_lcd_touch_gt911
        esp_lvgl_port
        freertos
        heap
        log
        lvgl
        nvs_flash
        spiffs
)

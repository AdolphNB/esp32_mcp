idf_component_register(
    SRCS
        "ui_scene.c"
        "ui_service.c"
        "ui_backend.c"
        "ui_backend_mock.c"
        "ui_backend_lvgl.c"
    INCLUDE_DIRS "include"
    REQUIRES
        app_board
        cjson
        esp_common
        freertos
        log
        nvs_flash
        spiffs
)

#include "ui_backend.h"

#include "sdkconfig.h"

#if CONFIG_APP_ENABLE_DISPLAY

#if CONFIG_APP_DISPLAY_BACKEND_MOCK
extern const ui_backend_t ui_backend_mock;
#elif CONFIG_APP_DISPLAY_BACKEND_LVGL
extern const ui_backend_t ui_backend_lvgl;
#else
#error "No display backend selected"
#endif

const ui_backend_t *ui_backend_get(void)
{
#if CONFIG_APP_DISPLAY_BACKEND_MOCK
    return &ui_backend_mock;
#else
    return &ui_backend_lvgl;
#endif
}

#else

const ui_backend_t *ui_backend_get(void)
{
    return NULL;
}

#endif /* CONFIG_APP_ENABLE_DISPLAY */

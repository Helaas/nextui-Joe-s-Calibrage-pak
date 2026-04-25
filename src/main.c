#define AP_IMPLEMENTATION
#include "apostrophe.h"
#define AP_WIDGETS_IMPLEMENTATION
#include "apostrophe_widgets.h"

#include <stdio.h>

#include "calibrage.h"

void jc_ui_run(void);

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    ap_config cfg = {0};
    cfg.window_title = "Joe's Calibrage";
    cfg.font_path = AP_PLATFORM_IS_DEVICE ? NULL
                                          : "third_party/apostrophe/res/font.ttf";
    cfg.log_path = ap_resolve_log_path("joes-calibrage");
    cfg.is_nextui = AP_PLATFORM_IS_DEVICE;
    cfg.cpu_speed = AP_CPU_SPEED_MENU;

    if (ap_init(&cfg) != AP_OK) {
        fprintf(stderr, "Failed to initialise Apostrophe: %s\n", ap_get_error());
        return 1;
    }

    ap_log("startup: platform=%s raw_device=%s", AP_PLATFORM_NAME,
           jc_raw_device_path());
    jc_ui_run();
    ap_quit();
    return 0;
}

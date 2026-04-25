#include "calibrage.h"

#include <stdlib.h>
#include <string.h>

static const jc_platform_info platform_my355 = {
    .id = JC_PLATFORM_MY355,
    .id_name = "my355",
    .display_name = "Miyoo Flip",
    .raw_min = 0,
    .raw_max = 255,
    .min_range = 40,
    .default_x_min = 24,
    .default_x_max = 216,
    .default_y_min = 35,
    .default_y_max = 216,
    .default_x_zero = 128,
    .default_y_zero = 128,
    .sd_userdata_root = "/mnt/SDCARD/.userdata/my355/userdata",
    .runtime_userdata_root = "/userdata",
    .inputd_dir = "/tmp/miyoo_inputd",
    .reload_trigger_path = "/tmp/miyoo_inputd/cal_update",
    .joy_type_path = "/sys/class/miyooio_chr_dev/joy_type",
    .raw_combined_device = "/dev/ttyS1",
    .raw_left_device = NULL,
    .raw_right_device = NULL,
    .calibration_flag_path = "/tmp/joypad_calibrating",
    .raw_baud = 9600,
    .raw_format = JC_RAW_FORMAT_MY355,
};

static const jc_platform_info platform_tg5040 = {
    .id = JC_PLATFORM_TG5040,
    .id_name = "tg5040",
    .display_name = "TrimUI Smart Pro",
    .raw_min = 0,
    .raw_max = 65535,
    .min_range = 300,
    .default_x_min = 1050,
    .default_x_max = 2900,
    .default_y_min = 1050,
    .default_y_max = 2900,
    .default_x_zero = 2150,
    .default_y_zero = 2150,
    .sd_userdata_root = "/mnt/SDCARD/.userdata/tg5040/joes-calibrage",
    .runtime_userdata_root = "/mnt/UDISK",
    .inputd_dir = "/tmp/trimui_inputd",
    .reload_trigger_path = "/tmp/trimui_inputd_restart",
    .joy_type_path = NULL,
    .raw_combined_device = NULL,
    .raw_left_device = "/dev/ttyS4",
    .raw_right_device = "/dev/ttyS3",
    .calibration_flag_path = "/tmp/trimui_inputd/grab",
    .raw_baud = 19200,
    .raw_format = JC_RAW_FORMAT_TG5040,
};

static int env_is(const char *env, const char *value)
{
    return env && strcmp(env, value) == 0;
}

const jc_platform_info *jc_platform_current(void)
{
    const char *env = getenv("CALIBRAGE_PLATFORM");
    if (env_is(env, "tg5040"))
        return &platform_tg5040;
    if (env_is(env, "my355"))
        return &platform_my355;

#if defined(PLATFORM_TG5040)
    return &platform_tg5040;
#else
    return &platform_my355;
#endif
}

const char *jc_platform_id_name(void)
{
    return jc_platform_current()->id_name;
}

const char *jc_platform_display_name(void)
{
    return jc_platform_current()->display_name;
}

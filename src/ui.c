#include "apostrophe.h"
#include "apostrophe_widgets.h"

#include "calibrage.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

typedef enum {
    UI_ACTION_TEST = 0,
    UI_ACTION_CAL_LEFT,
    UI_ACTION_CAL_RIGHT,
    UI_ACTION_VALUES,
    UI_ACTION_RESTORE,
    UI_ACTION_QUIT,
} ui_action;

static void show_message(const char *message, bool error)
{
    (void)error;
    ap_footer_item footer[] = {
        { .button = AP_BTN_A, .label = "OK", .is_confirm = true },
    };
    ap_message_opts opts = {
        .message = message,
        .footer = footer,
        .footer_count = 1,
    };
    ap_confirm_result result = {0};
    (void)ap_confirmation(&opts, &result);
}

static bool show_confirm(const char *message, const char *confirm_label)
{
    ap_footer_item footer[] = {
        { .button = AP_BTN_B, .label = "Cancel" },
        { .button = AP_BTN_A, .label = confirm_label, .is_confirm = true },
    };
    ap_message_opts opts = {
        .message = message,
        .footer = footer,
        .footer_count = 2,
    };
    ap_confirm_result result = {0};
    return ap_confirmation(&opts, &result) == AP_OK && result.confirmed;
}

static ui_action show_main_menu(void)
{
    ap_list_item items[] = {
        AP_LIST_ITEM("Test Sticks", NULL),
        AP_LIST_ITEM("Calibrate Left", NULL),
        AP_LIST_ITEM("Calibrate Right", NULL),
        AP_LIST_ITEM("View Values", NULL),
        AP_LIST_ITEM("Restore Backup", NULL),
        AP_LIST_ITEM("Quit", NULL),
    };
    ap_footer_item footer[] = {
        { .button = AP_BTN_B, .label = "Quit" },
        { .button = AP_BTN_A, .label = "Select", .is_confirm = true },
    };
    ap_list_opts opts = ap_list_default_opts("Joe's Calibrage", items, 6);
    opts.footer = footer;
    opts.footer_count = 2;

    ap_list_result result = {0};
    if (ap_list(&opts, &result) != AP_OK)
        return UI_ACTION_QUIT;
    if (result.selected_index < 0 || result.selected_index > 5)
        return UI_ACTION_QUIT;
    return (ui_action)result.selected_index;
}

static SDL_Joystick *open_joystick(void)
{
    SDL_JoystickUpdate();
    int count = SDL_NumJoysticks();
    for (int i = 0; i < count; i++) {
        SDL_Joystick *joy = SDL_JoystickOpen(i);
        if (joy)
            return joy;
    }
    return NULL;
}

static float norm_sdl_axis(Sint16 v)
{
    if (v < 0)
        return (float)v / 32768.0f;
    return (float)v / 32767.0f;
}

static bool read_sdl_sticks(SDL_Joystick *joy, float out[4])
{
    if (!joy)
        return false;
    SDL_JoystickUpdate();
    int axes = SDL_JoystickNumAxes(joy);
    if (axes < 5)
        return false;
    out[0] = norm_sdl_axis(SDL_JoystickGetAxis(joy, 0));
    out[1] = norm_sdl_axis(SDL_JoystickGetAxis(joy, 1));
    out[2] = norm_sdl_axis(SDL_JoystickGetAxis(joy, 3));
    out[3] = norm_sdl_axis(SDL_JoystickGetAxis(joy, 4));
    return true;
}

static void draw_line(int x1, int y1, int x2, int y2, ap_color c)
{
    SDL_Renderer *r = ap_get_renderer();
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_RenderDrawLine(r, x1, y1, x2, y2);
}

static void draw_stick_widget(int cx, int cy, int radius, float x, float y,
                              const char *label, const char *detail)
{
    ap_theme *t = ap_get_theme();
    ap_color ring = t->hint;
    ap_color dot = t->highlight;
    ap_color cross = t->accent;
    ap_draw_circle(cx, cy, radius, (ap_color){ ring.r, ring.g, ring.b, 55 });
    draw_line(cx - radius, cy, cx + radius, cy, cross);
    draw_line(cx, cy - radius, cx, cy + radius, cross);
    int dx = (int)(x * (float)(radius - ap_scale(8)));
    int dy = (int)(y * (float)(radius - ap_scale(8)));
    ap_draw_circle(cx + dx, cy + dy, ap_scale(7), dot);
    ap_draw_text(ap_get_font(AP_FONT_SMALL), label,
                 cx - ap_measure_text(ap_get_font(AP_FONT_SMALL), label) / 2,
                 cy + radius + ap_scale(12), t->text);
    ap_draw_text(ap_get_font(AP_FONT_TINY), detail,
                 cx - ap_measure_text(ap_get_font(AP_FONT_TINY), detail) / 2,
                 cy + radius + ap_scale(32), t->hint);
}

static void show_test_screen(void)
{
    SDL_Joystick *joy = open_joystick();

    bool done = false;
    while (!done) {
        ap_input_event ev;
        while (ap_poll_input(&ev)) {
            if (ev.pressed && ev.button == AP_BTN_B)
                done = true;
        }

        float axes[4] = {0};
        bool have_axes = read_sdl_sticks(joy, axes);

        ap_clear_screen();
        ap_draw_screen_title("Test Sticks", NULL);

        SDL_Rect content = ap_get_content_rect(true, true, false);
        int radius = content.w < content.h ? content.w / 7 : content.h / 4;
        if (radius < ap_scale(48))
            radius = ap_scale(48);
        int left_cx = content.x + content.w / 4;
        int right_cx = content.x + (content.w * 3) / 4;
        int cy = content.y + content.h / 2 - ap_scale(20);

        char left_detail[80];
        char right_detail[80];
        snprintf(left_detail, sizeof(left_detail), have_axes ? "SDL axis" : "No input");
        snprintf(right_detail, sizeof(right_detail), have_axes ? "SDL axis" : "No input");

        draw_stick_widget(left_cx, cy, radius, axes[0], axes[1], "Left", left_detail);
        draw_stick_widget(right_cx, cy, radius, axes[2], axes[3], "Right", right_detail);

        char joy_type[32] = "unknown";
        if (jc_read_joy_type(joy_type, sizeof(joy_type)) != 0)
            snprintf(joy_type, sizeof(joy_type), "n/a");
        char status[128];
        snprintf(status, sizeof(status), "SDL axes: %s   joy_type: %s",
                 have_axes ? "live" : "unavailable", joy_type);
        ap_draw_text(ap_get_font(AP_FONT_TINY), status, content.x,
                     content.y + ap_scale(8), ap_get_theme()->hint);

        ap_footer_item footer[] = {
            { .button = AP_BTN_B, .label = "Back" },
        };
        ap_draw_footer(footer, 1);
        ap_request_frame();
        ap_present();
    }

    if (joy)
        SDL_JoystickClose(joy);
}

static void format_config_line(char *buf, size_t size, const char *name,
                               const jc_config *cfg, bool loaded)
{
    snprintf(buf, size,
             "%s (%s)\n"
             "x_min=%d  x_zero=%d  x_max=%d\n"
             "y_min=%d  y_zero=%d  y_max=%d",
             name, loaded ? "saved" : "default",
             cfg->x_min, cfg->x_zero, cfg->x_max,
             cfg->y_min, cfg->y_zero, cfg->y_max);
}

static void show_values_screen(void)
{
    jc_config_pair cfg;
    char err[160] = {0};
    (void)jc_config_load_pair(&cfg, err, sizeof(err));
    char left[180];
    char right[180];
    format_config_line(left, sizeof(left), "Left", &cfg.left, cfg.have_left);
    format_config_line(right, sizeof(right), "Right", &cfg.right, cfg.have_right);
    char joy_type[32] = "n/a";
    (void)jc_read_joy_type(joy_type, sizeof(joy_type));
    char msg[520];
    snprintf(msg, sizeof(msg), "%s\n\n%s\n\nSD root:\n%s\n\njoy_type: %s",
             left, right, jc_config_sd_userdata_root(), joy_type);
    show_message(msg, false);
}

static bool range_ready(const jc_calibration_capture *cap)
{
    return cap->range_count >= 20 &&
           (cap->x_max - cap->x_min) >= 40 &&
           (cap->y_max - cap->y_min) >= 40;
}

static void draw_calibration_screen(jc_stick stick, int step,
                                    const jc_calibration_capture *cap,
                                    const jc_raw_sample *raw)
{
    ap_clear_screen();
    ap_draw_screen_title(stick == JC_STICK_LEFT ? "Calibrate Left" : "Calibrate Right", NULL);
    SDL_Rect content = ap_get_content_rect(true, true, false);
    ap_theme *t = ap_get_theme();

    const char *instruction = step == 0
        ? "Rotate fully around the edge, then press A."
        : "Release the stick and keep it centered, then press Y.";
    ap_draw_text_wrapped(ap_get_font(AP_FONT_SMALL), instruction,
                         content.x, content.y + ap_scale(8), content.w,
                         t->text, AP_ALIGN_CENTER);

    int x = stick == JC_STICK_LEFT ? raw->left_x : raw->right_x;
    int y = stick == JC_STICK_LEFT ? raw->left_y : raw->right_y;
    float nx = 0.0f;
    float ny = 0.0f;
    if (raw->valid) {
        int cx = cap->zero_count > 0 ? (int)(cap->x_zero_sum / cap->zero_count) : 128;
        int cy = cap->zero_count > 0 ? (int)(cap->y_zero_sum / cap->zero_count) : 128;
        nx = jc_config_normalize_axis(x, cap->x_min, cx, cap->x_max);
        ny = jc_config_normalize_axis(y, cap->y_min, cy, cap->y_max);
    }

    int radius = content.h / 4;
    if (radius < ap_scale(54))
        radius = ap_scale(54);
    int cx = content.x + content.w / 2;
    int circle_y = content.y + content.h / 2 - ap_scale(10);
    char detail[96];
    snprintf(detail, sizeof(detail), "raw %d,%d", x, y);
    draw_stick_widget(cx, circle_y, radius, nx, ny,
                      stick == JC_STICK_LEFT ? "Left" : "Right", detail);

    char stats[160];
    snprintf(stats, sizeof(stats),
             "range x:%d-%d y:%d-%d   samples:%d   center:%d",
             cap->x_min, cap->x_max, cap->y_min, cap->y_max,
             cap->range_count, cap->zero_count);
    ap_draw_text(ap_get_font(AP_FONT_TINY), stats, content.x,
                 content.y + content.h - ap_scale(34), t->hint);
}

static void calibrate_stick(jc_stick stick)
{
    jc_raw_reader raw;
    jc_raw_reader_init(&raw);

    char err[160] = {0};
    if (jc_raw_begin_calibration(err, sizeof(err)) != 0) {
        show_message(err, true);
        return;
    }
    if (jc_raw_reader_open(&raw) != 0) {
        jc_raw_end_calibration();
        show_message(raw.error[0] ? raw.error : "Raw stick stream unavailable.", true);
        return;
    }

    jc_calibration_capture cap;
    jc_capture_reset(&cap);
    int step = 0;
    bool done = false;
    bool cancelled = false;
    jc_raw_sample sample = {0};

    while (!done) {
        int poll = jc_raw_reader_poll(&raw, &sample);
        if (poll < 0) {
            show_message(raw.error[0] ? raw.error : "Could not read raw stick stream.", true);
            cancelled = true;
            break;
        }
        if (poll > 0 && sample.valid) {
            int x = stick == JC_STICK_LEFT ? sample.left_x : sample.right_x;
            int y = stick == JC_STICK_LEFT ? sample.left_y : sample.right_y;
            if (step == 0)
                jc_capture_add_range(&cap, x, y);
            else
                jc_capture_add_center(&cap, x, y);
        }

        ap_input_event ev;
        while (ap_poll_input(&ev)) {
            if (!ev.pressed)
                continue;
            if (ev.button == AP_BTN_B) {
                cancelled = true;
                done = true;
            } else if (ev.button == AP_BTN_X) {
                jc_capture_reset(&cap);
                step = 0;
            } else if (step == 0 && ev.button == AP_BTN_A) {
                if (range_ready(&cap)) {
                    step = 1;
                } else {
                    show_message("Move the stick farther in every direction before continuing.", true);
                }
            } else if (step == 1 && ev.button == AP_BTN_Y) {
                jc_config cfg;
                if (jc_capture_make_config(&cap, &cfg, err, sizeof(err)) != 0) {
                    show_message(err, true);
                } else if (jc_config_save_stick(stick, &cfg, err, sizeof(err)) != 0) {
                    show_message(err, true);
                } else {
                    show_message("Calibration saved.", false);
                    done = true;
                }
            }
        }

        draw_calibration_screen(stick, step, &cap, &sample);
        ap_footer_item footer_step0[] = {
            { .button = AP_BTN_B, .label = "Cancel" },
            { .button = AP_BTN_X, .label = "Reset" },
            { .button = AP_BTN_A, .label = "Next", .is_confirm = true },
        };
        ap_footer_item footer_step1[] = {
            { .button = AP_BTN_B, .label = "Cancel" },
            { .button = AP_BTN_X, .label = "Reset" },
            { .button = AP_BTN_Y, .label = "Save", .is_confirm = true },
        };
        if (step == 0)
            ap_draw_footer(footer_step0, 3);
        else
            ap_draw_footer(footer_step1, 3);
        ap_request_frame();
        ap_present();
    }

    jc_raw_end_calibration();
    jc_raw_reader_close(&raw);
    if (cancelled)
        show_message("Calibration cancelled.", false);
}

static void restore_backup_flow(void)
{
    if (!show_confirm("Restore the first-run backups for both sticks?", "Restore"))
        return;
    char err[160] = {0};
    if (jc_config_restore_backup(err, sizeof(err)) != 0)
        show_message(err[0] ? err : "Could not restore backups.", true);
    else
        show_message("Backups restored.", false);
}

void jc_ui_run(void)
{
    for (;;) {
        switch (show_main_menu()) {
        case UI_ACTION_TEST:
            show_test_screen();
            break;
        case UI_ACTION_CAL_LEFT:
            calibrate_stick(JC_STICK_LEFT);
            break;
        case UI_ACTION_CAL_RIGHT:
            calibrate_stick(JC_STICK_RIGHT);
            break;
        case UI_ACTION_VALUES:
            show_values_screen();
            break;
        case UI_ACTION_RESTORE:
            restore_backup_flow();
            break;
        case UI_ACTION_QUIT:
        default:
            return;
        }
    }
}

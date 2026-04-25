#include "apostrophe.h"
#include "apostrophe_widgets.h"

#include "calibrage.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int imax(int a, int b)
{
    return a > b ? a : b;
}

static int imin(int a, int b)
{
    return a < b ? a : b;
}

static int ui_gap(int logical_px)
{
    return imax(1, ap_scale(logical_px));
}

static int ui_header_margin(void)
{
    return imax(ap_scale(48), 30);
}

static SDL_Rect ui_content_rect(bool has_footer)
{
    SDL_Rect rect = ap_get_content_rect(true, has_footer, false);
    int margin = ui_header_margin();
    rect.x += margin;
    rect.w -= margin * 2;
    if (rect.w < 1)
        rect.w = 1;
    return rect;
}

static int font_line_h(TTF_Font *font)
{
    return font ? TTF_FontLineSkip(font) : ui_gap(20);
}

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
    };
    ap_footer_item footer[] = {
        { .button = AP_BTN_B, .label = "Quit" },
        { .button = AP_BTN_A, .label = "Select", .is_confirm = true },
    };
    ap_list_opts opts = ap_list_default_opts("Joe's Calibrage", items, 5);
    opts.footer = footer;
    opts.footer_count = 2;

    ap_list_result result = {0};
    if (ap_list(&opts, &result) != AP_OK)
        return UI_ACTION_QUIT;
    if (result.selected_index < 0 || result.selected_index > 4)
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
                              const char *label, const char *detail,
                              int text_x, int text_w)
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

    TTF_Font *label_font = ap_get_font(AP_FONT_TINY);
    TTF_Font *detail_font = ap_get_font(AP_FONT_MICRO);
    int label_y = cy + radius + ui_gap(6);
    int detail_y = label_y + font_line_h(label_font);
    int label_w = ap_measure_text_ellipsized(label_font, label, text_w);
    int detail_w = ap_measure_text_ellipsized(detail_font, detail, text_w);
    ap_draw_text_ellipsized(label_font, label, text_x + (text_w - label_w) / 2,
                            label_y, t->text, text_w);
    ap_draw_text_ellipsized(detail_font, detail, text_x + (text_w - detail_w) / 2,
                            detail_y, t->hint, text_w);
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

        SDL_Rect content = ui_content_rect(true);
        TTF_Font *status_font = ap_get_font(AP_FONT_TINY);
        TTF_Font *label_font = ap_get_font(AP_FONT_TINY);
        TTF_Font *detail_font = ap_get_font(AP_FONT_MICRO);
        int footer_top = ap_get_screen_height() - ap_get_footer_height();
        int status_y = content.y + ui_gap(4);
        int widget_top = status_y + font_line_h(status_font) + ui_gap(18);
        int label_h = font_line_h(label_font) + font_line_h(detail_font);
        int widget_gap = ui_gap(20);
        int col_gap = ui_gap(20);
        int col_w = (content.w - col_gap) / 2;
        int max_radius_w = imax(1, (col_w - ui_gap(8)) / 2);
        int max_radius_h = imax(1, (footer_top - widget_top - label_h - widget_gap) / 2);
        int radius = imin(max_radius_w, max_radius_h);
        radius = imax(radius, ui_gap(44));
        radius = imin(radius, max_radius_h);
        int left_x = content.x;
        int right_x = content.x + col_w + col_gap;
        int left_cx = left_x + col_w / 2;
        int right_cx = right_x + col_w / 2;
        int cy = widget_top + radius;

        char left_detail[80];
        char right_detail[80];
        snprintf(left_detail, sizeof(left_detail), have_axes ? "SDL axis" : "No input");
        snprintf(right_detail, sizeof(right_detail), have_axes ? "SDL axis" : "No input");

        char joy_type[32] = "unknown";
        if (jc_read_joy_type(joy_type, sizeof(joy_type)) != 0)
            snprintf(joy_type, sizeof(joy_type), "n/a");
        char status[128];
        snprintf(status, sizeof(status), "SDL axes: %s   joy_type: %s",
                 have_axes ? "live" : "unavailable", joy_type);
        ap_draw_text_ellipsized(status_font, status, content.x, status_y,
                                ap_get_theme()->hint, content.w);

        draw_stick_widget(left_cx, cy, radius, axes[0], axes[1], "Left", left_detail,
                          left_x, col_w);
        draw_stick_widget(right_cx, cy, radius, axes[2], axes[3], "Right", right_detail,
                          right_x, col_w);

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

static void draw_config_block(const char *name, const jc_config *cfg, bool loaded,
                              int x, int y, int w)
{
    ap_theme *t = ap_get_theme();
    TTF_Font *section_font = ap_get_font(AP_FONT_SMALL);
    TTF_Font *body_font = ap_get_font(AP_FONT_TINY);
    char line[96];

    snprintf(line, sizeof(line), "%s (%s)", name, loaded ? "saved" : "default");
    ap_draw_text_ellipsized(section_font, line, x, y, t->text, w);
    y += font_line_h(section_font);

    snprintf(line, sizeof(line), "X: min=%d  zero=%d  max=%d",
             cfg->x_min, cfg->x_zero, cfg->x_max);
    ap_draw_text_ellipsized(body_font, line, x, y, t->hint, w);
    y += font_line_h(body_font);

    snprintf(line, sizeof(line), "Y: min=%d  zero=%d  max=%d",
             cfg->y_min, cfg->y_zero, cfg->y_max);
    ap_draw_text_ellipsized(body_font, line, x, y, t->hint, w);
}

static void show_values_screen(void)
{
    jc_config_pair cfg;
    char err[160] = {0};
    (void)jc_config_load_pair(&cfg, err, sizeof(err));

    bool done = false;
    while (!done) {
        ap_input_event ev;
        while (ap_poll_input(&ev)) {
            if (ev.pressed && (ev.button == AP_BTN_A || ev.button == AP_BTN_B))
                done = true;
        }

        ap_clear_screen();
        ap_draw_screen_title("Values", NULL);
        SDL_Rect content = ui_content_rect(true);
        ap_theme *t = ap_get_theme();
        TTF_Font *body_font = ap_get_font(AP_FONT_TINY);
        TTF_Font *section_font = ap_get_font(AP_FONT_SMALL);
        int y = content.y + ui_gap(4);
        int block_h = font_line_h(section_font) + font_line_h(body_font) * 2;

        draw_config_block("Left", &cfg.left, cfg.have_left, content.x, y, content.w);
        y += block_h + ui_gap(12);
        draw_config_block("Right", &cfg.right, cfg.have_right, content.x, y, content.w);
        y += block_h + ui_gap(14);

        char joy_type[32] = "n/a";
        (void)jc_read_joy_type(joy_type, sizeof(joy_type));
        char line[JC_PATH_MAX + 32];
        snprintf(line, sizeof(line), "SD: %s", jc_config_sd_userdata_root());
        ap_draw_text_ellipsized(body_font, line, content.x, y, t->hint, content.w);
        y += font_line_h(body_font);
        snprintf(line, sizeof(line), "joy_type: %s", joy_type);
        ap_draw_text_ellipsized(body_font, line, content.x, y, t->hint, content.w);

        ap_footer_item footer[] = {
            { .button = AP_BTN_A, .label = "OK", .is_confirm = true },
        };
        ap_draw_footer(footer, 1);
        ap_request_frame();
        ap_present();
    }
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
    SDL_Rect content = ui_content_rect(true);
    ap_theme *t = ap_get_theme();

    const char *instruction = step == 0
        ? "Rotate fully around the edge, then press A."
        : "Release the stick and keep it centered, then press Y.";
    TTF_Font *instruction_font = ap_get_font(AP_FONT_TINY);
    TTF_Font *label_font = ap_get_font(AP_FONT_TINY);
    TTF_Font *detail_font = ap_get_font(AP_FONT_MICRO);
    TTF_Font *stats_font = ap_get_font(AP_FONT_MICRO);
    int instruction_y = content.y + ui_gap(4);
    int instruction_h = ap_draw_text_wrapped(instruction_font, instruction,
                                             content.x, instruction_y, content.w,
                                             t->text, AP_ALIGN_LEFT);

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

    int footer_top = ap_get_screen_height() - ap_get_footer_height();
    int stats_h = font_line_h(stats_font) * 2;
    int label_h = font_line_h(label_font) + font_line_h(detail_font);
    int widget_top = instruction_y + instruction_h + ui_gap(14);
    int widget_bottom = footer_top - stats_h - ui_gap(16);
    int max_radius_h = imax(1, (widget_bottom - widget_top - label_h) / 2);
    int radius = imin(content.w / 5, max_radius_h);
    radius = imax(radius, ui_gap(38));
    radius = imin(radius, max_radius_h);
    int cx = content.x + content.w / 2;
    int circle_y = widget_top + radius;
    char detail[96];
    snprintf(detail, sizeof(detail), "raw %d,%d", x, y);
    draw_stick_widget(cx, circle_y, radius, nx, ny,
                      stick == JC_STICK_LEFT ? "Left" : "Right", detail,
                      content.x, content.w);

    int stats_y = footer_top - stats_h - ui_gap(8);
    char stats[96];
    snprintf(stats, sizeof(stats), "range x:%d-%d y:%d-%d",
             cap->x_min, cap->x_max, cap->y_min, cap->y_max);
    ap_draw_text_ellipsized(stats_font, stats, content.x, stats_y, t->hint, content.w);
    snprintf(stats, sizeof(stats), "samples:%d  center:%d",
             cap->range_count, cap->zero_count);
    ap_draw_text_ellipsized(stats_font, stats, content.x,
                            stats_y + font_line_h(stats_font), t->hint, content.w);
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

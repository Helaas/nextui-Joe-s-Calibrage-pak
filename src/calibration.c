#include "calibrage.h"

#include <stdio.h>
#include <string.h>

int jc_clamp_raw(int value)
{
    if (value < 0)
        return 0;
    if (value > 255)
        return 255;
    return value;
}

void jc_capture_reset(jc_calibration_capture *cap)
{
    if (!cap)
        return;
    cap->x_min = 255;
    cap->x_max = 0;
    cap->y_min = 255;
    cap->y_max = 0;
    cap->x_zero_sum = 0;
    cap->y_zero_sum = 0;
    cap->zero_count = 0;
    cap->range_count = 0;
}

void jc_capture_add_range(jc_calibration_capture *cap, int x, int y)
{
    if (!cap)
        return;
    x = jc_clamp_raw(x);
    y = jc_clamp_raw(y);
    if (x < cap->x_min)
        cap->x_min = x;
    if (x > cap->x_max)
        cap->x_max = x;
    if (y < cap->y_min)
        cap->y_min = y;
    if (y > cap->y_max)
        cap->y_max = y;
    cap->range_count++;
}

void jc_capture_add_center(jc_calibration_capture *cap, int x, int y)
{
    if (!cap)
        return;
    cap->x_zero_sum += jc_clamp_raw(x);
    cap->y_zero_sum += jc_clamp_raw(y);
    cap->zero_count++;
}

static void set_err(char *err, size_t err_size, const char *msg)
{
    if (err && err_size > 0)
        snprintf(err, err_size, "%s", msg);
}

static float clamp_norm(float value)
{
    if (value < -1.0f)
        return -1.0f;
    if (value > 1.0f)
        return 1.0f;
    return value;
}

int jc_capture_make_config(const jc_calibration_capture *cap, jc_config *out,
                           char *err, size_t err_size)
{
    if (!cap || !out) {
        set_err(err, err_size, "Missing calibration capture.");
        return -1;
    }
    if (cap->range_count < 20) {
        set_err(err, err_size, "Not enough movement samples.");
        return -1;
    }
    if ((cap->x_max - cap->x_min) < 40 || (cap->y_max - cap->y_min) < 40) {
        set_err(err, err_size, "Move the stick farther in every direction.");
        return -1;
    }
    if (cap->zero_count < 8) {
        set_err(err, err_size, "Not enough center samples.");
        return -1;
    }

    out->x_min = jc_clamp_raw(cap->x_min);
    out->x_max = jc_clamp_raw(cap->x_max);
    out->y_min = jc_clamp_raw(cap->y_min);
    out->y_max = jc_clamp_raw(cap->y_max);
    out->x_zero = jc_clamp_raw((int)((cap->x_zero_sum + cap->zero_count / 2) /
                                    cap->zero_count));
    out->y_zero = jc_clamp_raw((int)((cap->y_zero_sum + cap->zero_count / 2) /
                                    cap->zero_count));

    if (out->x_zero < out->x_min)
        out->x_zero = out->x_min;
    if (out->x_zero > out->x_max)
        out->x_zero = out->x_max;
    if (out->y_zero < out->y_min)
        out->y_zero = out->y_min;
    if (out->y_zero > out->y_max)
        out->y_zero = out->y_max;

    if (!jc_config_valid(out)) {
        set_err(err, err_size, "Calibration values are invalid.");
        return -1;
    }
    return 0;
}

float jc_config_normalize_axis(int raw, int min, int zero, int max)
{
    raw = jc_clamp_raw(raw);
    if (raw < zero) {
        int span = zero - min;
        if (span <= 0)
            return 0.0f;
        return clamp_norm(-((float)(zero - raw) / (float)span));
    }

    int span = max - zero;
    if (span <= 0)
        return 0.0f;
    return clamp_norm((float)(raw - zero) / (float)span);
}

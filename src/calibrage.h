#ifndef CALIBRAGE_H
#define CALIBRAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define JC_CONFIG_FIELD_COUNT 6
#define JC_PATH_MAX 512

typedef enum {
    JC_STICK_LEFT = 0,
    JC_STICK_RIGHT = 1,
} jc_stick;

typedef struct {
    int x_min;
    int x_max;
    int y_min;
    int y_max;
    int x_zero;
    int y_zero;
} jc_config;

typedef struct {
    jc_config left;
    jc_config right;
    bool have_left;
    bool have_right;
} jc_config_pair;

typedef struct {
    int left_x;
    int left_y;
    int right_x;
    int right_y;
    bool valid;
} jc_raw_sample;

typedef struct {
    int x_min;
    int x_max;
    int y_min;
    int y_max;
    long x_zero_sum;
    long y_zero_sum;
    int zero_count;
    int range_count;
} jc_calibration_capture;

typedef struct {
    int fd;
    unsigned char packet[6];
    int packet_pos;
    jc_raw_sample last;
    char error[160];
} jc_raw_reader;

void jc_config_default(jc_config *cfg);
int jc_config_parse_text(const char *text, jc_config *out, char *err, size_t err_size);
int jc_config_format(const jc_config *cfg, char *buf, size_t buf_size);
bool jc_config_valid(const jc_config *cfg);
int jc_config_load_pair(jc_config_pair *pair, char *err, size_t err_size);
int jc_config_save_stick(jc_stick stick, const jc_config *cfg, char *err, size_t err_size);
int jc_config_restore_backup(char *err, size_t err_size);
int jc_config_trigger_reload(char *err, size_t err_size);
const char *jc_config_sd_userdata_root(void);
const char *jc_config_runtime_userdata_root(void);
const char *jc_config_inputd_dir(void);
int jc_read_joy_type(char *buf, size_t buf_size);

void jc_capture_reset(jc_calibration_capture *cap);
void jc_capture_add_range(jc_calibration_capture *cap, int x, int y);
void jc_capture_add_center(jc_calibration_capture *cap, int x, int y);
int jc_capture_make_config(const jc_calibration_capture *cap, jc_config *out,
                           char *err, size_t err_size);
int jc_clamp_raw(int value);
float jc_config_normalize_axis(int raw, int min, int zero, int max);

void jc_raw_reader_init(jc_raw_reader *reader);
int jc_raw_reader_open(jc_raw_reader *reader);
void jc_raw_reader_close(jc_raw_reader *reader);
int jc_raw_reader_poll(jc_raw_reader *reader, jc_raw_sample *out);
int jc_raw_parse_byte(jc_raw_reader *reader, unsigned char byte, jc_raw_sample *out);
int jc_raw_parse_packet(const unsigned char packet[6], jc_raw_sample *out);
int jc_raw_begin_calibration(char *err, size_t err_size);
void jc_raw_end_calibration(void);
const char *jc_raw_device_path(void);

#endif

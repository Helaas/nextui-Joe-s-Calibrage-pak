#include "calibrage.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        failures++; \
    } \
} while (0)

static void write_text(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    CHECK(f != NULL);
    if (!f)
        return;
    fputs(text, f);
    fclose(f);
}

static void read_text(const char *path, char *buf, size_t size)
{
    FILE *f = fopen(path, "r");
    CHECK(f != NULL);
    if (!f)
        return;
    size_t n = fread(buf, 1, size - 1, f);
    buf[n] = '\0';
    fclose(f);
}

static int file_exists(const char *path)
{
    return access(path, F_OK) == 0;
}

static void test_parse_and_format(void)
{
    setenv("CALIBRAGE_PLATFORM", "my355", 1);
    const char *text =
        "x_min=20\nx_max=216\ny_min=35\ny_max=216\nx_zero=116\ny_zero=130\n";
    jc_config cfg;
    char err[128] = {0};
    CHECK(jc_config_parse_text(text, &cfg, err, sizeof(err)) == 0);
    CHECK(cfg.x_min == 20);
    CHECK(cfg.x_max == 216);
    CHECK(cfg.y_zero == 130);
    char out[256];
    CHECK(jc_config_format(&cfg, out, sizeof(out)) == 0);
    CHECK(strstr(out, "x_zero=116") != NULL);

    CHECK(jc_config_parse_text("x_min=1\n", &cfg, err, sizeof(err)) != 0);
}

static void test_tg5040_defaults_and_parse(void)
{
    setenv("CALIBRAGE_PLATFORM", "tg5040", 1);
    jc_config cfg;
    jc_config_default(&cfg);
    CHECK(cfg.x_min == 1050);
    CHECK(cfg.x_max == 2900);
    CHECK(cfg.y_min == 1050);
    CHECK(cfg.y_max == 2900);
    CHECK(cfg.x_zero == 2150);
    CHECK(cfg.y_zero == 2150);

    const char *text =
        "x_min=1000\nx_max=3100\ny_min=900\ny_max=3200\n"
        "x_zero=2050\ny_zero=2100\n";
    char err[128] = {0};
    CHECK(jc_config_parse_text(text, &cfg, err, sizeof(err)) == 0);
    CHECK(cfg.x_max == 3100);
    CHECK(cfg.y_zero == 2100);

    CHECK(jc_config_parse_text("x_min=0\nx_max=70000\ny_min=0\ny_max=1\nx_zero=0\ny_zero=0\n",
                               &cfg, err, sizeof(err)) != 0);
}

static void test_raw_packet_parser(void)
{
    setenv("CALIBRAGE_PLATFORM", "my355", 1);
    unsigned char packet[6] = {0xff, 0x82, 0x74, 0x7d, 0x7b, 0xfe};
    jc_raw_sample sample = {0};
    CHECK(jc_raw_parse_packet(packet, sizeof(packet), JC_STICK_LEFT, &sample) == 0);
    CHECK(sample.left_y == 0x82);
    CHECK(sample.left_x == 0x74);
    CHECK(sample.right_y == 0x7d);
    CHECK(sample.right_x == 0x7b);
    CHECK(sample.left_valid);
    CHECK(sample.right_valid);

    jc_raw_reader reader;
    jc_raw_reader_init(&reader);
    jc_raw_sample stream_sample = {0};
    for (int i = 0; i < 6; i++)
        (void)jc_raw_parse_byte(&reader, packet[i], &stream_sample);
    CHECK(stream_sample.valid);
    CHECK(stream_sample.right_x == 0x7b);
    CHECK(stream_sample.left_valid);
    CHECK(stream_sample.right_valid);

    setenv("CALIBRAGE_PLATFORM", "tg5040", 1);
    unsigned char tg_packet[8] = {0xff, 0x00, 0x00, 0x04, 0x1a, 0x08, 0x66, 0xfe};
    memset(&sample, 0, sizeof(sample));
    CHECK(jc_raw_parse_packet(tg_packet, sizeof(tg_packet), JC_STICK_LEFT, &sample) == 0);
    CHECK(sample.left_x == 1050);
    CHECK(sample.left_y == 2150);
    CHECK(sample.left_valid);
    CHECK(!sample.right_valid);

    jc_raw_reader_init(&reader);
    memset(&stream_sample, 0, sizeof(stream_sample));
    for (int i = 0; i < 8; i++)
        (void)jc_raw_parse_byte(&reader, tg_packet[i], &stream_sample);
    CHECK(stream_sample.valid);
    CHECK(stream_sample.left_x == 1050);
    CHECK(stream_sample.left_y == 2150);
    CHECK(stream_sample.left_valid);
    CHECK(!stream_sample.right_valid);

    tg_packet[7] = 0xfd;
    CHECK(jc_raw_parse_packet(tg_packet, sizeof(tg_packet), JC_STICK_RIGHT, &sample) != 0);
}

static void test_capture_math(void)
{
    setenv("CALIBRAGE_PLATFORM", "my355", 1);
    jc_calibration_capture cap;
    jc_capture_reset(&cap);
    for (int i = 0; i < 30; i++) {
        jc_capture_add_range(&cap, 20 + (i % 2) * 190, 35 + (i % 3) * 85);
    }
    for (int i = 0; i < 12; i++)
        jc_capture_add_center(&cap, 116 + (i % 3), 130 + (i % 2));
    jc_config cfg;
    char err[128] = {0};
    CHECK(jc_capture_make_config(&cap, &cfg, err, sizeof(err)) == 0);
    CHECK(cfg.x_min == 20);
    CHECK(cfg.x_max == 210);
    CHECK(cfg.y_min == 35);
    CHECK(cfg.y_max == 205);
    CHECK(cfg.x_zero >= 116 && cfg.x_zero <= 118);

    jc_capture_reset(&cap);
    for (int i = 0; i < 30; i++)
        jc_capture_add_range(&cap, 110, 120);
    for (int i = 0; i < 12; i++)
        jc_capture_add_center(&cap, 110, 120);
    CHECK(jc_capture_make_config(&cap, &cfg, err, sizeof(err)) != 0);

    CHECK(jc_config_normalize_axis(0, 20, 120, 230) == -1.0f);
    CHECK(jc_config_normalize_axis(255, 20, 120, 230) == 1.0f);

    setenv("CALIBRAGE_PLATFORM", "tg5040", 1);
    jc_capture_reset(&cap);
    for (int i = 0; i < 30; i++)
        jc_capture_add_range(&cap, 1000 + (i % 2) * 1900,
                             1100 + (i % 3) * 850);
    for (int i = 0; i < 12; i++)
        jc_capture_add_center(&cap, 2148 + (i % 4), 2151 + (i % 3));
    CHECK(jc_capture_make_config(&cap, &cfg, err, sizeof(err)) == 0);
    CHECK(cfg.x_min == 1000);
    CHECK(cfg.x_max == 2900);
    CHECK(cfg.y_min == 1100);
    CHECK(cfg.y_max == 2800);
    CHECK(cfg.x_zero >= 2148 && cfg.x_zero <= 2151);

    jc_capture_reset(&cap);
    for (int i = 0; i < 30; i++)
        jc_capture_add_range(&cap, 2100, 2120);
    for (int i = 0; i < 12; i++)
        jc_capture_add_center(&cap, 2100, 2120);
    CHECK(jc_capture_make_config(&cap, &cfg, err, sizeof(err)) != 0);
}

static void make_dir(const char *path)
{
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        perror(path);
        CHECK(0);
    }
}

static void test_save_restore_and_reload(void)
{
    setenv("CALIBRAGE_PLATFORM", "my355", 1);
    unsetenv("CALIBRAGE_RELOAD_TRIGGER_PATH");
    char root_template[] = "/tmp/calibrage-test-XXXXXX";
    char *root = mkdtemp(root_template);
    CHECK(root != NULL);
    if (!root)
        return;

    char sd[256];
    char rt[256];
    char inputd[256];
    char sd_parent[256];
    snprintf(sd, sizeof(sd), "%s/sd/userdata", root);
    snprintf(rt, sizeof(rt), "%s/runtime", root);
    snprintf(inputd, sizeof(inputd), "%s/inputd", root);
    snprintf(sd_parent, sizeof(sd_parent), "%s/sd", root);
    make_dir(sd_parent);
    make_dir(sd);
    make_dir(rt);
    make_dir(inputd);

    setenv("CALIBRAGE_SD_USERDATA_ROOT", sd, 1);
    setenv("CALIBRAGE_RUNTIME_USERDATA_ROOT", rt, 1);
    setenv("CALIBRAGE_INPUTD_DIR", inputd, 1);

    char left_path[320];
    char right_path[320];
    char runtime_left_path[320];
    snprintf(left_path, sizeof(left_path), "%s/joypad.config", sd);
    snprintf(right_path, sizeof(right_path), "%s/joypad_right.config", sd);
    snprintf(runtime_left_path, sizeof(runtime_left_path), "%s/joypad.config", rt);
    write_text(left_path, "x_min=20\nx_max=216\ny_min=35\ny_max=216\nx_zero=116\ny_zero=130\n");
    write_text(right_path, "x_min=29\nx_max=214\ny_min=37\ny_max=210\nx_zero=128\ny_zero=117\n");
    write_text(runtime_left_path, "x_min=20\nx_max=216\ny_min=35\ny_max=216\nx_zero=116\ny_zero=130\n");

    jc_config cfg = {
        .x_min = 10, .x_max = 230, .y_min = 11,
        .y_max = 231, .x_zero = 120, .y_zero = 121,
    };
    char err[160] = {0};
    CHECK(jc_config_save_stick(JC_STICK_LEFT, &cfg, err, sizeof(err)) == 0);

    char backup[340];
    char runtime_backup[340];
    snprintf(backup, sizeof(backup), "%s.bak", left_path);
    snprintf(runtime_backup, sizeof(runtime_backup), "%s.bak", runtime_left_path);
    CHECK(file_exists(backup));
    CHECK(file_exists(runtime_backup));
    char reload[320];
    snprintf(reload, sizeof(reload), "%s/cal_update", inputd);
    CHECK(file_exists(reload));

    jc_config_pair pair;
    CHECK(jc_config_load_pair(&pair, err, sizeof(err)) == 0);
    CHECK(pair.left.x_min == 10);

    CHECK(jc_config_restore_backup(err, sizeof(err)) == 0);
    CHECK(jc_config_load_pair(&pair, err, sizeof(err)) == 0);
    CHECK(pair.left.x_min == 20);

    char runtime_text[256] = {0};
    read_text(runtime_left_path, runtime_text, sizeof(runtime_text));
    CHECK(strstr(runtime_text, "x_min=20") != NULL);
}

static void test_tg5040_save_restore_and_restart_signal(void)
{
    setenv("CALIBRAGE_PLATFORM", "tg5040", 1);
    unsetenv("USERDATA_PATH");

    char root_template[] = "/tmp/calibrage-tg-test-XXXXXX";
    char *root = mkdtemp(root_template);
    CHECK(root != NULL);
    if (!root)
        return;

    char mirror[256];
    char runtime[256];
    char trigger[256];
    char root_sd[256];
    snprintf(mirror, sizeof(mirror), "%s/sd/.userdata/tg5040/joes-calibrage", root);
    snprintf(runtime, sizeof(runtime), "%s/UDISK", root);
    snprintf(trigger, sizeof(trigger), "%s/trimui_inputd_restart", root);
    snprintf(root_sd, sizeof(root_sd), "%s/sd", root);
    make_dir(root_sd);
    make_dir(runtime);

    setenv("CALIBRAGE_SD_USERDATA_ROOT", mirror, 1);
    setenv("CALIBRAGE_RUNTIME_USERDATA_ROOT", runtime, 1);
    setenv("CALIBRAGE_RELOAD_TRIGGER_PATH", trigger, 1);

    char runtime_left[320];
    char runtime_right[320];
    snprintf(runtime_left, sizeof(runtime_left), "%s/joypad.config", runtime);
    snprintf(runtime_right, sizeof(runtime_right), "%s/joypad_right.config", runtime);
    write_text(runtime_left,
               "x_min=1050\nx_max=2900\ny_min=1050\ny_max=2900\n"
               "x_zero=2150\ny_zero=2150\n");
    write_text(runtime_right,
               "x_min=1100\nx_max=2800\ny_min=1000\ny_max=3000\n"
               "x_zero=2100\ny_zero=2200\n");

    jc_config cfg = {
        .x_min = 900, .x_max = 3100, .y_min = 950,
        .y_max = 3050, .x_zero = 2050, .y_zero = 2100,
    };
    char err[160] = {0};
    CHECK(jc_config_save_stick(JC_STICK_LEFT, &cfg, err, sizeof(err)) == 0);

    char mirror_left[360];
    snprintf(mirror_left, sizeof(mirror_left), "%s/joypad.config", mirror);
    CHECK(file_exists(runtime_left));
    CHECK(file_exists(mirror_left));
    CHECK(file_exists(trigger));

    char runtime_backup[340];
    snprintf(runtime_backup, sizeof(runtime_backup), "%s.bak", runtime_left);
    CHECK(file_exists(runtime_backup));

    jc_config_pair pair;
    CHECK(jc_config_load_pair(&pair, err, sizeof(err)) == 0);
    CHECK(pair.left.x_min == 900);
    CHECK(pair.right.x_zero == 2100);

    CHECK(jc_config_restore_backup(err, sizeof(err)) == 0);
    CHECK(jc_config_load_pair(&pair, err, sizeof(err)) == 0);
    CHECK(pair.left.x_min == 1050);

    char mirror_text[256] = {0};
    read_text(mirror_left, mirror_text, sizeof(mirror_text));
    CHECK(strstr(mirror_text, "x_min=1050") != NULL);
}

int main(void)
{
    test_parse_and_format();
    test_tg5040_defaults_and_parse();
    test_raw_packet_parser();
    test_capture_math();
    test_save_restore_and_reload();
    test_tg5040_save_restore_and_restart_signal();

    if (failures) {
        fprintf(stderr, "%d test failure(s)\n", failures);
        return 1;
    }
    printf("calibrage tests passed\n");
    return 0;
}

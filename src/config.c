#include "calibrage.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

static const char *left_name = "joypad.config";
static const char *right_name = "joypad_right.config";

static void set_err(char *err, size_t err_size, const char *fmt, ...)
{
    if (!err || err_size == 0)
        return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, err_size, fmt, ap);
    va_end(ap);
}

static const char *env_or_default(const char *env_name, const char *fallback)
{
    const char *value = getenv(env_name);
    return (value && value[0] != '\0') ? value : fallback;
}

const char *jc_config_sd_userdata_root(void)
{
    const char *env = getenv("CALIBRAGE_SD_USERDATA_ROOT");
    if (env && env[0] != '\0')
        return env;
    if (access("/mnt/SDCARD", F_OK) == 0)
        return "/mnt/SDCARD/.userdata/my355/userdata";
    return "/mnt/sdcard/.userdata/my355/userdata";
}

const char *jc_config_runtime_userdata_root(void)
{
    return env_or_default("CALIBRAGE_RUNTIME_USERDATA_ROOT", "/userdata");
}

const char *jc_config_inputd_dir(void)
{
    return env_or_default("CALIBRAGE_INPUTD_DIR", "/tmp/miyoo_inputd");
}

static const char *joy_type_path(void)
{
    return env_or_default("CALIBRAGE_JOY_TYPE_PATH",
                          "/sys/class/miyooio_chr_dev/joy_type");
}

void jc_config_default(jc_config *cfg)
{
    if (!cfg)
        return;
    cfg->x_min = 24;
    cfg->x_max = 216;
    cfg->y_min = 35;
    cfg->y_max = 216;
    cfg->x_zero = 128;
    cfg->y_zero = 128;
}

bool jc_config_valid(const jc_config *cfg)
{
    if (!cfg)
        return false;
    if (cfg->x_min < 0 || cfg->x_min > 255 || cfg->x_max < 0 || cfg->x_max > 255)
        return false;
    if (cfg->y_min < 0 || cfg->y_min > 255 || cfg->y_max < 0 || cfg->y_max > 255)
        return false;
    if (cfg->x_zero < 0 || cfg->x_zero > 255 || cfg->y_zero < 0 || cfg->y_zero > 255)
        return false;
    if (cfg->x_min >= cfg->x_max || cfg->y_min >= cfg->y_max)
        return false;
    if (cfg->x_zero < cfg->x_min || cfg->x_zero > cfg->x_max)
        return false;
    if (cfg->y_zero < cfg->y_min || cfg->y_zero > cfg->y_max)
        return false;
    return true;
}

int jc_config_parse_text(const char *text, jc_config *out, char *err, size_t err_size)
{
    if (!text || !out) {
        set_err(err, err_size, "Missing config text.");
        return -1;
    }

    bool seen[JC_CONFIG_FIELD_COUNT] = {0};
    jc_config cfg = {0};
    const char *p = text;
    while (*p) {
        while (*p == '\n' || *p == '\r' || *p == ' ' || *p == '\t')
            p++;
        if (*p == '\0')
            break;

        const char *line_end = strchr(p, '\n');
        size_t len = line_end ? (size_t)(line_end - p) : strlen(p);
        char line[128];
        if (len >= sizeof(line))
            len = sizeof(line) - 1;
        memcpy(line, p, len);
        line[len] = '\0';

        char key[32];
        int value = 0;
        if (sscanf(line, " %31[^=]=%d", key, &value) == 2) {
            if (strcmp(key, "x_min") == 0) {
                cfg.x_min = value;
                seen[0] = true;
            } else if (strcmp(key, "x_max") == 0) {
                cfg.x_max = value;
                seen[1] = true;
            } else if (strcmp(key, "y_min") == 0) {
                cfg.y_min = value;
                seen[2] = true;
            } else if (strcmp(key, "y_max") == 0) {
                cfg.y_max = value;
                seen[3] = true;
            } else if (strcmp(key, "x_zero") == 0) {
                cfg.x_zero = value;
                seen[4] = true;
            } else if (strcmp(key, "y_zero") == 0) {
                cfg.y_zero = value;
                seen[5] = true;
            }
        }

        p = line_end ? line_end + 1 : p + strlen(p);
    }

    for (int i = 0; i < JC_CONFIG_FIELD_COUNT; i++) {
        if (!seen[i]) {
            set_err(err, err_size, "Config is missing required fields.");
            return -1;
        }
    }
    if (!jc_config_valid(&cfg)) {
        set_err(err, err_size, "Config values are out of range.");
        return -1;
    }
    *out = cfg;
    return 0;
}

int jc_config_format(const jc_config *cfg, char *buf, size_t buf_size)
{
    if (!cfg || !buf || buf_size == 0 || !jc_config_valid(cfg))
        return -1;
    int n = snprintf(buf, buf_size,
                     "x_min=%d\n"
                     "x_max=%d\n"
                     "y_min=%d\n"
                     "y_max=%d\n"
                     "x_zero=%d\n"
                     "y_zero=%d\n",
                     cfg->x_min, cfg->x_max, cfg->y_min, cfg->y_max,
                     cfg->x_zero, cfg->y_zero);
    return (n > 0 && (size_t)n < buf_size) ? 0 : -1;
}

static int join_path(char *out, size_t out_size, const char *dir, const char *leaf)
{
    int n = snprintf(out, out_size, "%s/%s", dir, leaf);
    return (n > 0 && (size_t)n < out_size) ? 0 : -1;
}

static int read_file(const char *path, char *buf, size_t buf_size,
                     char *err, size_t err_size)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        set_err(err, err_size, "Could not open %s: %s", path, strerror(errno));
        return -1;
    }
    ssize_t n = read(fd, buf, buf_size - 1);
    int saved = errno;
    close(fd);
    if (n < 0) {
        set_err(err, err_size, "Could not read %s: %s", path, strerror(saved));
        return -1;
    }
    buf[n] = '\0';
    return 0;
}

static int mkdir_p(const char *path)
{
    char tmp[JC_PATH_MAX];
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(tmp))
        return -1;
    memcpy(tmp, path, len + 1);

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
                return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
        return -1;
    return 0;
}

static int parent_dir(const char *path, char *out, size_t out_size)
{
    const char *slash = strrchr(path, '/');
    if (!slash || slash == path) {
        if (out_size < 2)
            return -1;
        strcpy(out, slash == path ? "/" : ".");
        return 0;
    }
    size_t len = (size_t)(slash - path);
    if (len >= out_size)
        return -1;
    memcpy(out, path, len);
    out[len] = '\0';
    return 0;
}

static int copy_file_if_missing(const char *src, const char *dst)
{
    if (access(dst, F_OK) == 0)
        return 0;

    char buf[256];
    int in = open(src, O_RDONLY | O_CLOEXEC);
    if (in < 0)
        return -1;
    int out = open(dst, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    if (out < 0) {
        close(in);
        return -1;
    }
    for (;;) {
        ssize_t n = read(in, buf, sizeof(buf));
        if (n < 0) {
            close(in);
            close(out);
            return -1;
        }
        if (n == 0)
            break;
        ssize_t off = 0;
        while (off < n) {
            ssize_t wrote = write(out, buf + off, (size_t)(n - off));
            if (wrote < 0) {
                close(in);
                close(out);
                return -1;
            }
            off += wrote;
        }
    }
    fsync(out);
    close(in);
    close(out);
    return 0;
}

static int write_file_atomic(const char *path, const char *data,
                             char *err, size_t err_size)
{
    char dir[JC_PATH_MAX];
    if (parent_dir(path, dir, sizeof(dir)) != 0 || mkdir_p(dir) != 0) {
        set_err(err, err_size, "Could not create parent directory for %s", path);
        return -1;
    }

    char tmp[JC_PATH_MAX];
    int n = snprintf(tmp, sizeof(tmp), "%s.tmp.%ld", path, (long)getpid());
    if (n <= 0 || (size_t)n >= sizeof(tmp)) {
        set_err(err, err_size, "Path too long: %s", path);
        return -1;
    }

    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        set_err(err, err_size, "Could not write %s: %s", tmp, strerror(errno));
        return -1;
    }

    size_t len = strlen(data);
    size_t off = 0;
    while (off < len) {
        ssize_t wrote = write(fd, data + off, len - off);
        if (wrote < 0) {
            int saved = errno;
            close(fd);
            unlink(tmp);
            set_err(err, err_size, "Could not write %s: %s", tmp, strerror(saved));
            return -1;
        }
        off += (size_t)wrote;
    }
    if (fsync(fd) != 0) {
        int saved = errno;
        close(fd);
        unlink(tmp);
        set_err(err, err_size, "Could not flush %s: %s", tmp, strerror(saved));
        return -1;
    }
    if (close(fd) != 0) {
        int saved = errno;
        unlink(tmp);
        set_err(err, err_size, "Could not close %s: %s", tmp, strerror(saved));
        return -1;
    }
    if (rename(tmp, path) != 0) {
        int saved = errno;
        unlink(tmp);
        set_err(err, err_size, "Could not replace %s: %s", path, strerror(saved));
        return -1;
    }
    return 0;
}

static int load_one(const char *root, const char *name, jc_config *out,
                    char *err, size_t err_size)
{
    char path[JC_PATH_MAX];
    char text[512];
    if (join_path(path, sizeof(path), root, name) != 0) {
        set_err(err, err_size, "Config path too long.");
        return -1;
    }
    if (read_file(path, text, sizeof(text), err, err_size) != 0)
        return -1;
    return jc_config_parse_text(text, out, err, err_size);
}

int jc_config_load_pair(jc_config_pair *pair, char *err, size_t err_size)
{
    if (!pair)
        return -1;
    memset(pair, 0, sizeof(*pair));

    char local_err[160] = {0};
    if (load_one(jc_config_sd_userdata_root(), left_name, &pair->left,
                 local_err, sizeof(local_err)) == 0) {
        pair->have_left = true;
    } else if (load_one(jc_config_runtime_userdata_root(), left_name, &pair->left,
                        local_err, sizeof(local_err)) == 0) {
        pair->have_left = true;
    } else {
        jc_config_default(&pair->left);
    }

    if (load_one(jc_config_sd_userdata_root(), right_name, &pair->right,
                 local_err, sizeof(local_err)) == 0) {
        pair->have_right = true;
    } else if (load_one(jc_config_runtime_userdata_root(), right_name, &pair->right,
                        local_err, sizeof(local_err)) == 0) {
        pair->have_right = true;
    } else {
        jc_config_default(&pair->right);
    }

    if (!pair->have_left || !pair->have_right) {
        set_err(err, err_size, "Loaded defaults for missing calibration files.");
        return 1;
    }
    return 0;
}

static int same_existing_file(const char *a, const char *b)
{
    struct stat sa;
    struct stat sb;
    if (stat(a, &sa) != 0 || stat(b, &sb) != 0)
        return 0;
    return sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
}

static int save_to_path_with_backup(const char *path, const char *data,
                                    char *err, size_t err_size)
{
    if (access(path, F_OK) == 0) {
        char backup[JC_PATH_MAX];
        int n = snprintf(backup, sizeof(backup), "%s.bak", path);
        if (n <= 0 || (size_t)n >= sizeof(backup)) {
            set_err(err, err_size, "Backup path too long.");
            return -1;
        }
        if (copy_file_if_missing(path, backup) != 0 && access(backup, F_OK) != 0) {
            set_err(err, err_size, "Could not create backup for %s", path);
            return -1;
        }
    }
    return write_file_atomic(path, data, err, err_size);
}

int jc_config_save_stick(jc_stick stick, const jc_config *cfg, char *err, size_t err_size)
{
    if (!jc_config_valid(cfg)) {
        set_err(err, err_size, "Refusing to save invalid calibration values.");
        return -1;
    }

    const char *name = (stick == JC_STICK_LEFT) ? left_name : right_name;
    char data[256];
    if (jc_config_format(cfg, data, sizeof(data)) != 0) {
        set_err(err, err_size, "Could not format calibration values.");
        return -1;
    }

    char sd_path[JC_PATH_MAX];
    char runtime_path[JC_PATH_MAX];
    if (join_path(sd_path, sizeof(sd_path), jc_config_sd_userdata_root(), name) != 0 ||
        join_path(runtime_path, sizeof(runtime_path), jc_config_runtime_userdata_root(), name) != 0) {
        set_err(err, err_size, "Calibration path too long.");
        return -1;
    }

    if (save_to_path_with_backup(sd_path, data, err, err_size) != 0)
        return -1;
    if (!same_existing_file(sd_path, runtime_path)) {
        if (save_to_path_with_backup(runtime_path, data, err, err_size) != 0)
            return -1;
    }

    sync();
    if (jc_config_trigger_reload(err, err_size) != 0)
        return -1;
    return 0;
}

static int restore_one(const char *root, const char *name, char *err, size_t err_size)
{
    char path[JC_PATH_MAX];
    char backup[JC_PATH_MAX];
    char text[512];
    if (join_path(path, sizeof(path), root, name) != 0) {
        set_err(err, err_size, "Restore path too long.");
        return -1;
    }
    int n = snprintf(backup, sizeof(backup), "%s.bak", path);
    if (n <= 0 || (size_t)n >= sizeof(backup)) {
        set_err(err, err_size, "Backup path too long.");
        return -1;
    }
    if (access(backup, F_OK) != 0)
        return 1;
    if (read_file(backup, text, sizeof(text), err, err_size) != 0)
        return -1;
    jc_config cfg;
    if (jc_config_parse_text(text, &cfg, err, err_size) != 0)
        return -1;
    return write_file_atomic(path, text, err, err_size);
}

int jc_config_restore_backup(char *err, size_t err_size)
{
    int restored = 0;
    int rc = restore_one(jc_config_sd_userdata_root(), left_name, err, err_size);
    if (rc < 0)
        return -1;
    restored += rc == 0;
    rc = restore_one(jc_config_sd_userdata_root(), right_name, err, err_size);
    if (rc < 0)
        return -1;
    restored += rc == 0;

    char sd_left[JC_PATH_MAX];
    char rt_left[JC_PATH_MAX];
    if (join_path(sd_left, sizeof(sd_left), jc_config_sd_userdata_root(), left_name) != 0 ||
        join_path(rt_left, sizeof(rt_left), jc_config_runtime_userdata_root(), left_name) != 0) {
        set_err(err, err_size, "Restore path too long.");
        return -1;
    }
    if (!same_existing_file(sd_left, rt_left)) {
        rc = restore_one(jc_config_runtime_userdata_root(), left_name, err, err_size);
        if (rc < 0)
            return -1;
        restored += rc == 0;
        rc = restore_one(jc_config_runtime_userdata_root(), right_name, err, err_size);
        if (rc < 0)
            return -1;
        restored += rc == 0;
    }
    if (restored == 0) {
        set_err(err, err_size, "No calibration backups found.");
        return -1;
    }
    sync();
    return jc_config_trigger_reload(err, err_size);
}

int jc_config_trigger_reload(char *err, size_t err_size)
{
    const char *dir = jc_config_inputd_dir();
    if (mkdir_p(dir) != 0) {
        set_err(err, err_size, "Could not create %s", dir);
        return -1;
    }
    char path[JC_PATH_MAX];
    if (join_path(path, sizeof(path), dir, "cal_update") != 0) {
        set_err(err, err_size, "Reload trigger path too long.");
        return -1;
    }
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        set_err(err, err_size, "Could not trigger input reload: %s", strerror(errno));
        return -1;
    }
    close(fd);
    return 0;
}

int jc_read_joy_type(char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0)
        return -1;
    char text[128];
    if (read_file(joy_type_path(), text, sizeof(text), NULL, 0) != 0)
        return -1;
    char first[32];
    if (sscanf(text, " %31s", first) != 1)
        return -1;
    snprintf(buf, buf_size, "%s", first);
    return 0;
}

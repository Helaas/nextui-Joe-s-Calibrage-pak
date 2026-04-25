#include "calibrage.h"

#include <errno.h>
#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

#ifndef O_NOCTTY
#define O_NOCTTY 0
#endif

#define JC_MAX_PAUSED_PIDS 8

static pid_t paused_pids[JC_MAX_PAUSED_PIDS];
static int paused_pid_count = 0;

const char *jc_raw_device_path(void)
{
    const char *env = getenv("CALIBRAGE_RAW_DEVICE");
    if (env && env[0] != '\0')
        return env;
    const jc_platform_info *platform = jc_platform_current();
    if (platform->raw_combined_device)
        return platform->raw_combined_device;
    return jc_raw_left_device_path();
}

const char *jc_raw_left_device_path(void)
{
    const char *env = getenv("CALIBRAGE_RAW_LEFT_DEVICE");
    if (env && env[0] != '\0')
        return env;
    const char *fallback = getenv("CALIBRAGE_RAW_DEVICE");
    if (fallback && fallback[0] != '\0')
        return fallback;
    const jc_platform_info *platform = jc_platform_current();
    return platform->raw_left_device ? platform->raw_left_device
                                     : platform->raw_combined_device;
}

const char *jc_raw_right_device_path(void)
{
    const char *env = getenv("CALIBRAGE_RAW_RIGHT_DEVICE");
    if (env && env[0] != '\0')
        return env;
    const jc_platform_info *platform = jc_platform_current();
    return platform->raw_right_device ? platform->raw_right_device
                                      : platform->raw_combined_device;
}

static const char *calibrating_flag_path(void)
{
    const char *env = getenv("CALIBRAGE_CALIBRATING_FLAG");
    return (env && env[0] != '\0') ? env : jc_platform_current()->calibration_flag_path;
}

void jc_raw_reader_init(jc_raw_reader *reader)
{
    if (!reader)
        return;
    memset(reader, 0, sizeof(*reader));
    for (size_t i = 0; i < sizeof(reader->streams) / sizeof(reader->streams[0]); i++)
        reader->streams[i].fd = -1;
}

static void set_reader_error(jc_raw_reader *reader, const char *fmt, ...)
{
    if (!reader)
        return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(reader->error, sizeof(reader->error), fmt, ap);
    va_end(ap);
}

static speed_t speed_for_baud(int baud)
{
    switch (baud) {
    case 19200:
        return B19200;
    case 9600:
    default:
        return B9600;
    }
}

static void configure_serial_best_effort(int fd, int baud)
{
    struct termios tio;
    if (tcgetattr(fd, &tio) != 0)
        return;
    speed_t speed = speed_for_baud(baud);
    cfsetispeed(&tio, speed);
    cfsetospeed(&tio, speed);
    tio.c_cflag |= (CLOCAL | CREAD);
    tio.c_cflag &= ~CSIZE;
    tio.c_cflag |= CS8;
#ifdef CRTSCTS
    tio.c_cflag &= ~(PARENB | CSTOPB | CRTSCTS);
#else
    tio.c_cflag &= ~(PARENB | CSTOPB);
#endif
    tio.c_lflag = 0;
    tio.c_iflag = 0;
    tio.c_oflag = 0;
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 0;
    tcsetattr(fd, TCSANOW, &tio);
}

static int open_stream(jc_raw_reader *reader, int index, const char *path,
                       jc_stick stick, bool combined)
{
    jc_raw_stream *stream = &reader->streams[index];
    stream->fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOCTTY);
    if (stream->fd < 0) {
        set_reader_error(reader, "Could not open %s: %s", path, strerror(errno));
        return -1;
    }
    stream->packet_pos = 0;
    stream->stick = stick;
    stream->combined = combined;
    stream->path = path;
    configure_serial_best_effort(stream->fd, jc_platform_current()->raw_baud);
    return 0;
}

int jc_raw_reader_open(jc_raw_reader *reader)
{
    if (!reader)
        return -1;
    jc_raw_reader_close(reader);
    jc_raw_reader_init(reader);

    const jc_platform_info *platform = jc_platform_current();
    if (platform->raw_format == JC_RAW_FORMAT_TG5040) {
        reader->stream_count = 2;
        if (open_stream(reader, 0, jc_raw_left_device_path(), JC_STICK_LEFT, false) != 0)
            return -1;
        if (open_stream(reader, 1, jc_raw_right_device_path(), JC_STICK_RIGHT, false) != 0) {
            jc_raw_reader_close(reader);
            return -1;
        }
        return 0;
    }

    reader->stream_count = 1;
    return open_stream(reader, 0, jc_raw_device_path(), JC_STICK_LEFT, true);
}

int jc_raw_reader_open_stick(jc_raw_reader *reader, jc_stick stick)
{
    if (!reader)
        return -1;

    const jc_platform_info *platform = jc_platform_current();
    if (platform->raw_format != JC_RAW_FORMAT_TG5040)
        return jc_raw_reader_open(reader);

    return jc_raw_reader_open(reader);
}

void jc_raw_reader_close(jc_raw_reader *reader)
{
    if (!reader)
        return;
    for (size_t i = 0; i < sizeof(reader->streams) / sizeof(reader->streams[0]); i++) {
        if (reader->streams[i].fd >= 0) {
            close(reader->streams[i].fd);
            reader->streams[i].fd = -1;
        }
        reader->streams[i].packet_pos = 0;
    }
    reader->stream_count = 0;
}

static int packet_size_for_platform(void)
{
    return jc_platform_current()->raw_format == JC_RAW_FORMAT_TG5040 ? 8 : 6;
}

static int parse_my355_packet(const unsigned char *packet, size_t packet_size,
                              jc_raw_sample *out)
{
    if (packet_size != 6 || packet[0] != 0xff || packet[5] != 0xfe)
        return -1;
    out->left_y = packet[1];
    out->left_x = packet[2];
    out->right_y = packet[3];
    out->right_x = packet[4];
    out->valid = true;
    out->left_valid = true;
    out->right_valid = true;
    return 0;
}

static int parse_tg5040_packet(const unsigned char *packet, size_t packet_size,
                               jc_stick stick, jc_raw_sample *out)
{
    if (packet_size != 8 || packet[0] != 0xff || packet[7] != 0xfe)
        return -1;
    int x = ((int)packet[3] << 8) | packet[4];
    int y = ((int)packet[5] << 8) | packet[6];
    int buttons = packet[2];
    if (stick == JC_STICK_RIGHT) {
        out->right_x = x;
        out->right_y = y;
        out->right_buttons = buttons;
        out->right_valid = true;
    } else {
        out->left_x = x;
        out->left_y = y;
        out->left_buttons = buttons;
        out->left_valid = true;
    }
    out->valid = true;
    return 0;
}

int jc_raw_parse_packet(const unsigned char *packet, size_t packet_size,
                        jc_stick stick, jc_raw_sample *out)
{
    if (!packet || !out)
        return -1;
    memset(out, 0, sizeof(*out));
    if (jc_platform_current()->raw_format == JC_RAW_FORMAT_TG5040)
        return parse_tg5040_packet(packet, packet_size, stick, out);
    return parse_my355_packet(packet, packet_size, out);
}

static void merge_sample(jc_raw_reader *reader, const jc_raw_stream *stream,
                         const jc_raw_sample *sample, jc_raw_sample *out)
{
    if (stream->combined) {
        reader->last = *sample;
        reader->last.left_valid = true;
        reader->last.right_valid = true;
        reader->have_left = true;
        reader->have_right = true;
    } else if (stream->stick == JC_STICK_RIGHT) {
        reader->last.right_x = sample->right_x;
        reader->last.right_y = sample->right_y;
        reader->last.right_buttons = sample->right_buttons;
        reader->last.right_valid = true;
        reader->have_right = true;
    } else {
        reader->last.left_x = sample->left_x;
        reader->last.left_y = sample->left_y;
        reader->last.left_buttons = sample->left_buttons;
        reader->last.left_valid = true;
        reader->have_left = true;
    }
    reader->last.valid = reader->have_left || reader->have_right;
    *out = reader->last;
}

static int parse_stream_byte(jc_raw_reader *reader, jc_raw_stream *stream,
                             unsigned char byte, jc_raw_sample *out)
{
    if (stream->packet_pos == 0) {
        if (byte != 0xff)
            return 0;
        stream->packet[stream->packet_pos++] = byte;
        return 0;
    }

    if (stream->packet_pos >= JC_RAW_MAX_PACKET)
        stream->packet_pos = 0;
    stream->packet[stream->packet_pos++] = byte;

    int packet_size = packet_size_for_platform();
    if (stream->packet_pos < packet_size)
        return 0;

    stream->packet_pos = 0;
    jc_raw_sample sample = {0};
    if (jc_raw_parse_packet(stream->packet, (size_t)packet_size,
                            stream->stick, &sample) == 0) {
        merge_sample(reader, stream, &sample, out);
        return 1;
    }
    if (byte == 0xff) {
        stream->packet[0] = byte;
        stream->packet_pos = 1;
    }
    return 0;
}

int jc_raw_parse_byte(jc_raw_reader *reader, unsigned char byte, jc_raw_sample *out)
{
    if (!reader || !out)
        return -1;
    if (reader->stream_count <= 0) {
        reader->stream_count = 1;
        reader->streams[0].fd = -1;
        reader->streams[0].stick = JC_STICK_LEFT;
        reader->streams[0].combined =
            jc_platform_current()->raw_format == JC_RAW_FORMAT_MY355;
        reader->streams[0].path = jc_raw_device_path();
    }
    return parse_stream_byte(reader, &reader->streams[0], byte, out);
}

static int poll_stream(jc_raw_reader *reader, jc_raw_stream *stream, jc_raw_sample *out)
{
    unsigned char buf[64];
    int got = 0;
    for (;;) {
        ssize_t n = read(stream->fd, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            set_reader_error(reader, "Could not read %s: %s", stream->path,
                             strerror(errno));
            return -1;
        }
        if (n == 0)
            break;
        for (ssize_t i = 0; i < n; i++) {
            jc_raw_sample sample = {0};
            if (parse_stream_byte(reader, stream, buf[i], &sample) == 1) {
                *out = sample;
                got = 1;
            }
        }
        if (n < (ssize_t)sizeof(buf))
            break;
    }
    return got;
}

int jc_raw_reader_poll(jc_raw_reader *reader, jc_raw_sample *out)
{
    if (!reader || !out || reader->stream_count <= 0)
        return -1;

    int got = 0;
    for (int i = 0; i < reader->stream_count; i++) {
        if (reader->streams[i].fd < 0)
            return -1;
        int rc = poll_stream(reader, &reader->streams[i], out);
        if (rc < 0)
            return -1;
        if (rc > 0)
            got = 1;
    }
    return got;
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

static int read_comm(pid_t pid, char *buf, size_t buf_size)
{
    char path[64];
    int n = snprintf(path, sizeof(path), "/proc/%ld/comm", (long)pid);
    if (n <= 0 || (size_t)n >= sizeof(path))
        return -1;

    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return -1;
    ssize_t got = read(fd, buf, buf_size - 1);
    close(fd);
    if (got <= 0)
        return -1;
    buf[got] = '\0';
    char *nl = strchr(buf, '\n');
    if (nl)
        *nl = '\0';
    return 0;
}

static void pause_trimui_inputd(void)
{
    paused_pid_count = 0;
    if (jc_platform_current()->id != JC_PLATFORM_TG5040)
        return;

    DIR *dir = opendir("/proc");
    if (!dir)
        return;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (!isdigit((unsigned char)ent->d_name[0]))
            continue;
        pid_t pid = (pid_t)strtol(ent->d_name, NULL, 10);
        if (pid <= 0)
            continue;
        char comm[64];
        if (read_comm(pid, comm, sizeof(comm)) != 0)
            continue;
        if (strcmp(comm, "trimui_inputd") != 0)
            continue;
        if (kill(pid, SIGSTOP) == 0 && paused_pid_count < JC_MAX_PAUSED_PIDS)
            paused_pids[paused_pid_count++] = pid;
    }
    closedir(dir);
}

static void resume_trimui_inputd(void)
{
    for (int i = 0; i < paused_pid_count; i++) {
        if (paused_pids[i] > 0)
            kill(paused_pids[i], SIGCONT);
    }
    paused_pid_count = 0;
}

int jc_raw_begin_calibration(char *err, size_t err_size)
{
    const char *path = calibrating_flag_path();
    char dir[JC_PATH_MAX];
    if (parent_dir(path, dir, sizeof(dir)) != 0 || mkdir_p(dir) != 0) {
        if (err && err_size > 0)
            snprintf(err, err_size, "Could not create parent directory for %s", path);
        return -1;
    }
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        if (err && err_size > 0)
            snprintf(err, err_size, "Could not create %s: %s", path, strerror(errno));
        return -1;
    }
    close(fd);
    pause_trimui_inputd();
    return 0;
}

void jc_raw_end_calibration(void)
{
    resume_trimui_inputd();
    unlink(calibrating_flag_path());
}

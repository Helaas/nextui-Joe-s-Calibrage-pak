#include "calibrage.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

#ifndef O_NOCTTY
#define O_NOCTTY 0
#endif

const char *jc_raw_device_path(void)
{
    const char *env = getenv("CALIBRAGE_RAW_DEVICE");
    return (env && env[0] != '\0') ? env : "/dev/ttyS1";
}

static const char *calibrating_flag_path(void)
{
    const char *env = getenv("CALIBRAGE_CALIBRATING_FLAG");
    return (env && env[0] != '\0') ? env : "/tmp/joypad_calibrating";
}

void jc_raw_reader_init(jc_raw_reader *reader)
{
    if (!reader)
        return;
    memset(reader, 0, sizeof(*reader));
    reader->fd = -1;
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

static void configure_serial_best_effort(int fd)
{
    struct termios tio;
    if (tcgetattr(fd, &tio) != 0)
        return;
    cfsetispeed(&tio, B9600);
    cfsetospeed(&tio, B9600);
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

int jc_raw_reader_open(jc_raw_reader *reader)
{
    if (!reader)
        return -1;
    jc_raw_reader_close(reader);
    reader->fd = open(jc_raw_device_path(),
                      O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOCTTY);
    if (reader->fd < 0) {
        set_reader_error(reader, "Could not open %s: %s", jc_raw_device_path(),
                         strerror(errno));
        return -1;
    }
    configure_serial_best_effort(reader->fd);
    return 0;
}

void jc_raw_reader_close(jc_raw_reader *reader)
{
    if (!reader)
        return;
    if (reader->fd >= 0) {
        close(reader->fd);
        reader->fd = -1;
    }
    reader->packet_pos = 0;
}

int jc_raw_parse_packet(const unsigned char packet[6], jc_raw_sample *out)
{
    if (!packet || !out)
        return -1;
    if (packet[0] != 0xff || packet[5] != 0xfe)
        return -1;
    out->left_y = packet[1];
    out->left_x = packet[2];
    out->right_y = packet[3];
    out->right_x = packet[4];
    out->valid = true;
    return 0;
}

int jc_raw_parse_byte(jc_raw_reader *reader, unsigned char byte, jc_raw_sample *out)
{
    if (!reader || !out)
        return -1;
    if (reader->packet_pos == 0) {
        if (byte != 0xff)
            return 0;
        reader->packet[reader->packet_pos++] = byte;
        return 0;
    }

    reader->packet[reader->packet_pos++] = byte;
    if (reader->packet_pos < 6)
        return 0;

    reader->packet_pos = 0;
    if (jc_raw_parse_packet(reader->packet, out) == 0) {
        reader->last = *out;
        return 1;
    }
    if (byte == 0xff) {
        reader->packet[0] = byte;
        reader->packet_pos = 1;
    }
    return 0;
}

int jc_raw_reader_poll(jc_raw_reader *reader, jc_raw_sample *out)
{
    if (!reader || !out || reader->fd < 0)
        return -1;

    unsigned char buf[64];
    int got = 0;
    for (;;) {
        ssize_t n = read(reader->fd, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            set_reader_error(reader, "Could not read %s: %s", jc_raw_device_path(),
                             strerror(errno));
            return -1;
        }
        if (n == 0)
            break;
        for (ssize_t i = 0; i < n; i++) {
            jc_raw_sample sample = {0};
            if (jc_raw_parse_byte(reader, buf[i], &sample) == 1) {
                *out = sample;
                got = 1;
            }
        }
        if (n < (ssize_t)sizeof(buf))
            break;
    }
    return got;
}

int jc_raw_begin_calibration(char *err, size_t err_size)
{
    const char *path = calibrating_flag_path();
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        if (err && err_size > 0)
            snprintf(err, err_size, "Could not create %s: %s", path, strerror(errno));
        return -1;
    }
    close(fd);
    return 0;
}

void jc_raw_end_calibration(void)
{
    unlink(calibrating_flag_path());
}

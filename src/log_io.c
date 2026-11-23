#define _POSIX_C_SOURCE 200809L

#include "log_io.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int g_log_fd = -1;

static void write_all(int fd, const char *buf, size_t len) {
    size_t written = 0;
    while (written < len) {
        ssize_t n = write(fd, buf + written, len - written);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        written += (size_t)n;
    }
}

static void format_timestamp(char *out, size_t out_size) {
    time_t now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    strftime(out, out_size, "%Y-%m-%d %H:%M:%S", &tm_buf);
}

int log_open(const char *filepath) {
    if (filepath == NULL) {
        return -1;
    }

    int fd = open(filepath, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) {
        return -1;
    }

    g_log_fd = fd;
    return 0;
}

void log_close(void) {
    if (g_log_fd >= 0) {
        close(g_log_fd);
        g_log_fd = -1;
    }
}

const char *proc_state_name(ProcState state) {
    switch (state) {
        case PROC_UNUSED:
            return "UNUSED";
        case PROC_RUNNING:
            return "RUNNING";
        case PROC_EXITED_NORMAL:
            return "EXITED_NORMAL";
        case PROC_STOPPED_LIMIT:
            return "STOPPED_LIMIT";
        case PROC_TERMINATED:
            return "TERMINATED";
        default:
            return "UNKNOWN";
    }
}

void log_write(const char *tag, const char *fmt, ...) {
    if (g_log_fd < 0 || tag == NULL || fmt == NULL) {
        return;
    }

    char ts[32];
    char msg[512];
    char line[600];
    va_list args;

    format_timestamp(ts, sizeof(ts));

    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    int len = snprintf(line, sizeof(line), "[%s] [%-8s] %s\n", ts, tag, msg);
    if (len < 0) {
        return;
    }
    if ((size_t)len >= sizeof(line)) {
        len = (int)(sizeof(line) - 1);
        line[len - 1] = '\n';
    }

    write_all(g_log_fd, line, (size_t)len);
}

void log_status_report(const ChildProcess *table, int count) {
    if (g_log_fd < 0 || table == NULL) {
        return;
    }

    log_write("STATUS", "SIGUSR1 status report (%d tracked workers)", count);
    for (int i = 0; i < count; i++) {
        const ChildProcess *c = &table[i];
        log_write("STATUS", "  worker=%s pid=%d state=%s mode=%s restarts=%d/%d",
                  c->name,
                  (int)c->pid,
                  proc_state_name(c->state),
                  c->mode,
                  c->restart_count,
                  MAX_RESTARTS);
    }
}
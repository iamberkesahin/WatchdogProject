#define _XOPEN_SOURCE 700

#include "signals.h"

#include <stddef.h>
#include <sys/time.h>

volatile sig_atomic_t g_shutdown_flag = 0;
volatile sig_atomic_t g_shutdown_signal = 0;
volatile sig_atomic_t g_status_flag = 0;
volatile sig_atomic_t g_child_flag = 0;
volatile sig_atomic_t g_alarm_flag = 0;

static void handle_shutdown(int signo) {
    g_shutdown_signal = signo;
    g_shutdown_flag = 1;
}

static void handle_status(int signo) {
    (void)signo;
    g_status_flag = 1;
}

static void handle_child(int signo) {
    (void)signo;
    g_child_flag = 1;
}

static void handle_alarm(int signo) {
    (void)signo;
    g_alarm_flag = 1;
}

static int register_handler(int signo, void (*fn)(int), int flags) {
    struct sigaction sa;

    sa.sa_handler = fn;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = flags;

    return sigaction(signo, &sa, NULL);
}

int signals_install(void) {
    if (register_handler(SIGINT, handle_shutdown, 0) == -1) {
        return -1;
    }
    if (register_handler(SIGTERM, handle_shutdown, 0) == -1) {
        return -1;
    }
    if (register_handler(SIGUSR1, handle_status, 0) == -1) {
        return -1;
    }
    if (register_handler(SIGCHLD, handle_child, SA_NOCLDSTOP) == -1) {
        return -1;
    }
    if (register_handler(SIGALRM, handle_alarm, 0) == -1) {
        return -1;
    }
    return 0;
}

int signals_start_timer(int interval_sec) {
    struct itimerval timer;

    if (interval_sec <= 0) {
        return -1;
    }

    timer.it_value.tv_sec = interval_sec;
    timer.it_value.tv_usec = 0;
    timer.it_interval.tv_sec = interval_sec;
    timer.it_interval.tv_usec = 0;

    return setitimer(ITIMER_REAL, &timer, NULL);
}

void signals_stop_timer(void) {
    struct itimerval timer;

    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = 0;
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = 0;

    setitimer(ITIMER_REAL, &timer, NULL);
}
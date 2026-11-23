#define _POSIX_C_SOURCE 200809L

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t g_stop = 0;

static void handle_stop(int signo) {
    (void)signo;
    g_stop = 1;
}

static void sleep_interruptible(int seconds) {
    int remaining = seconds;
    while (remaining > 0 && !g_stop) {
        remaining = (int)sleep((unsigned int)remaining);
    }
}

int main(int argc, char *argv[]) {
    const char *name = (argc > 1) ? argv[1] : "worker";
    const char *mode = (argc > 2) ? argv[2] : "daemon";
    int duration = (argc > 3) ? atoi(argv[3]) : 5;

    if (duration <= 0) {
        duration = 5;
    }

    if (strcmp(mode, "stubborn") == 0) {
        struct sigaction sa;
        sa.sa_handler = SIG_IGN;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(SIGTERM, &sa, NULL);
        sigaction(SIGINT, &sa, NULL);

        while (1) {
            pause();
        }
        return 0;
    }

    struct sigaction sa;
    sa.sa_handler = handle_stop;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    (void)name;

    if (strcmp(mode, "daemon") == 0) {
        while (!g_stop) {
            pause();
        }
        return 0;
    }

    if (strcmp(mode, "normal") == 0) {
        sleep_interruptible(duration);
        return 0;
    }

    if (strcmp(mode, "fail") == 0) {
        sleep_interruptible(duration);
        if (g_stop) {
            return 0;
        }
        return 1;
    }

    fprintf(stderr, "Unknown worker mode: %s\n", mode);
    return 2;
}
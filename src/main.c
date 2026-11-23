#define _POSIX_C_SOURCE 200809L

#include "common.h"
#include "log_io.h"
#include "signals.h"
#include "supervisor.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int parse_worker_spec(const char *spec) {
    char buf[96];
    char *first_colon;
    char *second_colon;
    const char *mode;
    const char *arg = NULL;

    snprintf(buf, sizeof(buf), "%s", spec);

    first_colon = strchr(buf, ':');
    if (first_colon == NULL || first_colon == buf || *(first_colon + 1) == '\0') {
        return -1;
    }
    *first_colon = '\0';
    mode = first_colon + 1;

    second_colon = strchr(mode, ':');
    if (second_colon != NULL) {
        *second_colon = '\0';
        if (*(second_colon + 1) != '\0') {
            arg = second_colon + 1;
        }
    }

    return supervisor_register(buf, mode, arg);
}

static void register_default_workers(void) {
    supervisor_register("worker-1", "daemon", NULL);
    supervisor_register("worker-2", "normal", "8");
    supervisor_register("worker-3", "daemon", NULL);
}

int main(int argc, char *argv[]) {
    const char *log_path = DEFAULT_LOG_PATH;
    int shutdown_timeout = SHUTDOWN_TIMEOUT_SEC;
    int check_interval = CHECK_INTERVAL_SEC;
    int custom_workers = 0;
    int opt;

    supervisor_init();

    while ((opt = getopt(argc, argv, "l:w:t:i:")) != -1) {
        switch (opt) {
            case 'l':
                log_path = optarg;
                break;
            case 'w':
                if (parse_worker_spec(optarg) != 0) {
                    fprintf(stderr, "Invalid worker spec: %s (expected name:mode[:arg])\n", optarg);
                    return 1;
                }
                custom_workers++;
                break;
            case 't':
                shutdown_timeout = atoi(optarg);
                if (shutdown_timeout <= 0) {
                    shutdown_timeout = SHUTDOWN_TIMEOUT_SEC;
                }
                break;
            case 'i':
                check_interval = atoi(optarg);
                if (check_interval <= 0) {
                    check_interval = CHECK_INTERVAL_SEC;
                }
                break;
            default:
                fprintf(stderr, "Usage: %s [-l logfile] [-w name:mode[:arg]] [-t timeout] [-i interval]\n", argv[0]);
                return 1;
        }
    }

    if (custom_workers == 0) {
        register_default_workers();
    }

    if (log_open(log_path) != 0) {
        fprintf(stderr, "Failed to open log file: %s\n", log_path);
        return 1;
    }

    if (signals_install() != 0) {
        log_write("ERROR", "failed to install signal handlers");
        log_close();
        return 1;
    }

    log_write("INIT", "watchdog started (pid=%d, log=%s)", (int)getpid(), log_path);
    printf("watchdog running (PID: %d). Log file: %s\n", (int)getpid(), log_path);
    fflush(stdout);

    if (supervisor_start_all() == 0) {
        log_write("ERROR", "no worker processes could be started");
        log_close();
        return 1;
    }

    if (signals_start_timer(check_interval) != 0) {
        log_write("WARN", "failed to start periodic timer");
    }

    sigset_t block_set;
    sigset_t orig_set;
    sigemptyset(&block_set);
    sigaddset(&block_set, SIGINT);
    sigaddset(&block_set, SIGTERM);
    sigaddset(&block_set, SIGUSR1);
    sigaddset(&block_set, SIGCHLD);
    sigaddset(&block_set, SIGALRM);

    while (!g_shutdown_flag) {
        sigprocmask(SIG_BLOCK, &block_set, &orig_set);
        if (!g_shutdown_flag && !g_child_flag && !g_status_flag && !g_alarm_flag) {
            sigsuspend(&orig_set);
        }
        sigprocmask(SIG_SETMASK, &orig_set, NULL);

        if (g_child_flag) {
            g_child_flag = 0;
            supervisor_handle_children(1);
        }
        if (g_status_flag) {
            g_status_flag = 0;
            supervisor_report_status();
        }
        if (g_alarm_flag) {
            g_alarm_flag = 0;
            supervisor_periodic_check();
        }
    }

    signals_stop_timer();
    log_write("SHUTDOWN", "received signal %d, starting graceful shutdown", (int)g_shutdown_signal);
    supervisor_shutdown(shutdown_timeout);
    log_write("EXIT", "watchdog stopped cleanly");
    log_close();

    printf("watchdog stopped cleanly.\n");
    return 0;
}
#define _POSIX_C_SOURCE 200809L

#include "supervisor.h"
#include "signals.h"
#include "log_io.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static ChildProcess g_children[MAX_CHILDREN];
static int g_child_count = 0;

static ChildProcess *find_by_pid(pid_t pid) {
    for (int i = 0; i < g_child_count; i++) {
        if (g_children[i].pid == pid) {
            return &g_children[i];
        }
    }
    return NULL;
}

static int spawn_child(ChildProcess *child, int restarting) {
    sigset_t stop_signals, saved, pending;
    sigemptyset(&stop_signals);
    sigaddset(&stop_signals, SIGINT);
    sigaddset(&stop_signals, SIGTERM);
    if (sigprocmask(SIG_BLOCK, &stop_signals, &saved) == -1) {
        log_write("ERROR", "cannot block shutdown signals: %s", strerror(errno));
        return -1;
    }

    /* Treat each launch as one decision relative to the shutdown handler. */
    if (sigpending(&pending) == -1) {
        log_write("ERROR", "cannot read pending signals: %s", strerror(errno));
        sigprocmask(SIG_SETMASK, &saved, NULL);
        return -1;
    }
    if (g_shutdown_flag || sigismember(&pending, SIGINT) || sigismember(&pending, SIGTERM)) {
        child->state = PROC_TERMINATED;
        sigprocmask(SIG_SETMASK, &saved, NULL);
        return 0;
    }

    if (restarting) {
        child->restart_count++;
        log_write("RESTART", "worker=%s attempt=%d/%d",
                  child->name, child->restart_count, MAX_RESTARTS);
    }
    pid_t pid = fork();
    if (pid < 0) {
        log_write("ERROR", "fork failed for %s: %s", child->name, strerror(errno));
        sigprocmask(SIG_SETMASK, &saved, NULL);
        return -1;
    }

    if (pid == 0) {
        struct sigaction action = {0};
        action.sa_handler = SIG_DFL;
        sigemptyset(&action.sa_mask);
        if (sigaction(SIGTERM, &action, NULL) == -1 ||
            sigaction(SIGINT, &action, NULL) == -1 ||
            sigprocmask(SIG_SETMASK, &saved, NULL) == -1) {
            _exit(127);
        }
        if (child->arg[0] != '\0') {
            execl(WORKER_PATH, WORKER_PATH, child->name, child->mode, child->arg, (char *)NULL);
        } else {
            execl(WORKER_PATH, WORKER_PATH, child->name, child->mode, (char *)NULL);
        }
        perror("execl");
        _exit(127);
    }

    child->pid = pid;
    child->state = PROC_RUNNING;
    child->started_at = time(NULL);

    log_write("START", "worker=%s pid=%d mode=%s restarts=%d",
              child->name, (int)pid, child->mode, child->restart_count);
    sigprocmask(SIG_SETMASK, &saved, NULL);
    return 1;
}

void supervisor_init(void) {
    memset(g_children, 0, sizeof(g_children));
    for (int i = 0; i < MAX_CHILDREN; i++) {
        g_children[i].pid = -1;
        g_children[i].state = PROC_UNUSED;
    }
    g_child_count = 0;
}

int supervisor_register(const char *name, const char *mode, const char *arg) {
    if (g_child_count >= MAX_CHILDREN || name == NULL || mode == NULL) {
        return -1;
    }

    ChildProcess *c = &g_children[g_child_count];
    c->id = g_child_count + 1;
    c->pid = -1;
    snprintf(c->name, sizeof(c->name), "%s", name);
    snprintf(c->mode, sizeof(c->mode), "%s", mode);
    if (arg != NULL) {
        snprintf(c->arg, sizeof(c->arg), "%s", arg);
    } else {
        c->arg[0] = '\0';
    }
    c->state = PROC_UNUSED;
    c->restart_count = 0;
    c->started_at = 0;

    g_child_count++;
    return 0;
}

int supervisor_start_all(void) {
    int started = 0;
    for (int i = 0; i < g_child_count; i++) {
        if (g_shutdown_flag) {
            break;
        }
        if (spawn_child(&g_children[i], 0) > 0) {
            started++;
        }
    }
    return started;
}

void supervisor_handle_children(int allow_restart) {
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        ChildProcess *c = find_by_pid(pid);
        if (c == NULL) {
            continue;
        }

        c->pid = -1;

        if (!allow_restart || g_shutdown_flag) {
            c->state = PROC_TERMINATED;
            log_write("EXIT", "worker=%s pid=%d terminated during shutdown",
                      c->name, (int)pid);
            continue;
        }

        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            c->state = PROC_EXITED_NORMAL;
            log_write("EXIT", "worker=%s pid=%d exited normally (code=0)",
                      c->name, (int)pid);
            continue;
        }

        if (WIFEXITED(status)) {
            log_write("WARN", "worker=%s pid=%d exited unexpectedly (code=%d)",
                      c->name, (int)pid, WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            log_write("WARN", "worker=%s pid=%d killed by signal %d",
                      c->name, (int)pid, WTERMSIG(status));
        } else {
            log_write("WARN", "worker=%s pid=%d terminated abnormally",
                      c->name, (int)pid);
        }

        if (c->restart_count < MAX_RESTARTS) {
            if (spawn_child(c, 1) < 0) {
                c->state = PROC_STOPPED_LIMIT;
            }
        } else {
            c->state = PROC_STOPPED_LIMIT;
            log_write("LIMIT", "worker=%s reached max restarts (%d), not restarting",
                      c->name, MAX_RESTARTS);
        }
    }
}

void supervisor_periodic_check(void) {
    int alive = 0;

    supervisor_handle_children(1);

    for (int i = 0; i < g_child_count; i++) {
        if (g_children[i].state == PROC_RUNNING && g_children[i].pid > 0) {
            if (kill(g_children[i].pid, 0) == 0) {
                alive++;
            }
        }
    }

    log_write("CHECK", "periodic check complete: %d/%d workers running",
              alive, g_child_count);
}

void supervisor_report_status(void) {
    log_status_report(g_children, g_child_count);
}

int supervisor_active_count(void) {
    int count = 0;
    for (int i = 0; i < g_child_count; i++) {
        if (g_children[i].state == PROC_RUNNING && g_children[i].pid > 0) {
            count++;
        }
    }
    return count;
}

void supervisor_shutdown(int timeout_sec) {
    struct timespec start, now, step;

    log_write("SHUTDOWN", "sending SIGTERM to active workers (timeout=%ds)", timeout_sec);

    for (int i = 0; i < g_child_count; i++) {
        if (g_children[i].state == PROC_RUNNING && g_children[i].pid > 0) {
            if (kill(g_children[i].pid, SIGTERM) == -1 && errno != ESRCH) {
                log_write("ERROR", "SIGTERM failed for worker=%s pid=%d: %s",
                          g_children[i].name, (int)g_children[i].pid, strerror(errno));
            }
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &start);
    step.tv_sec = 0;
    step.tv_nsec = 100000000L;

    while (supervisor_active_count() > 0) {
        supervisor_handle_children(0);
        if (supervisor_active_count() == 0) {
            break;
        }

        clock_gettime(CLOCK_MONOTONIC, &now);
        double elapsed = (double)(now.tv_sec - start.tv_sec) +
                         (double)(now.tv_nsec - start.tv_nsec) / 1e9;
        if (elapsed >= (double)timeout_sec) {
            break;
        }
        nanosleep(&step, NULL);
    }

    for (int i = 0; i < g_child_count; i++) {
        if (g_children[i].state == PROC_RUNNING && g_children[i].pid > 0) {
            pid_t target = g_children[i].pid;
            log_write("KILL", "worker=%s pid=%d did not exit in time, sending SIGKILL",
                      g_children[i].name, (int)target);
            kill(target, SIGKILL);

            int status;
            while (waitpid(target, &status, 0) == -1 && errno == EINTR) {
            }
            g_children[i].pid = -1;
            g_children[i].state = PROC_TERMINATED;
        }
    }

    supervisor_handle_children(0);
    log_write("SHUTDOWN", "all child processes stopped and reaped");
}
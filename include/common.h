#ifndef COMMON_H
#define COMMON_H

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <sys/types.h>
#include <time.h>

#define MAX_CHILDREN 8
#define MAX_RESTARTS 3
#define SHUTDOWN_TIMEOUT_SEC 5
#define CHECK_INTERVAL_SEC 5
#define DEFAULT_LOG_PATH "watchdog.log"
#define WORKER_PATH "./worker"

typedef enum {
    PROC_UNUSED = 0,
    PROC_RUNNING,
    PROC_EXITED_NORMAL,
    PROC_STOPPED_LIMIT,
    PROC_TERMINATED
} ProcState;

typedef struct {
    int id;
    pid_t pid;
    char name[32];
    char mode[32];
    char arg[32];
    ProcState state;
    int restart_count;
    time_t started_at;
} ChildProcess;

#endif
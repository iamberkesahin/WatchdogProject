# Watchdog - POSIX Signal-Based Process Supervisor

A modular C application developed for the Sakarya University Software Engineering System Programming term project. The `watchdog` supervisor launches, monitors, restarts, and gracefully shuts down child processes using POSIX system calls.

## Features

- Child process creation using `fork()` and `execl()`
- Async-signal-safe signal handling via `sigaction()` and `volatile sig_atomic_t` flags
- Race-free main event loop using `sigprocmask()` and `sigsuspend()`
- Zombie-free reaping with `SIGCHLD` and non-blocking `waitpid(-1, &status, WNOHANG)`
- Distinction between normal exits (`exit(0)`) and unexpected terminations (non-zero exit or signal)
- Bounded automatic restart (maximum 3 attempts per worker)
- Periodic health checks using `setitimer(ITIMER_REAL)` and `SIGALRM`
- On-demand status reporting to the log file via `SIGUSR1`
- Graceful shutdown on `SIGINT` (Ctrl+C) or `SIGTERM` with `SIGKILL` fallback after timeout
- Timestamped event logging using POSIX `open()` and `write()`

## Project Structure

```text
WatchdogProject/
├── Makefile
├── README.md
├── include/
│   ├── common.h
│   ├── log_io.h
│   ├── signals.h
│   └── supervisor.h
├── src/
│   ├── main.c
│   ├── signals.c
│   ├── supervisor.c
│   └── log_io.c
├── workers/
│   └── worker.c
├── tests/
│   ├── test_scenarios.sh
│   └── sample_watchdog.log
└── docs/
    └── REPORT.pdf
CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -Iinclude

WATCHDOG_SRCS = src/main.c src/signals.c src/supervisor.c src/log_io.c
WATCHDOG_OBJS = $(WATCHDOG_SRCS:.c=.o)
WORKER_SRC = workers/worker.c

.PHONY: all clean test

all: watchdog worker

watchdog: $(WATCHDOG_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

worker: $(WORKER_SRC)
	$(CC) $(CFLAGS) -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

test: all
	bash tests/test_scenarios.sh
	python3 tests/test_shutdown_overlap.py

clean:
	rm -f $(WATCHDOG_OBJS) watchdog worker *.log
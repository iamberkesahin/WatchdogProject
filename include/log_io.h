#ifndef LOG_IO_H
#define LOG_IO_H

#include "common.h"

int log_open(const char *filepath);
void log_close(void);
void log_write(const char *tag, const char *fmt, ...);
void log_status_report(const ChildProcess *table, int count);
const char *proc_state_name(ProcState state);

#endif
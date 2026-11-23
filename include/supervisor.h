#ifndef SUPERVISOR_H
#define SUPERVISOR_H

#include "common.h"

void supervisor_init(void);
int supervisor_register(const char *name, const char *mode, const char *arg);
int supervisor_start_all(void);
void supervisor_handle_children(int allow_restart);
void supervisor_periodic_check(void);
void supervisor_report_status(void);
void supervisor_shutdown(int timeout_sec);
int supervisor_active_count(void);

#endif
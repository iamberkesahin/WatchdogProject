#ifndef SIGNALS_H
#define SIGNALS_H

#include <signal.h>

extern volatile sig_atomic_t g_shutdown_flag;
extern volatile sig_atomic_t g_shutdown_signal;
extern volatile sig_atomic_t g_status_flag;
extern volatile sig_atomic_t g_child_flag;
extern volatile sig_atomic_t g_alarm_flag;

int signals_install(void);
int signals_start_timer(int interval_sec);
void signals_stop_timer(void);

#endif
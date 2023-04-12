#ifndef __PYROSCOPE_API_H
#define __PYROSCOPE_API_H

#include <sys/types.h>

extern void phpspy_init_spy(const char *args);
extern int phpspy_init_pid(int pid_i, void *err_ptr, int err_len);
extern int phpspy_cleanup(int pid_i, void *err_ptr, int err_len);
extern int phpspy_snapshot(int pid_i, void *ptr, int len, void *err_ptr,
                           int err_len);

#endif

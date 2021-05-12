#ifndef __PYROSCOPE_API_H
#define __PYROSCOPE_API_H

#include <sys/types.h>

extern int phpspy_init(pid_t pid, void *err_ptr, int err_len);
extern int phpspy_cleanup(pid_t pid, void *err_ptr, int err_len);
extern int phpspy_snapshot(pid_t pid, void *ptr, int len, void *err_ptr,
                           int err_len);

#endif

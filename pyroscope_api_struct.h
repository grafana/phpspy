#ifndef __PYROSCOPE_API_STRUCT_H
#define __PYROSCOPE_API_STRUCT_H

#include <limits.h>
#include <unistd.h>

#include "phpspy.h"

#define PYROSCOPE_MAX_STACK_DEPTH 64

typedef struct pyroscope_context_t {
  pid_t pid;
  char app_root_dir[PATH_MAX];
  trace_frame_t frames[PYROSCOPE_MAX_STACK_DEPTH];
  struct trace_context_s phpspy_context;
  int (*do_trace_ptr)(trace_context_t *context);
  struct pyroscope_context_t *next;
} pyroscope_context_t;

#endif

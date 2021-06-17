#ifndef __PYROSCOPE_API_STRUCT_H
#define __PYROSCOPE_API_STRUCT_H

#include <limits.h>
#include <unistd.h>

#include "phpspy.h"

typedef struct pyroscope_context_t {
  pid_t pid;
  char app_root_dir[PATH_MAX];
  trace_frame_t frames[MAX_STACK_DEPTH];
  struct trace_context_s phpspy_context;
  struct pyroscope_context_t *next;
} pyroscope_context_t;

#endif

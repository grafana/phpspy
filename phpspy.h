#ifndef __PHPSPY_H
#define __PHPSPY_H

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <main/php_config.h>
#include <pthread.h>
#include <regex.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#undef ZEND_DEBUG
#define ZEND_DEBUG 0
#include <main/SAPI.h>
#undef snprintf
#undef vsnprintf
#undef HASH_ADD

#define MAX_STACK_DEPTH 64

#include <uthash.h>

// clang-format off

#define try(__rv, __call)                       \
  do {                                           \
    if (((__rv) = (__call)) != 0) return (__rv); \
  } while (0)

// clang-format on

#define PHPSPY_VERSION "0.6.0"
#define PHPSPY_MIN(a, b) ((a) < (b) ? (a) : (b))
#define PHPSPY_MAX(a, b) ((a) > (b) ? (a) : (b))
#define PHPSPY_STR_SIZE 256
#define PHPSPY_MAX_ARRAY_BUCKETS 128
#define PHPSPY_MAX_ARRAY_TABLE_SIZE 512

#define PHPSPY_OK 0
#define PHPSPY_ERR 1
#define PHPSPY_ERR_PID_DEAD 2
#define PHPSPY_ERR_BUF_FULL 4

#define PHPSPY_TRACE_EVENT_INIT 0
#define PHPSPY_TRACE_EVENT_STACK_BEGIN 1
#define PHPSPY_TRACE_EVENT_FRAME 2
#define PHPSPY_TRACE_EVENT_VARPEEK 3
#define PHPSPY_TRACE_EVENT_GLOPEEK 4
#define PHPSPY_TRACE_EVENT_REQUEST 5
#define PHPSPY_TRACE_EVENT_MEM 6
#define PHPSPY_TRACE_EVENT_STACK_END 7
#define PHPSPY_TRACE_EVENT_ERROR 8
#define PHPSPY_TRACE_EVENT_DEINIT 9

typedef struct trace_loc_s {
  char func[PHPSPY_STR_SIZE];
  char class_name[PHPSPY_STR_SIZE];
  char file[PHPSPY_STR_SIZE];
  size_t func_len;
  size_t class_len;
  size_t file_len;
  int lineno;
} trace_loc_t;

typedef struct trace_frame_s {
  trace_loc_t loc;
  int depth;
} trace_frame_t;

typedef struct trace_target_s {
  pid_t pid;
  int mem_fd;
  uint64_t executor_globals_addr;
  // uint64_t sapi_globals_addr; // TODO: Needed?
  uint64_t basic_functions_module_addr;  // TODO: Needed?
} trace_target_t;

typedef struct trace_context_s {
  trace_target_t target;
  struct {
    trace_frame_t frame;
  } event;
  void *event_udata;
  int (*event_handler)(struct trace_context_s *context, int event_type);
  char buf[PHPSPY_STR_SIZE];
  size_t buf_len;
} trace_context_t;

typedef struct addr_memo_s {
  char php_bin_path[PHPSPY_STR_SIZE];
  char php_bin_path_root[PHPSPY_STR_SIZE];
  uint64_t php_base_addr;
} addr_memo_t;

int get_symbol_addr(addr_memo_t *memo, pid_t pid, const char *symbol,
                    uint64_t *raddr);
int find_addresses(trace_target_t *target);
int copy_proc_mem(trace_target_t *target, const char *what, void *raddr,
                  void *laddr, size_t size);
void log_error(const char *fmt, ...);
int do_trace(trace_context_t *context);
int initialize(pid_t pid, struct trace_context_s *context, void *event_udata,
               int (*event_handler)(struct trace_context_s *context,
                                    int event_type));
void deinitialize(struct trace_context_s *context);

#endif

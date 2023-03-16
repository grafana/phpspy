#define _GNU_SOURCE 1
#include <sys/uio.h>
#include "pyroscope_api.h"
#include "pyroscope_api_struct.h"
#include "phpspy.h"
#include <unistd.h>

char *opt_libname_awk_patt = "libphp[78]?";
int log_error_enabled = 1;

int opt_max_stack_depth = PYROSCOPE_MAX_STACK_DEPTH;

int opt_capture_req = 0;
int opt_capture_req_qstring = 0;
int opt_capture_req_cookie = 0;
int opt_capture_req_uri = 0;
int opt_capture_req_path = 0;
int opt_capture_mem = 0;

int opt_continue_on_error = 0;


varpeek_entry_t *varpeek_map = NULL;
glopeek_entry_t *glopeek_map = NULL;

static int get_php_version(trace_target_t *target);
static int do_trace_70(trace_context_t *context);
static int do_trace_71(trace_context_t *context);
static int do_trace_72(trace_context_t *context);
static int do_trace_73(trace_context_t *context);
static int do_trace_74(trace_context_t *context);
static int do_trace_80(trace_context_t *context);
static int do_trace_81(trace_context_t *context);
static int do_trace_82(trace_context_t *context);
static int do_trace_83(trace_context_t *context);

int find_addresses(trace_target_t *target);
int initialize(pid_t pid,
               struct pyroscope_context_t *pyroscope_context,
               void *event_udata,
               int (*event_handler)(struct trace_context_s *context, int event_type)
);

void deinitialize(struct pyroscope_context_t *pyroscope_context);

pyroscope_context_t *first_ctx = NULL;

pyroscope_context_t *allocate_context() {
  if (NULL == first_ctx) {
    first_ctx = calloc(sizeof(pyroscope_context_t), 1);
    return first_ctx;
  }

  pyroscope_context_t *current = first_ctx;
  while (1) {
    if (NULL == current->next) {
      current->next = calloc(sizeof(pyroscope_context_t), 1);
      return current->next;
    } else {
      current = current->next;
    }
  }
}

void deallocate_context(pyroscope_context_t *ctx) {
  if (ctx == first_ctx) {
    first_ctx = ctx->next;
  } else {
    pyroscope_context_t *iter = first_ctx;
    while (1) {
      if (iter->next == ctx) {
        iter->next = ctx->next;
        break;
      }
      iter = iter->next;
    }
  }

  free(ctx);
}

pyroscope_context_t *find_matching_context(pid_t pid) {
  pyroscope_context_t *ctx = first_ctx;

  while (1) {
    if (NULL == ctx) {
      return NULL;
    }

    if (ctx->pid == pid) {
      return ctx;
    } else {
      ctx = ctx->next;
    }
  }
}

int event_handler(struct trace_context_s *context, int event_type) {
  switch (event_type) {
    case PHPSPY_TRACE_EVENT_FRAME: {
      trace_frame_t *frames = (trace_frame_t *)context->event_udata;
      memcpy(&frames[context->event.frame.depth], &context->event.frame,
             sizeof(trace_frame_t));
      break;
    }
    case PHPSPY_TRACE_EVENT_ERROR: {
      return PHPSPY_TRACE_EVENT_ERROR;
    }
  }
  return PHPSPY_OK;
}

int formulate_error_msg(int rv, struct trace_context_s *context, char *err_ptr,
                        int err_len) {
  int err_msg_len = 0;
  if (rv != (PHPSPY_OK)) {
    switch (rv) {
      case (((unsigned int)PHPSPY_ERR) & ((unsigned int)PHPSPY_ERR_PID_DEAD)): {
        err_msg_len = snprintf((char *)err_ptr, err_len,
                               "App with PID %d is dead!", context->target.pid);
        break;
      }
      case (PHPSPY_ERR): {
        err_msg_len = snprintf((char *)err_ptr, err_len, "General error!");
        break;
      }
      default: {
        err_msg_len =
            snprintf((char *)err_ptr, err_len, "Unknown error code: %u",
                     (unsigned int)rv & ~((unsigned int)PHPSPY_ERR));
        break;
      }
    }
    return err_msg_len < err_len ? -err_msg_len : -err_len;
  }
  return 0;
}

void get_process_cwd(char *app_cwd, pid_t pid) {
  char buf[PATH_MAX];
  snprintf(buf, PATH_MAX, "/proc/%d/cwd", pid);
  int app_cwd_len = readlink(buf, app_cwd, PATH_MAX);

  if (app_cwd_len < 0) {
    app_cwd[0] = '\0';
  }
}

int formulate_output(struct trace_context_s *context, const char *app_root_dir,
                     char *data_ptr, int data_len, void *err_ptr, int err_len) {
  int written = 0;
  const int nof_frames = context->event.frame.depth;

  for (int current_frame_idx = (nof_frames - 1); current_frame_idx >= 0;
       current_frame_idx--) {
    trace_frame_t *frames = (trace_frame_t *)context->event_udata;
    trace_loc_t *loc = &frames[current_frame_idx].loc;

    char *write_cursor = ((char *)data_ptr) + written;

    int file_path_beginning = 0;
    if (app_root_dir[0] != '\0') {
      const int root_path_len = strlen(app_root_dir);
      if (memcmp(loc->file, app_root_dir, root_path_len) == 0) {
        file_path_beginning = root_path_len + 1;
      }
    }

    if (loc->lineno == -1) {
      char out_fmt[] = "%s - %s%s%s;";
      written += snprintf(write_cursor, data_len, out_fmt,
                          &loc->file[file_path_beginning], loc->class,
                          loc->class_len ? "::" : "", loc->func);
    } else {
      char out_fmt[] = "%s:%d - %s%s%s;";
      written += snprintf(
          write_cursor, data_len, out_fmt, &loc->file[file_path_beginning],
          loc->lineno, loc->class, loc->class_len ? "::" : "", loc->func);
    }

    if (written > data_len) {
      int err_msg_len =
          snprintf((char *)err_ptr, err_len, "Not enough space! %d > %d",
                   written, data_len);
      return -err_msg_len;
    }
  }
  return written;
}

int phpspy_init(pid_t pid, void *err_ptr, int err_len) {
  int rv = 0;

  pyroscope_context_t *pyroscope_context = allocate_context();
  pyroscope_context->pid = pid;
  get_process_cwd(&pyroscope_context->app_root_dir[0], pid);
  try
    (rv, formulate_error_msg(
             initialize(pid, pyroscope_context,
                        &pyroscope_context->frames[0], event_handler),
             &pyroscope_context->phpspy_context, err_ptr, err_len));

  return rv;
}

int phpspy_snapshot(pid_t pid, void *ptr, int len, void *err_ptr, int err_len) {
  int rv = 0;

  pyroscope_context_t *pyroscope_context = find_matching_context(pid);

  if (NULL == pyroscope_context) {
    int err_msg_len = snprintf((char *)err_ptr, err_len,
                               "Phpspy not initialized for %d pid", pid);
    return -err_msg_len;
  }

  try
    (rv,
     formulate_error_msg(pyroscope_context->do_trace_ptr(&pyroscope_context->phpspy_context),
                         &pyroscope_context->phpspy_context, err_ptr, err_len));

  int written = formulate_output(&pyroscope_context->phpspy_context,
                                 &pyroscope_context->app_root_dir[0], ptr, len,
                                 err_ptr, err_len);

  return written;
}

int phpspy_cleanup(pid_t pid, void *err_ptr, int err_len) {
  pyroscope_context_t *pyroscope_context = find_matching_context(pid);

  if (NULL == pyroscope_context) {
    int err_msg_len = snprintf((char *)err_ptr, err_len,
                               "Phpspy not initialized for %d pid", pid);
    return -err_msg_len;
  }

  deinitialize(pyroscope_context);
  deallocate_context(pyroscope_context);

  return 0;
}

int initialize(pid_t pid, struct pyroscope_context_t *pyroscope_context, void *event_udata,
               int (*event_handler)(struct trace_context_s *context,
                                    int event_type)) {
    struct trace_context_s *context = &pyroscope_context->phpspy_context;
//    static const char path_fmt[] = "/proc/%d/mem";
//    char path[PATH_MAX];
//    snprintf(&path[0], PATH_MAX, &path_fmt[0], pid);

    context->event_udata = event_udata;
    context->target.pid = pid;
    context->event_handler = event_handler;


//#ifdef USE_DIRECT
//    context->target.mem_fd = open(path, O_RDONLY);
//
//  if (context->target.mem_fd < 0) {
//    log_error("initialize: Failed to initialize for pid %d; err=%s\n", pid,
//              strerror(errno));
//    if (errno == ESRCH) {
//      return PHPSPY_ERR | PHPSPY_ERR_PID_DEAD;
//    }
//    return PHPSPY_ERR;
//  }
//#endif

    int rv = find_addresses(&context->target);

    return rv;
}

void deinitialize(struct pyroscope_context_t *pyroscope_context) {
    //todo direct
//#ifdef USE_DIRECT
//    close(context->target.mem_fd);
//#endif
}


int find_addresses(trace_target_t *target) {
    int rv;
    addr_memo_t memo;

    memset(&memo, 0, sizeof(addr_memo_t));

    try(rv, get_symbol_addr(&memo, target->pid, "executor_globals", &target->executor_globals_addr));

    try(rv, get_symbol_addr(&memo, target->pid, "sapi_globals", &target->sapi_globals_addr));

    log_error_enabled = 0;
    if (get_symbol_addr(&memo, target->pid, "basic_functions_module", &target->basic_functions_module_addr) != 0) {
        target->basic_functions_module_addr = 0;
    }
    log_error_enabled = 1;
    return PHPSPY_OK;
}

static int copy_proc_mem(pid_t pid, const char *what, void *raddr, void *laddr, size_t size) {
    struct iovec local[1];
    struct iovec remote[1];

    if (raddr == NULL) {
        log_error("copy_proc_mem: Not copying %s; raddr is NULL\n", what);
        return PHPSPY_ERR;
    }

    local[0].iov_base = laddr;
    local[0].iov_len = size;
    remote[0].iov_base = raddr;
    remote[0].iov_len = size;

    if (process_vm_readv(pid, local, 1, remote, 1, 0) == -1) {
        if (errno == ESRCH) { /* No such process */
            perror("process_vm_readv");
            return PHPSPY_ERR | PHPSPY_ERR_PID_DEAD;
        }
        log_error("copy_proc_mem: Failed to copy %s; err=%s raddr=%p size=%lu\n", what, strerror(errno), raddr, size);
        return PHPSPY_ERR;
    }

    return PHPSPY_OK;
}

void log_error(const char *fmt, ...) {
    va_list args;
    if (log_error_enabled) {
        va_start(args, fmt);
        vfprintf(stderr, fmt, args);
    }
}

uint64_t phpspy_zend_inline_hash_func(const char *str, size_t len) {
    /* Adapted from zend_string.h */
    uint64_t hash;
    hash = 5381UL;

    for (; len >= 8; len -= 8) {
        hash = ((hash << 5) + hash) + *str++;
        hash = ((hash << 5) + hash) + *str++;
        hash = ((hash << 5) + hash) + *str++;
        hash = ((hash << 5) + hash) + *str++;
        hash = ((hash << 5) + hash) + *str++;
        hash = ((hash << 5) + hash) + *str++;
        hash = ((hash << 5) + hash) + *str++;
        hash = ((hash << 5) + hash) + *str++;
    }
    switch (len) {
        case 7: hash = ((hash << 5) + hash) + *str++; /* fallthrough... */
        case 6: hash = ((hash << 5) + hash) + *str++; /* fallthrough... */
        case 5: hash = ((hash << 5) + hash) + *str++; /* fallthrough... */
        case 4: hash = ((hash << 5) + hash) + *str++; /* fallthrough... */
        case 3: hash = ((hash << 5) + hash) + *str++; /* fallthrough... */
        case 2: hash = ((hash << 5) + hash) + *str++; /* fallthrough... */
        case 1: hash = ((hash << 5) + hash) + *str++; break;
        case 0: break;
    }

    return hash | 0x8000000000000000UL;
}



#define phpv 70
#include "phpspy_trace_tpl.c"
#undef phpv
#define phpv 71
#include "phpspy_trace_tpl.c"
#undef phpv
#define phpv 72
#include "phpspy_trace_tpl.c"
#undef phpv
#define phpv 73
#include "phpspy_trace_tpl.c"
#undef phpv
#define phpv 74
#include "phpspy_trace_tpl.c"
#undef phpv
#define phpv 80
#include "phpspy_trace_tpl.c"
#undef phpv
#define phpv 81
#include "phpspy_trace_tpl.c"
#undef phpv
#define phpv 82
#include "phpspy_trace_tpl.c"
#undef phpv
#define phpv 83
#include "phpspy_trace_tpl.c"
#undef phpv


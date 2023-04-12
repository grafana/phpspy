#define _GNU_SOURCE 1

#include "pyroscope_api.h"
#include "pyroscope_api_struct.h"
#include "phpspy.h"
#include <unistd.h>

int get_php_version(trace_target_t *target);
int do_trace_70(trace_context_t *context);
int do_trace_71(trace_context_t *context);
int do_trace_72(trace_context_t *context);
int do_trace_73(trace_context_t *context);
int do_trace_74(trace_context_t *context);
int do_trace_80(trace_context_t *context);
int do_trace_81(trace_context_t *context);
int do_trace_82(trace_context_t *context);
int do_trace_83(trace_context_t *context);

extern char *opt_phpv;

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

int formulate_error_msg(int rv, struct trace_context_s *context, char *err_ptr, int err_len) {
    int err_msg_len = 0;
    if (rv == PHPSPY_OK) {
        return 0;
    }
    if ((rv & PHPSPY_ERR) == 0) {
        err_msg_len =
                snprintf((char *) err_ptr, err_len, "Unknown error code: %u",
                         (unsigned int) rv & ~((unsigned int) PHPSPY_ERR));
    } else if (rv & PHPSPY_ERR_PID_DEAD) {
        err_msg_len = snprintf((char *) err_ptr, err_len,
                               "App with PID %d is dead!", context->target.pid);
    } else if (rv & PHPSPY_ERR_BUF_FULL) {
        err_msg_len = snprintf((char *) err_ptr, err_len,
                               "Buffer is full with PID %d", context->target.pid);
    } else {
        err_msg_len = snprintf((char *) err_ptr, err_len, "General error!");
    }
    return err_msg_len < err_len ? -err_msg_len : -err_len;
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

void phpspy_init_spy(const char *args) {
    char *args_copy = strdup(args);
    char *saveptr1 = NULL, *saveptr2 = NULL;

    char *key_value_pair = strtok_r(args_copy, ",", &saveptr1);

    while (key_value_pair != NULL) {
        char *key = strtok_r(key_value_pair, "=", &saveptr2);
        char *value = strtok_r(NULL, "=", &saveptr2);

        if (strcmp(key, "direct_mem") == 0) {
            opt_direct_mem = strcmp(value, "true") == 0;
        } else if (strcmp(key, "libphp_awk_pattern") == 0) {
            opt_libname_awk_patt = strdup(value);
        }

        key_value_pair = strtok_r(NULL, ",", &saveptr1);
    }

    free(args_copy);
}

int phpspy_init_pid(pid_t pid, void *err_ptr, int err_len) {
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

    context->event_udata = event_udata;
    context->target.pid = pid;
    context->event_handler = event_handler;
    int rv = 0;

    if (strcmp(opt_phpv, "auto") == 0) {
        try(rv, get_php_version(&context->target));
    }

    if (strcmp("70", opt_phpv) == 0) {
        pyroscope_context->do_trace_ptr = do_trace_70;
    } else if (strcmp("71", opt_phpv) == 0) {
        pyroscope_context->do_trace_ptr = do_trace_71;
    } else if (strcmp("72", opt_phpv) == 0) {
        pyroscope_context->do_trace_ptr = do_trace_72;
    } else if (strcmp("73", opt_phpv) == 0) {
        pyroscope_context->do_trace_ptr = do_trace_73;
    } else if (strcmp("74", opt_phpv) == 0) {
        pyroscope_context->do_trace_ptr = do_trace_74;
    } else if (strcmp("80", opt_phpv) == 0) {
        pyroscope_context->do_trace_ptr = do_trace_80;
    } else if (strcmp("81", opt_phpv) == 0) {
        pyroscope_context->do_trace_ptr = do_trace_81;
    } else if (strcmp("82", opt_phpv) == 0) {
        pyroscope_context->do_trace_ptr = do_trace_82;
    } else if (strcmp("83", opt_phpv) == 0) {
        pyroscope_context->do_trace_ptr = do_trace_83; /* TODO verify 8.3 structs */
    } else {
        log_error("main_pid: Unrecognized PHP version (%s)\n", opt_phpv);
        return PHPSPY_ERR;
    }

    static const char path_fmt[] = "/proc/%d/mem";
    char path[PATH_MAX];
    snprintf(&path[0], PATH_MAX, &path_fmt[0], pid);

    context->target.mem_fd = open(path, O_RDONLY);

    if (context->target.mem_fd < 0) {
        log_error("initialize: Failed to initialize for pid %d; err=%s\n", pid,
                  strerror(errno));
        if (errno == ESRCH) {
            return PHPSPY_ERR | PHPSPY_ERR_PID_DEAD;
        }
        return PHPSPY_ERR;
    }

    rv = find_addresses(&context->target);

    return rv;
}

void deinitialize(struct pyroscope_context_t *pyroscope_context) {
    close(pyroscope_context->phpspy_context.target.mem_fd);
}


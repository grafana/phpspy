#include "pyroscope_api.h"

#include <unistd.h>

#include "phpspy.h"
#include "pyroscope_api_struct.h"

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
                          &loc->file[file_path_beginning], loc->class_name,
                          loc->class_len ? "::" : "", loc->func);
    } else {
      char out_fmt[] = "%s:%d - %s%s%s;";
      written += snprintf(
          write_cursor, data_len, out_fmt, &loc->file[file_path_beginning],
          loc->lineno, loc->class_name, loc->class_len ? "::" : "", loc->func);
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
             initialize(pid, &pyroscope_context->phpspy_context,
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
     formulate_error_msg(do_trace(&pyroscope_context->phpspy_context),
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

  deinitialize(&pyroscope_context->phpspy_context);
  deallocate_context(pyroscope_context);

  return 0;
}

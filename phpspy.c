#include "phpspy.h"

#ifdef USE_DIRECT
static int copy_proc_mem_direct(trace_target_t *target, const char *what,
                                void *raddr, void *laddr, size_t size) {
  if (lseek(target->mem_fd, (uint64_t)raddr, SEEK_SET) == -1) {
    log_error(
        "copy_proc_mem_direct: Failed to copy %s; err=%s raddr=%p size=%lu\n",
        what, strerror(errno), raddr, size);
    return PHPSPY_ERR;
  }
  if (read(target->mem_fd, laddr, size) == -1) {
    log_error(
        "copy_proc_mem_direct: Failed to copy %s; err=%s raddr=%p size=%lu\n",
        what, strerror(errno), raddr, size);
    return PHPSPY_ERR;
  }
  return PHPSPY_OK;
}
#else
static int copy_proc_mem_syscall(trace_target_t *target, const char *what,
                                 void *raddr, void *laddr, size_t size) {
  struct iovec local;
  struct iovec remote;
  local.iov_base = laddr;
  local.iov_len = size;
  remote.iov_base = raddr;
  remote.iov_len = size;

  if (process_vm_readv(target->pid, &local, 1, &remote, 1, 0) == -1) {
    if (errno == ESRCH) { /* No such process */
      perror("process_vm_readv");
      return PHPSPY_ERR | PHPSPY_ERR_PID_DEAD;
    }
    log_error(
        "copy_proc_mem_syscall: Failed to copy %s; err=%s raddr=%p size=%lu\n",
        what, strerror(errno), raddr, size);
    return PHPSPY_ERR;
  }

  return PHPSPY_OK;
}
#endif

int copy_proc_mem(trace_target_t *target, const char *what, void *raddr,
                  void *laddr, size_t size) {
  if (raddr == NULL) {
    log_error("copy_proc_mem: Not copying %s; raddr is NULL\n", what);
    return PHPSPY_ERR;
  }

  // TODO: Check if pid is alive, otherwise: PHPSPY_ERR_PID_DEAD

#ifdef USE_DIRECT
  return copy_proc_mem_direct(target, what, raddr, laddr, size);
#else
  return copy_proc_mem_syscall(target, what, raddr, laddr, size);
#endif
}

int find_addresses(trace_target_t *target) {
  int rv;
  addr_memo_t memo;

  memset(&memo, 0, sizeof(addr_memo_t));

  try
    (rv, get_symbol_addr(&memo, target->pid, "executor_globals",
                         &target->executor_globals_addr));

  // TODO: Is this doing someting?
  /*
      if (get_symbol_addr(&memo, target->pid, "basic_functions_module",
     &target->basic_functions_module_addr) != 0) {
          target->basic_functions_module_addr = 0;
      }
  */
  return PHPSPY_OK;
}

int initialize(pid_t pid, struct trace_context_s *context, void *event_udata,
               int (*event_handler)(struct trace_context_s *context,
                                    int event_type)) {
  static const char path_fmt[] = "/proc/%d/mem";
  char path[PATH_MAX];
  snprintf(&path[0], PATH_MAX, &path_fmt[0], pid);

  context->event_udata = event_udata;
  context->target.pid = pid;
  context->event_handler = event_handler;

#ifdef USE_DIRECT
  context->target.mem_fd = open(path, O_RDONLY);

  if (context->target.mem_fd < 0) {
    log_error("initialize: Failed to initialize for pid %d; err=%s\n", pid,
              strerror(errno));
    if (errno == ESRCH) {
      return PHPSPY_ERR | PHPSPY_ERR_PID_DEAD;
    }
    return PHPSPY_ERR;
  }
#endif

  int rv = find_addresses(&context->target);

  return rv;
}

void deinitialize(struct trace_context_s *context) {
#ifdef USE_DIRECT
  close(context->target.mem_fd);
#endif
}

void log_error(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
}

#include "pyroscope_api.h"

int phpspy_init(pid_t pid, void* err_ptr, int err_len) {
  return 0;
}

int phpspy_cleanup(pid_t pid, void* err_ptr, int err_len) {
  return 0;
}

int phpspy_snapshot(pid_t pid, void* ptr, int len, void* err_ptr, int err_len) {
  char placeholder[] = "placeholder";
  strcpy(ptr, placeholder);
  return 11;
}

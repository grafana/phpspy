#include "pyroscope_api.h"

/*
 * Initializes the profiler for a given process
 *
 * @param pid Process ID of the profiled process
 * @param err_ptr Pointer to the error buffer
 * @param err_len Length of the error buffer
 * @return Greater or equal 0 indicates success. Less than zero means error happened and indicates the length of the error message
 */
int phpspy_init(pid_t pid, void* err_ptr, int err_len) {
  return 0;
}

/*
 * Cleans up resources associated with a profiling session for a given process
 *
 * @param pid Process ID of the profiled process
 * @param err_ptr Pointer to the error buffer
 * @param err_len Length of the error buffer
 * @return Greater or equal 0 indicates success. Less than zero means error happened and indicates the length of the error message
 */
int phpspy_cleanup(pid_t pid, void* err_ptr, int err_len) {
  return 0;
}

/*
 * Takes a snapshot of the process's stacktrace and puts it into buffer at `ptr`
 *
 * @param pid Process ID of the profiled process
 * @param ptr Buffer for the stacktrace
 * @param len Length of the stacktrace buffer
 * @param err_ptr Pointer to the error buffer
 * @param err_len Length of the error buffer
 * @return indicates the length of the stacktrace. Less than zero means error happened and indicates the length of the error message
 */
int phpspy_snapshot(pid_t pid, void* ptr, int len, void* err_ptr, int err_len) {
  char placeholder[] = "placeholder";
  strcpy(ptr, placeholder);
  return 11;
}

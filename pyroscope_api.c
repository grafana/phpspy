#include "pyroscope_api.h"

/* TODO This is done in the same way in phpspy, so until they fix it, it has to stay like it is */
#include "phpspy.c"

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
    int rv = 0;
    trace_context_t context;
    const trace_loc_t* loc = &context.event.frame.loc;
    const size_t str_items = 3;
    const size_t str_items_len = str_items * PHPSPY_STR_SIZE;
    const fileno_str_len = 6 * PHPSPY_STR_SIZE;

    memset(&context, 0, sizeof(trace_context_t));
    context.target.pid = pid;
    context.event_handler = opt_event_handler;

    try(rv, find_addresses(&context.target));
    try(rv, context.event_handler(&context, PHPSPY_TRACE_EVENT_INIT));

    /* TODO Determine PHP version or make sure you can use ZEND */
    rv |= do_trace(&context);

    if((rv & PHPSPY_OK) != 0)
    {
        int err_msg_len = 0;
        switch(rv)
        {
            case PHPSPY_ERR_PID_DEAD:
            {
                err_msg_len = snprintf((char*)err_ptr, err_len, "Application with PID %d is dead!", pid);
                break;
            }
            /* TODO: maybe expand? */
            default:
            {
                err_msg_len = snprintf((char*)err_ptr, err_len, "Unknown error");
                break;
            }
        }
        return err_msg_len < err_len ? -err_msg_len : -err_len;
    }

    if((str_items_len + fileno_str_len) < len)
    {
        int err_msg_len = snprintf((char*)err_ptr, err_len, "Not enough space for output string");
        return -err_msg_len;
    }

    char out_fmt[] = "%s:%d - %s::%s";
    /* '+ 1' is to get rid of '<' chracter in the name of a function */
    int written = snprintf((char*)ptr, len, out_fmt, loc->file, loc->lineno, loc->class, loc->func + 1);

    /* This is done coz 'written' may be bigger than true size of the string
     * '- 1' is to get rid of the '>' character in the name of a function */
    int data_len = written < len ? (written - 1) : len;
    ((char*)ptr)[data_len] = '\0';

    context.event_handler(&context, PHPSPY_TRACE_EVENT_DEINIT);

    return data_len;
}

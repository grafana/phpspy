#include "pyroscope_api.h"
#include <linux/limits.h>
#include <unistd.h>

/* TODO This is done in the same way in phpspy, so until they fix it, it has to stay like it is */
#include "phpspy.c"

#define MAX_STACK_DEPTH 50

int event_handler(struct trace_context_s *context, int event_type);

typedef struct stack_trace_t {
    trace_frame_t frames[MAX_STACK_DEPTH];
    char app_root_dir[PATH_MAX];
} stack_trace_t;

#define handle_error(__rv, __err_ptr, __err_len)                         \
do{                                                                      \
    if((__rv & PHPSPY_OK) != 0)                                          \
    {                                                                    \
        int err_msg_len = 0;                                             \
        switch(__rv)                                                     \
        {                                                                \
            case PHPSPY_ERR_PID_DEAD:                                    \
            {                                                            \
                err_msg_len = snprintf((char*)__err_ptr, __err_len,      \
                        "App with PID %d is dead!", context.target.pid); \
                break;                                                   \
            }                                                            \
            default:                                                     \
            {                                                            \
                err_msg_len = snprintf((char*)__err_ptr, __err_len,      \
                        "Unknown error: %d", __rv);                      \
                break;                                                   \
            }                                                            \
        }                                                                \
        return err_msg_len < __err_len ? -err_msg_len : -__err_len;      \
    }                                                                    \
} while(0)

void get_process_cwd(char* app_cwd, pid_t pid)
{
    char buf[PATH_MAX];
    snprintf(buf, PATH_MAX, "/proc/%d/cwd", pid);
    int app_cwd_len = readlink(buf, app_cwd, PATH_MAX);

    if(app_cwd_len < 0)
    {
        app_cwd[0] = '\0';
    }
}

struct trace_context_s context;
stack_trace_t stack_trace;
int phpspy_init(pid_t pid, void* err_ptr, int err_len) {
    opt_max_stack_depth = MAX_STACK_DEPTH;
    memset(&stack_trace, 0, sizeof(stack_trace_t));
    memset(&context, 0, sizeof(struct trace_context_s));
    context.event_udata = (void*)&stack_trace;
    context.target.pid = pid;
    context.event_handler = event_handler;
    get_process_cwd(&stack_trace.app_root_dir[0], pid);
    handle_error(find_addresses(&context.target), err_ptr, err_len);
    return 0;
}

int phpspy_cleanup(pid_t pid, void* err_ptr, int err_len) {
    return 0;
}

int event_handler(struct trace_context_s *context, int event_type) {
    switch (event_type) {
        case PHPSPY_TRACE_EVENT_FRAME:
            {
                stack_trace_t* stack_trace = (stack_trace_t*)context->event_udata;
                memcpy(&stack_trace->frames[context->event.frame.depth], &context->event.frame, sizeof(trace_frame_t));
                break;
            }
        case PHPSPY_TRACE_EVENT_ERROR:
            {
                return PHPSPY_TRACE_EVENT_ERROR;
            }
    }
    return PHPSPY_OK;
}

int parse_output(struct trace_context_s* context, char* app_root_dir, char* data_ptr, int data_len, void* err_ptr, int err_len)
{
    int written = 0;
    const int nof_frames = context->event.frame.depth;
    for(int current_frame_idx = (nof_frames - 1); current_frame_idx >= 0; current_frame_idx--)
    {
        stack_trace_t* stack_trace = (stack_trace_t*)context->event_udata;
        trace_loc_t* loc = &stack_trace->frames[current_frame_idx].loc;

        char* write_buffer = ((char*)data_ptr) + written;

        int file_path_beginning = 0;
        if(app_root_dir[0] != '\0')
        {
            const int root_path_len = strlen(app_root_dir);
            if(memcmp(loc->file, app_root_dir, root_path_len) == 0)
            {
                file_path_beginning = root_path_len + 1;
            }
        }

        if(loc->lineno == -1)
        {
            char out_fmt[] = "%s - %s%s%s; ";
            written += snprintf(write_buffer, data_len, out_fmt, &loc->file[file_path_beginning], loc->class_len ? loc->class : "", loc->class_len? "::" : "", loc->func);
        }
        else
        {
            char out_fmt[] = "%s:%d - %s%s%s; ";
            written += snprintf(write_buffer, data_len, out_fmt, &loc->file[file_path_beginning], loc->lineno, loc->class_len ? loc->class : "", loc->class_len? "::" : "", loc->func);
        }

        if(written > data_len)
        {
            int err_msg_len = snprintf((char*)err_ptr, err_len, "Not enough space! %d > %d", written, data_len);
            return -err_msg_len;
        }
    }
    return written;
}

int phpspy_snapshot(pid_t pid, void* ptr, int len, void* err_ptr, int err_len) {
    handle_error(do_trace(&context), err_ptr, err_len);
    int written = parse_output(&context, &stack_trace.app_root_dir[0],  ptr, len, err_ptr, err_len);
    return written;
}

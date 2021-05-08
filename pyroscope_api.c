#include "pyroscope_api.h"

/* TODO This is done in the same way in phpspy, so until they fix it, it has to stay like it is */
#include "phpspy.c"

#define MAX_STACK_DEPTH 50

/* TODO: Will this be shared between all currently debugged php app? I highly doubt!
 * Those would be a separate conteiners, right?
 * But what about phpspy itself and use cases other than pyroscope?*/
typedef struct stack_trace_t {
    trace_frame_t frames[MAX_STACK_DEPTH];
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

int phpspy_init(pid_t pid, void* err_ptr, int err_len) {
    opt_max_stack_depth = MAX_STACK_DEPTH;
  return 0;
}

int phpspy_cleanup(pid_t pid, void* err_ptr, int err_len) {
  return 0;
}

int event_handler(struct trace_context_s *context, int event_type) {
    /* TODO: Do some profiling. How fast is fast enough? */
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

void get_process_cwd(char** app_cwd, pid_t pid)
{
    int app_cwd_len = 0;
    char buf[PATH_MAX];
    snprintf(buf, PATH_MAX, "/proc/%d/cwd", pid);
    ssize_t alloclen = 512;
    (*app_cwd) = malloc(alloclen);
    while ((app_cwd_len = readlink(buf, *app_cwd, alloclen)) == alloclen) {
        alloclen *= 2;
        app_cwd = realloc(*app_cwd, alloclen);
    }

    if(app_cwd_len < 0)
    {
        (*app_cwd)[0] = '\0';
    }
}

int parse_output(struct trace_context_s* context, char** app_root_dir, char* data_ptr, int data_len, void* err_ptr, int err_len)
{
    /* TODO: Handle lineno == '-1'? or pyroscope shall do it? */
    int written = 0;
    const int nof_frames = context->event.frame.depth;
    for(int current_frame_idx = (nof_frames - 1); current_frame_idx >= 0; current_frame_idx--)
    {
        stack_trace_t* stack_trace = (stack_trace_t*)context->event_udata;
        trace_loc_t* loc = &stack_trace->frames[current_frame_idx].loc;

        char* write_buffer = ((char*)data_ptr) + written;

        int file_path_beginning = 0;
        if(*app_root_dir)
        {
            const int root_path_len = strlen(*app_root_dir);
            if(memcmp(loc->file, *app_root_dir, root_path_len) == 0)
            {
                file_path_beginning = root_path_len + 1;
            }
        }

        /* TODO: Optimize, or remove and let pyroscope handle class name */
        if(loc->class_len > 0)
        {
            char out_fmt[] = "%s:%d - %s::%s; ";
            written += snprintf(write_buffer, data_len, out_fmt, &loc->file[file_path_beginning], loc->lineno, loc->class, loc->func);
        }
        else
        {
            char out_fmt[] = "%s:%d - %s; ";
            written += snprintf(write_buffer, data_len, out_fmt, &loc->file[file_path_beginning], loc->lineno, loc->func);
        }

        if(written > data_len)
        {
            if(*app_root_dir)
            {
                free(*app_root_dir);
                (*app_root_dir) = NULL;
            }
            int err_msg_len = snprintf((char*)err_ptr, err_len, "Not enough space! %d > %d", written, data_len);
            return -err_msg_len;
        }
    }
    return written;
}

int phpspy_snapshot(pid_t pid, void* ptr, int len, void* err_ptr, int err_len) {
    static struct trace_context_s context;
    static stack_trace_t stack_trace;

    context.event_udata = (void*)&stack_trace;
    context.target.pid = pid;
    context.event_handler = event_handler;

    handle_error(find_addresses(&context.target), err_ptr, err_len);
    handle_error(do_trace(&context), err_ptr, err_len);

    char* app_root_dir = NULL;
    /* TODO: get_process_cwd(&app_root_dir, pid); */
    int written = parse_output(&context, &app_root_dir,  ptr, len, err_ptr, err_len);
    if(app_root_dir)
    {
        free(app_root_dir);
        app_root_dir = NULL;
    }

    return written;
}

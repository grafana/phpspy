#include "pyroscope_api.h"

/* TODO This is done in the same way in phpspy, so until they fix it, it has to stay like it is */
#include "phpspy.c"

#define MAX_FRAMES 50

typedef struct stack_trace_t {
    trace_frame_t frames[MAX_FRAMES];
} stack_trace_t;

int phpspy_init(pid_t pid, void* err_ptr, int err_len) {
    opt_max_stack_depth = MAX_FRAMES;
  return 0;
}

int phpspy_cleanup(pid_t pid, void* err_ptr, int err_len) {
  return 0;
}

int event_handler(struct trace_context_s *context, int event_type) {
    /* TODO: Ensure no memory leaks. Maybe mempool? */
    /* TODO: Do some profiling. How fast is fast enough? */

    switch (event_type) {
        case PHPSPY_TRACE_EVENT_INIT:
            {
                context->event_udata = calloc(1, sizeof(stack_trace_t));
                break;
            }
        case PHPSPY_TRACE_EVENT_STACK_BEGIN:
            {
                break;
            }
        case PHPSPY_TRACE_EVENT_FRAME:
            {
                stack_trace_t* stack_trace = (stack_trace_t*)context->event_udata;
                memcpy(&stack_trace->frames[context->event.frame.depth], &context->event.frame, sizeof(trace_frame_t));
                break;
            }
        case PHPSPY_TRACE_EVENT_STACK_END:
            {
                break;
            }
        case PHPSPY_TRACE_EVENT_ERROR:
        case PHPSPY_TRACE_EVENT_DEINIT:
            {
                if(context->event_udata)
                {
                    free(context->event_udata);
                    context->event_udata = NULL;
                }
                break;
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

int do_trace_safe(struct trace_context_s* context, char* err_ptr, int err_len)
{
    int rv = 0;
    /* TODO Determine PHP version or make sure you can use ZEND */
    rv |= do_trace(context);

    if((rv & PHPSPY_OK) != 0)
    {
        try(rv, context->event_handler(context, PHPSPY_TRACE_EVENT_ERROR));
        int err_msg_len = 0;
        switch(rv)
        {
            case PHPSPY_ERR_PID_DEAD:
            {
                err_msg_len = snprintf((char*)err_ptr, err_len, "Application with PID %d is dead!", context->target.pid);
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
    return 0;
}

int parse_output(struct trace_context_s* context, char** app_cwd, char* data_ptr, int data_len, void* err_ptr, int err_len)
{
    int rv = 0;
    /* TODO: Handle lineno == '-1'? or pyroscope shall do it? */
    int written = 0;
    const int nof_frames = context->event.frame.depth;
    for(int current_frame_idx = (nof_frames - 1); current_frame_idx >= 0; current_frame_idx--)
    {
        stack_trace_t* stack_trace = (stack_trace_t*)context->event_udata;
        trace_loc_t* loc = &stack_trace->frames[current_frame_idx].loc;

        char* write_buffer = ((char*)data_ptr) + written;

        int file_path_beginning = 0;
        if(*app_cwd)
        {
            const int root_path_len = strlen(*app_cwd);
            if(memcmp(loc->file, *app_cwd, root_path_len) == 0)
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
            try(rv, context->event_handler(context, PHPSPY_TRACE_EVENT_ERROR));
            free(*app_cwd);
            (*app_cwd) = NULL;
            int err_msg_len = snprintf((char*)err_ptr, err_len, "Not enough space! %d > %d", written, data_len);
            return -err_msg_len;
        }
    }

    return written;
}

int phpspy_snapshot(pid_t pid, void* ptr, int len, void* err_ptr, int err_len) {
    int rv = 0;
    struct trace_context_s context;

    memset(&context, 0, sizeof(trace_context_t));
    context.target.pid = pid;
    context.event_handler = event_handler;

    try(rv, find_addresses(&context.target));

    try(rv, context.event_handler(&context, PHPSPY_TRACE_EVENT_INIT));

    try(rv, do_trace_safe(&context, err_ptr, err_len));

    char* app_cwd = NULL;
    get_process_cwd(&app_cwd, pid);

    int written = parse_output(&context, &app_cwd,  ptr, len, err_ptr, err_len);
    if(app_cwd)
    {
        free(app_cwd);
        app_cwd = NULL;
    }

    try(rv, context.event_handler(&context, PHPSPY_TRACE_EVENT_DEINIT));

    return written;
}

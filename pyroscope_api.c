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
                free(context->event_udata);
                break;
            }
    }
    return PHPSPY_OK;
}

int phpspy_snapshot(pid_t pid, void* ptr, int len, void* err_ptr, int err_len) {
    int rv = 0;
    trace_context_t context;

    memset(&context, 0, sizeof(trace_context_t));
    context.target.pid = pid;
    context.event_handler = event_handler;

    try(rv, find_addresses(&context.target));
    try(rv, context.event_handler(&context, PHPSPY_TRACE_EVENT_INIT));

    /* TODO Determine PHP version or make sure you can use ZEND */
    rv |= do_trace(&context);

    if((rv & PHPSPY_OK) != 0)
    {
        try(rv, context.event_handler(&context, PHPSPY_TRACE_EVENT_ERROR));
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

    /* TODO: Handle lineno == '-1'? or pyroscope shall do it? */
    int written = 0;
    const int nof_frames = context.event.frame.depth;
    for(int current_frame_idx = (nof_frames - 1); current_frame_idx >= 0; current_frame_idx--)
    {
        stack_trace_t* stack_trace = (stack_trace_t*)context.event_udata;
        trace_loc_t* loc = &stack_trace->frames[current_frame_idx].loc;

        char* write_buffer = ((char*)ptr) + written;
        /* TODO: Optimize, or remove and let pyroscope handle class name */
        if(loc->class_len > 0)
        {
            char out_fmt[] = "%s:%d - %s::%s; ";
            written += snprintf(write_buffer, len, out_fmt, loc->file, loc->lineno, loc->class, loc->func);
        }
        else
        {
            char out_fmt[] = "%s:%d - %s; ";
            written += snprintf(write_buffer, len, out_fmt, loc->file, loc->lineno, loc->func);
        }

        /* This is done coz 'written' may be bigger than true size of the string */
        if(written > len)
        {
            try(rv, context.event_handler(&context, PHPSPY_TRACE_EVENT_ERROR));
            int err_msg_len = snprintf((char*)err_ptr, err_len, "Not enough space! %d > %d", written, len);
            return -err_msg_len;
        }
    }

    context.event_handler(&context, PHPSPY_TRACE_EVENT_DEINIT);
    return written;
}

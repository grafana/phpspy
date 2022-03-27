#include "phpspy.h"

#define try_copy_proc_mem(__what, __raddr, __laddr, __size) \
  try                                                       \
  (rv,                                                      \
   copy_proc_mem(&context->target, (__what), (__raddr), (__laddr), (__size)))

static int trace_stack(trace_context_t *context,
                       zend_execute_data *remote_execute_data, int *depth);

static int copy_executor_globals(trace_context_t *context,
                                 zend_executor_globals *executor_globals);

static int sprint_zstring(trace_context_t *context, const char *what,
                          zend_string *lzstring, char *buf, size_t buf_size,
                          size_t *buf_len);
static int sprint_zval(trace_context_t *context, zval *lzval, char *buf,
                       size_t buf_size, size_t *buf_len);
static int sprint_zarray(trace_context_t *context, zend_array *rzarray,
                         char *buf, size_t buf_size, size_t *buf_len);
static int sprint_zarray_bucket(trace_context_t *context, Bucket *lbucket,
                                char *buf, size_t buf_size, size_t *buf_len);

int do_trace(trace_context_t *context) {
  int rv, depth;
  zend_executor_globals executor_globals;

  try
    (rv, copy_executor_globals(context, &executor_globals));
  try
    (rv, context->event_handler(context, PHPSPY_TRACE_EVENT_STACK_BEGIN));

  rv = PHPSPY_OK;
  do {
#define maybe_break_on_err()                      \
  do {                                            \
    if ((rv & PHPSPY_ERR_PID_DEAD) != 0) {        \
      break;                                      \
    } else if ((rv & PHPSPY_ERR_BUF_FULL) != 0) { \
      break;                                      \
    } else {                                      \
      break;                                      \
    }                                             \
  } while (0)

    rv |= trace_stack(context, executor_globals.current_execute_data, &depth);
    maybe_break_on_err();
    if (depth < 1) break;

#undef maybe_break_on_err
  } while (0);

  if (rv == PHPSPY_OK) {
    try
      (rv, context->event_handler(context, PHPSPY_TRACE_EVENT_STACK_END));
  }

  return PHPSPY_OK;
}

static int trace_stack(trace_context_t *context,
                       zend_execute_data *remote_execute_data, int *depth) {
  int rv;
  zend_execute_data execute_data;
  zend_function zfunc;
  zend_string zstring;
  zend_class_entry zce;
  zend_op zop;
  trace_frame_t *frame;

  frame = &context->event.frame;
  *depth = 0;

  while (remote_execute_data && *depth != MAX_STACK_DEPTH) {
    memset(&execute_data, 0, sizeof(execute_data));
    memset(&zfunc, 0, sizeof(zfunc));
    memset(&zstring, 0, sizeof(zstring));
    memset(&zce, 0, sizeof(zce));
    memset(&zop, 0, sizeof(zop));

    /* TODO reduce number of copy calls */
    try_copy_proc_mem("execute_data", remote_execute_data, &execute_data,
                      sizeof(execute_data));
    try_copy_proc_mem("zfunc", execute_data.func, &zfunc, sizeof(zfunc));
    if (zfunc.common.function_name) {
      try
        (rv, sprint_zstring(context, "function_name",
                            zfunc.common.function_name, frame->loc.func,
                            sizeof(frame->loc.func), &frame->loc.func_len));
    } else {
      frame->loc.func_len =
          snprintf(frame->loc.func, sizeof(frame->loc.func), "<main>");
    }
    if (zfunc.common.scope) {
      try_copy_proc_mem("zce", zfunc.common.scope, &zce, sizeof(zce));
      try
        (rv,
         sprint_zstring(context, "class_name", zce.name, frame->loc.class_name,
                        sizeof(frame->loc.class_name), &frame->loc.class_len));
    } else {
      frame->loc.class_name[0] = '\0';
      frame->loc.class_len = 0;
    }
    if (zfunc.type == 2 && zfunc.op_array.filename != NULL) {
      try
        (rv, sprint_zstring(context, "filename", zfunc.op_array.filename,
                            frame->loc.file, sizeof(frame->loc.file),
                            &frame->loc.file_len));
      frame->loc.lineno = zfunc.op_array.line_start;
      /* TODO add comments */
    } else {
      frame->loc.file_len =
          snprintf(frame->loc.file, sizeof(frame->loc.file), "<internal>");
      frame->loc.lineno = -1;
    }
    frame->depth = *depth;
    try
      (rv, context->event_handler(context, PHPSPY_TRACE_EVENT_FRAME));
    remote_execute_data = execute_data.prev_execute_data;
    *depth += 1;
  }

  return PHPSPY_OK;
}

static int copy_executor_globals(trace_context_t *context,
                                 zend_executor_globals *executor_globals) {
  int rv;
  executor_globals->current_execute_data = NULL;
  try_copy_proc_mem("executor_globals",
                    (void *)context->target.executor_globals_addr,
                    executor_globals, sizeof(*executor_globals));
  return PHPSPY_OK;
}

static int sprint_zstring(trace_context_t *context, const char *what,
                          zend_string *rzstring, char *buf, size_t buf_size,
                          size_t *buf_len) {
  int rv;
  zend_string lzstring;

  *buf = '\0';
  *buf_len = 0;
  try_copy_proc_mem(what, rzstring, &lzstring, sizeof(lzstring));
  *buf_len = PHPSPY_MIN(lzstring.len, PHPSPY_MAX(1, buf_size) - 1);
  try_copy_proc_mem(what, ((char *)rzstring) + offsetof(zend_string, val), buf,
                    *buf_len);
  *(buf + (int)*buf_len) = '\0';

  return PHPSPY_OK;
}

static int sprint_zval(trace_context_t *context, zval *lzval, char *buf,
                       size_t buf_size, size_t *buf_len) {
  int rv;
  int type;
  type = (int)lzval->u1.v.type;
  switch (type) {
    case IS_LONG:
      snprintf(buf, buf_size, "%ld", lzval->value.lval);
      *buf_len = strlen(buf);
      break;
    case IS_DOUBLE:
      snprintf(buf, buf_size, "%f", lzval->value.dval);
      *buf_len = strlen(buf);
      break;
    case IS_STRING:
      try
        (rv, sprint_zstring(context, "zval", lzval->value.str, buf, buf_size,
                            buf_len));
      break;
    case IS_ARRAY:
      try
        (rv, sprint_zarray(context, lzval->value.arr, buf, buf_size, buf_len));
      break;
    default:
      /* TODO handle other zval types */
      /* fprintf(context->fout, "value not supported, found type: %d\n", type);
       */
      return PHPSPY_ERR;
  }
  return PHPSPY_OK;
}

static int sprint_zarray(trace_context_t *context, zend_array *rzarray,
                         char *buf, size_t buf_size, size_t *buf_len) {
  int rv;
  int i;
  int array_len;
  size_t tmp_len;
  Bucket buckets[PHPSPY_MAX_ARRAY_BUCKETS];
  zend_array lzarray;
  char *obuf;

  obuf = buf;
  try_copy_proc_mem("array", rzarray, &lzarray, sizeof(lzarray));

  array_len = PHPSPY_MIN(lzarray.nNumOfElements, PHPSPY_MAX_ARRAY_BUCKETS);
  try_copy_proc_mem("buckets", lzarray.arData, buckets,
                    sizeof(Bucket) * array_len);

  for (i = 0; i < array_len; i++) {
    try
      (rv, sprint_zarray_bucket(context, buckets + i, buf, buf_size, &tmp_len));
    buf_size -= tmp_len;
    buf += tmp_len;

    /* TODO Introduce a string class to clean this silliness up */
    if (buf_size >= 2) {
      *buf = ',';
      --buf_size;
      ++buf;
    }
  }

  *buf_len = (size_t)(buf - obuf);

  return PHPSPY_OK;
}

static int sprint_zarray_bucket(trace_context_t *context, Bucket *lbucket,
                                char *buf, size_t buf_size, size_t *buf_len) {
  int rv;
  char tmp_key[PHPSPY_STR_SIZE];
  size_t tmp_len;
  char *obuf;

  obuf = buf;

  if (lbucket->key != NULL) {
    try
      (rv, sprint_zstring(context, "array_key", lbucket->key, tmp_key,
                          sizeof(tmp_key), &tmp_len));

    /* TODO Introduce a string class to clean this silliness up */
    if (buf_size > tmp_len + 1 + 1) {
      snprintf(buf, buf_size, "%s=", tmp_key);
      buf_size -= tmp_len + 1;
      buf += tmp_len + 1;
    }
  }

  try
    (rv, sprint_zval(context, &lbucket->val, buf, buf_size, &tmp_len));
  buf += tmp_len;

  *buf_len = (size_t)(buf - obuf);
  return PHPSPY_OK;
}

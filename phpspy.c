#include "phpspy.h"

#define TB_IMPL
#define TB_OPT_V1_COMPAT
#include "termbox.h"
#include "pyroscope_api_struct.h"

#undef TB_IMPL

uint64_t opt_executor_globals_addr = 0;
uint64_t opt_sapi_globals_addr = 0;
int opt_capture_req = 0;
int opt_capture_req_qstring = 0;
int opt_capture_req_cookie = 0;
int opt_capture_req_uri = 0;
int opt_capture_req_path = 0;
int opt_capture_mem = 0;
int opt_max_stack_depth = PYROSCOPE_MAX_STACK_DEPTH;
char *opt_phpv = "auto";
int opt_continue_on_error = 0;
char *opt_libname_awk_patt = "libphp[78]?";
int opt_direct_mem = 0;
varpeek_entry_t *varpeek_map = NULL;
glopeek_entry_t *glopeek_map = NULL;
int log_error_enabled = 1;

int find_addresses(trace_target_t *target);
int copy_proc_mem(trace_target_t *target, const char *what, void *raddr, void *laddr, size_t size);

#ifdef USE_ZEND
#error "USE_ZEND is not supported"
#endif

int get_php_version(trace_target_t *target);
int do_trace_70(trace_context_t *context);
int do_trace_71(trace_context_t *context);
int do_trace_72(trace_context_t *context);
int do_trace_73(trace_context_t *context);
int do_trace_74(trace_context_t *context);
int do_trace_80(trace_context_t *context);
int do_trace_81(trace_context_t *context);
int do_trace_82(trace_context_t *context);
int do_trace_83(trace_context_t *context);

int find_addresses(trace_target_t *target) {
    int rv;
    addr_memo_t memo;

    memset(&memo, 0, sizeof(addr_memo_t));

    if (opt_executor_globals_addr != 0) {
        target->executor_globals_addr = opt_executor_globals_addr;
    } else {
        try(rv, get_symbol_addr(&memo, target->pid, "executor_globals", &target->executor_globals_addr));
    }
    if (opt_sapi_globals_addr != 0) {
        target->sapi_globals_addr = opt_sapi_globals_addr;
    } else if (opt_capture_req) {
        try(rv, get_symbol_addr(&memo, target->pid, "sapi_globals", &target->sapi_globals_addr));
    }
    if (opt_capture_mem) {
        try(rv, get_symbol_addr(&memo, target->pid, "alloc_globals", &target->alloc_globals_addr));
    }
    log_error_enabled = 0;
    if (get_symbol_addr(&memo, target->pid, "basic_functions_module", &target->basic_functions_module_addr) != 0) {
        target->basic_functions_module_addr = 0;
    }
    log_error_enabled = 1;
    return PHPSPY_OK;
}

static int copy_proc_mem_vm_read(trace_target_t *target, const char *what, void *raddr, void *laddr, size_t size) {
    pid_t pid = target->pid;
    struct iovec local[1];
    struct iovec remote[1];

    if (raddr == NULL) {
        log_error("copy_proc_mem: Not copying %s; raddr is NULL\n", what);
        return PHPSPY_ERR;
    }

    local[0].iov_base = laddr;
    local[0].iov_len = size;
    remote[0].iov_base = raddr;
    remote[0].iov_len = size;

    if (process_vm_readv(pid, local, 1, remote, 1, 0) == -1) {
        if (errno == ESRCH) { /* No such process */
            perror("process_vm_readv");
            return PHPSPY_ERR | PHPSPY_ERR_PID_DEAD;
        }
        log_error("copy_proc_mem: Failed to copy %s; err=%s raddr=%p size=%lu\n", what, strerror(errno), raddr, size);
        return PHPSPY_ERR;
    }

    return PHPSPY_OK;
}

static int copy_proc_mem_direct(trace_target_t *target, const char *what, void *raddr, void *laddr, size_t size) {
    if (target->mem_fd_alive_check) {
        target->mem_fd_alive_check = 0;
        struct stat s = {};
        int alive_check_result = stat(target->mem_path, &s);
        if (alive_check_result != 0) {
            return PHPSPY_ERR | PHPSPY_ERR_PID_DEAD;
        }
    }
    if (lseek(target->mem_fd, (uint64_t) raddr, SEEK_SET) == -1) {
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

int copy_proc_mem(trace_target_t *target, const char *what, void *raddr, void *laddr, size_t size) {
    if (opt_direct_mem) {
        return copy_proc_mem_direct(target, what, raddr, laddr, size);
    } else {
        return copy_proc_mem_vm_read(target, what, raddr, laddr, size);
    }
}

int get_php_version(trace_target_t *target) {
    struct _zend_module_entry basic_functions_module;
    char version_cmd[1024];
    char phpv[4];
    pid_t pid;
    FILE *pcmd;

    pid = target->pid;
    phpv[0] = '\0';

    /* Try reading basic_functions_module */
    if (target->basic_functions_module_addr) {
        if (copy_proc_mem(target, "basic_functions_module", (void*)target->basic_functions_module_addr, &basic_functions_module, sizeof(basic_functions_module)) == 0) {
            copy_proc_mem(target, "basic_functions_module.version", (void*)basic_functions_module.version, phpv, 3);
        }
    }

    /* Try greping binary */
    if (phpv[0] == '\0') {
        char libname[PHPSPY_STR_SIZE];
        if (shell_escape(opt_libname_awk_patt, libname, sizeof(libname), "opt_libname_awk_patt")) {
            return PHPSPY_ERR;
        }
        int n = snprintf(
            version_cmd,
            sizeof(version_cmd),
            "{ echo -n /proc/%d/root/; "
            "  awk -ve=1 -vp=%s '$0~p{print $NF; e=0; exit} END{exit e}' /proc/%d/maps "
            "  || readlink /proc/%d/exe; } "
            "| { xargs stat --printf=%%n 2>/dev/null || echo /proc/%d/exe; } "
            "| xargs strings "
            "| grep -Po '(?<=X-Powered-By: PHP/)\\d\\.\\d'",
            pid, libname, pid, pid, pid
        );
        if ((size_t)n >= sizeof(version_cmd) - 1) {
            log_error("get_php_version: snprintf overflow\n");
            return PHPSPY_ERR;
        }

        if ((pcmd = popen(version_cmd, "r")) == NULL) {
            perror("get_php_version: popen");
            return PHPSPY_ERR;
        } else if (fread(&phpv, sizeof(char), 3, pcmd) != 3) {
            log_error("get_php_version: Could not detect PHP version\n");
            pclose(pcmd);
            return PHPSPY_ERR;
        }
        pclose(pcmd);
    }

    if      (strncmp(phpv, "7.0", 3) == 0) opt_phpv = "70";
    else if (strncmp(phpv, "7.1", 3) == 0) opt_phpv = "71";
    else if (strncmp(phpv, "7.2", 3) == 0) opt_phpv = "72";
    else if (strncmp(phpv, "7.3", 3) == 0) opt_phpv = "73";
    else if (strncmp(phpv, "7.4", 3) == 0) opt_phpv = "74";
    else if (strncmp(phpv, "8.0", 3) == 0) opt_phpv = "80";
    else if (strncmp(phpv, "8.1", 3) == 0) opt_phpv = "81";
    else if (strncmp(phpv, "8.2", 3) == 0) opt_phpv = "82";
    else if (strncmp(phpv, "8.3", 3) == 0) opt_phpv = "83";
    else {
        log_error("get_php_version: Unrecognized PHP version (%s)\n", phpv);
        return PHPSPY_ERR;
    }

    return PHPSPY_OK;
}

uint64_t phpspy_zend_inline_hash_func(const char *str, size_t len) {
    /* Adapted from zend_string.h */
    uint64_t hash;
    hash = 5381UL;

    for (; len >= 8; len -= 8) {
        hash = ((hash << 5) + hash) + *str++;
        hash = ((hash << 5) + hash) + *str++;
        hash = ((hash << 5) + hash) + *str++;
        hash = ((hash << 5) + hash) + *str++;
        hash = ((hash << 5) + hash) + *str++;
        hash = ((hash << 5) + hash) + *str++;
        hash = ((hash << 5) + hash) + *str++;
        hash = ((hash << 5) + hash) + *str++;
    }
    switch (len) {
        case 7: hash = ((hash << 5) + hash) + *str++; /* fallthrough... */
        case 6: hash = ((hash << 5) + hash) + *str++; /* fallthrough... */
        case 5: hash = ((hash << 5) + hash) + *str++; /* fallthrough... */
        case 4: hash = ((hash << 5) + hash) + *str++; /* fallthrough... */
        case 3: hash = ((hash << 5) + hash) + *str++; /* fallthrough... */
        case 2: hash = ((hash << 5) + hash) + *str++; /* fallthrough... */
        case 1: hash = ((hash << 5) + hash) + *str++; break;
        case 0: break;
    }

    return hash | 0x8000000000000000UL;
}

void log_error(const char *fmt, ...) {
    va_list args;
    if (log_error_enabled) {
        va_start(args, fmt);
        vfprintf(stderr, fmt, args);
    }
}

/* TODO figure out a way to make this cleaner */
#define phpv 70
#include "phpspy_trace_tpl.c"
#undef phpv
#define phpv 71
#include "phpspy_trace_tpl.c"
#undef phpv
#define phpv 72
#include "phpspy_trace_tpl.c"
#undef phpv
#define phpv 73
#include "phpspy_trace_tpl.c"
#undef phpv
#define phpv 74
#include "phpspy_trace_tpl.c"
#undef phpv
#define phpv 80
#include "phpspy_trace_tpl.c"
#undef phpv
#define phpv 81
#include "phpspy_trace_tpl.c"
#undef phpv
#define phpv 82
#include "phpspy_trace_tpl.c"
#undef phpv
#define phpv 83
#include "phpspy_trace_tpl.c"
#undef phpv

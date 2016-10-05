#include <sc_io.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

bool sc_has_errored = false;
bool sc_warn_to_err = false;
const char *sc_stage_name = "initialization";

static void sc_log_v(const char *fmt, va_list args) {
    vprintf(fmt, args);
    putchar('\n');
}

void sc_log(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    sc_log_v(fmt, args);
    va_end(args);
}

void sc_debug(const char *fmt, ...) {
    #if (!defined(NDEBUG) && !defined(DISABLE_DEBUG_TRACES)) || defined(ENABLE_DEBUG_TRACES)
        va_list args;
        va_start(args, fmt);
        vprintf(fmt, args);
        va_end(args);
        putchar('\n');
    #endif
}

void sc_enter_stage(const char *name) {
    if (sc_has_errored) {
        sc_log("Errors present in stage \"%s\", exiting.", sc_stage_name);
        exit(1);
    }
    // TODO: timer end here
    // TODO: timer start here
    // TODO: Show timer in debug log.
    sc_debug("Entering stage \"%s\"", name);
    sc_stage_name = name;
}

static void sc_error_v(bool fatal, const char *fmt, va_list args) {
    vfprintf(stderr, fmt, args);
    putc('\n', stderr);

    if (fatal) {
        sc_log("Encountered fatal error in stage \"%s\", exiting.", sc_stage_name);
        exit(1);
    }

    sc_has_errored = true;
}

void sc_error(bool fatal, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    sc_error_v(fatal, fmt, args);
    va_end(args);
}

void sc_warning(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    if (sc_warn_to_err) {
        sc_error_v(false, fmt, args);
    } else {
        sc_log_v(fmt, args); 
    }
    va_end(args);
}

void sc_warnings_as_errors(bool set) {
    sc_warn_to_err = set;
}

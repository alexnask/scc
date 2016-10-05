#ifndef SC_LOGGING_H__
#define SC_LOGGING_H__

#include <stdbool.h>

// Note: The logging module uses global variables.
void sc_enter_stage(const char *name);

// General purpose logging.
void sc_log(const char *fmt, ...);
// Debug logging.
void sc_debug(const char *fmt, ...);
// Fatal errors cause us to exit instantly.
// Non fatal errors are recoverable to an extent (so we can find errors later on) and cause us to exit when entering another stage.
void sc_error(bool fatal, const char *fmt, ...);
void sc_warning(const char *fmt, ...);
// If we pass true, sc_warning(fmt, ...) calls sc_error(false, fmt, ...)
void sc_warnings_as_errors(bool set);

// TODO: File IO. (temporaries one function make + dump, one function read into buffer with allocator etc.)

#endif

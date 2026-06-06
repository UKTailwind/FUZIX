#ifndef DEBUG_H
#define DEBUG_H
#include <stdio.h>

/* ---- Debug levels ---- */
typedef enum {
    DEBUG_ERROR = 0,
    DEBUG_WARN  = 1,
    DEBUG_INFO  = 2,
    DEBUG_TRACE = 3,
    DEBUG_NONE  = 4
} debug_level_t;

/* Track max files opens */
extern int g_open_files;
extern int g_peak_open_files;

/* Provide __func__ style functionality for older compilers */
/* This returns the function name or the current function for debug logging */
#ifndef FUNC_NAME

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L)
#define FUNC_NAME __func__
#elif defined(__GNUC__)
#define FUNC_NAME __FUNCTION__
#else
#define FUNC_NAME "unknown"
#endif

#endif

/* ---- API ---- */
#ifdef DEBUG_ENABLED
/* Debug code is included */
void debug_init(const char *path, debug_level_t level);
void debug_close(void);
void debug_log(debug_level_t level, const char *func, const char *fmt, ...);
void dump_bytes(debug_level_t level, const char *label, const void *p, size_t n);

#else

/* When DEBUG_ENABLED is absent calls become empty macros: */
#define debug_init(path, level)
#define debug_close()
#define debug_log(level, func, fmt, ...)
#define dump_bytes(level, label, p, n)
#endif

#endif

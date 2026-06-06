#include "debug.h"
#include <stdarg.h>
#include <time.h>
#include <unistd.h>

int g_open_files = 0;
int g_peak_open_files;


#ifdef DEBUG_ENABLED
static FILE *dbg_fp = NULL;

/*
* current_debug_level comes from the shell command line. It's the first parameter
* Make sure DEBUG_ENABLED is set in the Makefile or Debug code will not be compiled in.
* For more than 2 concurrent terminals on Raspberry Pi pico  Debug code must be excluded.
*/

static debug_level_t current_debug_level;

void debug_init(const char *path, debug_level_t level)
{
    current_debug_level = level;

    if (current_debug_level == DEBUG_NONE)
        return;

    dbg_fp = fopen(path, "a");

    /* File open failed */
    if (!dbg_fp) {
        /* Do not assume stderr exists on Fuzix */
        printf("FATAL: cannot open debug log: %s\n", path);
        perror("fopen");
        fflush(stdout);
        _exit(1);   /* use _exit to avoid stdio cleanup issues */
    }
    /* File Open succeeded */
    else{
        g_open_files++;
        if (g_open_files > g_peak_open_files)
            g_peak_open_files = g_open_files;
        debug_log(DEBUG_INFO, FUNC_NAME, "OPEN DEBUG FILE total=%d peak=%d", g_open_files, g_peak_open_files);
    }

#ifdef FUZIX
    setvbuf(dbg_fp, NULL, _IONBF, 0);
#endif
}

void debug_close(void)
{
    if (dbg_fp) {
        g_open_files--;
        debug_log(DEBUG_INFO, FUNC_NAME, "FINAL OPEN FILE COUNT: total=%d peak=%d", g_open_files, g_peak_open_files);

        if (fclose(dbg_fp)){
            dbg_fp = NULL;
            g_open_files--;
            fflush(stdout);
        }
    }
}

void dump_bytes(debug_level_t level, const char *label, const void *p, size_t n)
{
    const unsigned char *b = p;
    debug_log(level, FUNC_NAME, "%s:", label);

    size_t i;
    for (i = 0; i < n; i++)
        debug_log(level, FUNC_NAME, " %02X", b[i]);
}

/* ---- Logging ---- */
void debug_log(debug_level_t level,
               const char *func,
               const char *fmt, ...)
{
    if (level > current_debug_level)
        return;

    if (!dbg_fp)
        return;

    time_t now = time(NULL);
    struct tm *tm = localtime(&now);

    fprintf(dbg_fp, "%02d:%02d:%02d [%s]",
            tm->tm_hour, tm->tm_min, tm->tm_sec, func);

    fprintf(dbg_fp, " ");

    va_list ap;
    va_start(ap, fmt);
    vfprintf(dbg_fp, fmt, ap);
    va_end(ap);

    fputc('\n', dbg_fp);
    fflush(dbg_fp);
}
#endif

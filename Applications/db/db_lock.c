#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include "debug.h"
#include "ui.h"

int db_lock_shared(const char *filename)
{
    (void)filename;
    debug_log(DEBUG_TRACE, FUNC_NAME, "Enter:");
    return 0;
}

int db_lock_exclusive(const char *filename)
{
    debug_log(DEBUG_INFO, FUNC_NAME, "Enter");
    char lockname[128];

    snprintf(lockname, sizeof(lockname), "%s.lock", filename);

    for (;;) {
        int fd = open(lockname, O_CREAT | O_EXCL, 0666);
        if (fd >= 0) {
            close(fd);
            return 0;  /* lock acquired */
        }

        /* someone else holds lock */
        char msg[80];
        snprintf(msg, sizeof(msg), "Database File %s Locked, Retrying...", filename);
        ui_status(msg);
        sleep(1);
    }
}

int db_unlock(const char *filename)
{
    debug_log(DEBUG_INFO, FUNC_NAME, "Enter");
    char lockname[128];

    snprintf(lockname, sizeof(lockname), "%s.lock", filename);

    return unlink(lockname);
}

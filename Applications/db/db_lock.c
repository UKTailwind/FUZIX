#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include "debug.h"
#include "ui.h"

int db_lock_shared(const char *filename)
{
    debug_log(DEBUG_TRACE, FUNC_NAME, "Enter:");
    return 0;
}

int db_lock_exclusive(const char *filename)
{
    char lockname[128];
    char msg[80];

    debug_log(DEBUG_INFO, FUNC_NAME, "Enter");
    snprintf(lockname, sizeof(lockname), "%s.lock", filename);

    for (;;) {
        int fd = open(lockname, O_CREAT | O_EXCL, 0666);
        if (fd >= 0) {
            close(fd);
            return 0;  /* lock acquired */
        }

        /* someone else holds lock */
        snprintf(msg, sizeof(msg), "Database File %s Locked, Retrying...", filename);
        ui_status(msg);
        sleep(1);
    }
}

int db_unlock(const char *filename)
{
    char lockname[128];
    debug_log(DEBUG_INFO, FUNC_NAME, "Enter");

    snprintf(lockname, sizeof(lockname), "%s.lock", filename);

    return unlink(lockname);
}

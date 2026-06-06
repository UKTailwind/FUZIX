/*
* All database writes must go through db_*_write()
*/

#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdio.h>
#include <sys/types.h>
#include "db_state.h"
#include "db_state_layout.h"
#include "db_common.h"
#include "db_lock.h"
#include "ui.h"
#include "debug.h"

int db_state_read(int fd, long recno, state_t *out)
{
    /* Log entry + arguments */
    debug_log(DEBUG_INFO, FUNC_NAME, "Enter: fd=%d recno=%ld out=%p", fd, recno, out);
    if (fd < 0 || recno < 0 || !out){
        debug_log(DEBUG_ERROR, FUNC_NAME, "Invalid args: fd=%d recno=%ld out=%p", fd, recno, out);
        return -1;
    }
    char line[STATE_DISK_LEN];
    off_t off;

    /* Computed File Offset */
    off = (off_t)recno * STATE_DISK_LEN;
    debug_log(DEBUG_INFO, FUNC_NAME, "offset=%ld (recno=%ld * %d)", (long)off, recno, STATE_DISK_LEN);

    if (lseek(fd, off, SEEK_SET) != off){
        debug_log(DEBUG_ERROR, FUNC_NAME, "Error: Reading State");
        debug_log(DEBUG_ERROR, FUNC_NAME, "EOF: offset=%ld", (long)off);
        return -1;
    }

    if (read(fd, line, STATE_DISK_LEN) != STATE_DISK_LEN){
        debug_log(DEBUG_ERROR, FUNC_NAME, "ERROR: State File Length ");
        debug_log(DEBUG_ERROR, FUNC_NAME, "EOF: offset=%ld", (long)off);
        return -1;
    }

    return db_state_parse_line(line, out);
}

int db_state_write(int fd, long recno, const state_t *in)
{
    debug_log(DEBUG_INFO, FUNC_NAME, "Enter: record number=%ld", recno);

    char line[STATE_DISK_LEN];
    off_t off;

    if (fd < 0 || recno < 0 || !in){
        debug_log(DEBUG_ERROR, FUNC_NAME, "Invalid arguments: fd=%d recno=%ld in=%p", fd, recno, in);
        return -1;
    }
    off = (off_t)recno * STATE_DISK_LEN;

    db_state_format_line(in, line);

    if (lseek(fd, off, SEEK_SET) == (off_t)-1) {
        debug_log(DEBUG_ERROR, FUNC_NAME, "lseek failed: fd=%d off=%ld errno=%d", fd, (long)off, errno);
        return -1;
    }
    ssize_t rc = write(fd, line, STATE_DISK_LEN);
    if (rc != STATE_DISK_LEN) {
        debug_log(DEBUG_ERROR, FUNC_NAME, "write failed: fd=%d wanted=%d wrote=%ld errno=%d", fd, STATE_DISK_LEN, (long)rc, errno);
        return -1;
    }
    return 0;
}


int db_state_parse_line(const char *line, state_t *out)
{
    debug_log(DEBUG_TRACE, FUNC_NAME, "Enter:");
    char buf[8];

    if (!line || !out)
        return -1;

    /* State ID */
    SAFE_COPY(out->state_id,
              line + STATE_ID_OFF,
              STATE_ID_LEN);

    /* Sort order */
    SAFE_COPY(buf,
              line + STATE_SORT_OFF,
              STATE_SORT_LEN);
    buf[STATE_SORT_LEN] = '\0';
    out->sort_order = atoi(buf);

    /* State name */
    SAFE_COPY(out->name,
              line + STATE_NAME_OFF,
              STATE_NAME_LEN);

    return 0;
}

void db_state_format_line(const state_t *in, char *line)
{
    debug_log(DEBUG_TRACE, FUNC_NAME, "Enter:");
    if (!in || !line)
        return;

    memset(line, ' ', STATE_RECORD_LEN);

    line[STATE_ID_OFF   + STATE_ID_LEN]   = FIELD_SEP;
    line[STATE_SORT_OFF + STATE_SORT_LEN] = FIELD_SEP;

    strncpy(line + STATE_ID_OFF,
            in->state_id,
            strnlen(in->state_id, STATE_ID_LEN));

    char tmp[5];
    sprintf(tmp, "%04d", in->sort_order);
    memcpy(line + STATE_SORT_OFF, tmp, STATE_SORT_LEN);

    strncpy(line + STATE_NAME_OFF,
            in->name,
            strnlen(in->name, STATE_NAME_LEN));

    line[STATE_RECORD_LEN] = '\n';
}

int db_state_open(void)
{
    debug_log(DEBUG_INFO, FUNC_NAME, "Enter: ");

    int fd = open("data/state.db", O_RDWR);
    if (fd < 0) {
        debug_log(DEBUG_ERROR, FUNC_NAME, "Failed to open state");
        ui_status("Failed to open state file");
        sleep(2);
        return -1;
    }

    /* Check state file */
    struct stat st;
    if (fstat(fd, &st) != 0) {
        debug_log(DEBUG_ERROR, FUNC_NAME, "Cannot stat state database");
        ui_status("Cannot stat state database");
        close(fd);
        sleep(2);
        return -1;
    }

    if (st.st_size % STATE_DISK_LEN != 0) {
        debug_log(DEBUG_ERROR, FUNC_NAME,
                  "State DB corrupt (bad record length)");
        ui_status("State DB corrupt (bad record length)");
        close(fd);
        sleep(2);
        return -1;
    }
    /* Count open files */
    g_open_files++;

    if (g_open_files > g_peak_open_files)
        g_peak_open_files = g_open_files;
    debug_log(DEBUG_INFO, FUNC_NAME, "OPEN READ fd=%d total=%d peak=%d", fd, g_open_files, g_peak_open_files);

    return fd;
}

int db_state_name_from_id(int fd, const char *state_id, char *state_name)
{
    debug_log(DEBUG_TRACE, FUNC_NAME, "Enter:");
    char line[STATE_DISK_LEN];
    ssize_t n;

    /* start at beginning of file */
    lseek(fd, 0, SEEK_SET);

    while ((n = read(fd, line, STATE_DISK_LEN)) == STATE_DISK_LEN) {

        char sid[STATE_ID_MAX + 1];

        strncpy(sid, line + STATE_ID_OFF, STATE_ID_LEN);
        sid[STATE_ID_LEN] = '\0';

        if (strcmp(sid, state_id) == 0) {

            strncpy(state_name, line + STATE_NAME_OFF, STATE_NAME_LEN);
            state_name[STATE_NAME_LEN] = '\0';

            return 0;   /* found */
        }
    }

    return -1;  /* not found */
}



/* Close database */
int db_state_close(int fd)
{
    debug_log(DEBUG_INFO, FUNC_NAME, "Enter: ");

    if (fd < 0)
        return 0;
    int rc = close(fd);
    if (rc == 0) {
        g_open_files--;
        debug_log(DEBUG_INFO, FUNC_NAME, "CLOSE READ fd=%d total=%d", fd, g_open_files);
    }
    return rc;
}

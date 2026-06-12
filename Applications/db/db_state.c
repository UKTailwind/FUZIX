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
#include "ui.h"
#include "debug.h"

int db_state_read(long recno, state_t *out)
{
    char line[STATE_DISK_LEN];
    off_t off;

    /* Log entry + arguments */
    debug_log((DEBUG_INFO, FUNC_NAME, "Enter: fd=%d recno=%ld out=%p", state_db->fd, recno, out));
    if (state_db->fd < 0 || recno < 0 || !out){
        debug_log((DEBUG_ERROR, FUNC_NAME, "Invalid args: fd=%d recno=%ld out=%p", state_db->fd, recno, out));
        return -1;
    }
    /* Computed File Offset */
    off = (off_t)recno * STATE_DISK_LEN;
    debug_log((DEBUG_INFO, FUNC_NAME, "offset=%ld (recno=%ld * %d)", (long)off, recno, STATE_DISK_LEN));

    if (lseek(state_db->fd, off, SEEK_SET) != off){
        debug_log((DEBUG_ERROR, FUNC_NAME, "Error: Reading State"));
        debug_log((DEBUG_ERROR, FUNC_NAME, "EOF: offset=%ld", (long)off));
        return -1;
    }

    if (read(state_db->fd, line, STATE_DISK_LEN) != STATE_DISK_LEN){
        debug_log((DEBUG_ERROR, FUNC_NAME, "ERROR: State File Length "));
        debug_log((DEBUG_ERROR, FUNC_NAME, "EOF: offset=%ld", (long)off));
        return -1;
    }

    return db_state_parse_line(line, out);
}

int db_state_write(long recno, const state_t *in)
{
    char line[STATE_DISK_LEN];
    off_t off;
    ssize_t rc;

    debug_log((DEBUG_INFO, FUNC_NAME, "Enter: record number=%ld", recno));

    if (state_db->fd < 0 || recno < 0 || !in){
        debug_log((DEBUG_ERROR, FUNC_NAME, "Invalid arguments: fd=%d recno=%ld in=%p", state_db->fd, recno, in));
        return -1;
    }
    off = (off_t)recno * STATE_DISK_LEN;

    db_state_format_line(in, line);

    if (lseek(state_db->fd, off, SEEK_SET) == (off_t)-1) {
        debug_log((DEBUG_ERROR, FUNC_NAME, "lseek failed: fd=%d off=%ld errno=%d", state_db->fd, (long)off, errno));
        return -1;
    }
    rc = write(state_db->fd, line, STATE_DISK_LEN);
    if (rc != STATE_DISK_LEN) {
        debug_log((DEBUG_ERROR, FUNC_NAME, "write failed: fd=%d wanted=%d wrote=%ld errno=%d", state_db->fd, STATE_DISK_LEN, (long)rc, errno));
        return -1;
    }
    return 0;
}


int db_state_parse_line(const char *line, state_t *out)
{
    char buf[8];

    debug_log((DEBUG_TRACE, FUNC_NAME, "Enter:"));
    if (!line || !out)
        return -1;

    /* State ID */
    copy_record(out->state_id,
              line + STATE_ID_OFF,
              STATE_ID_LEN);

    /* Sort order */
    copy_record(buf,
              line + STATE_SORT_OFF,
              STATE_SORT_LEN);
    buf[STATE_SORT_LEN] = '\0';
    out->sort_order = atoi(buf);

    /* State name */
    copy_record(out->name,
              line + STATE_NAME_OFF,
              STATE_NAME_LEN);

    return 0;
}

void db_state_format_line(const state_t *in, char *line)
{
    char tmp[5];

    debug_log((DEBUG_TRACE, FUNC_NAME, "Enter:"));
    if (!in || !line)
        return;

    memset(line, ' ', STATE_RECORD_LEN);

    line[STATE_ID_OFF   + STATE_ID_LEN]   = FIELD_SEP;
    line[STATE_SORT_OFF + STATE_SORT_LEN] = FIELD_SEP;

    strncpy(line + STATE_ID_OFF,
            in->state_id,
            strnlen(in->state_id, STATE_ID_LEN));

    sprintf(tmp, "%04d", in->sort_order);
    memcpy(line + STATE_SORT_OFF, tmp, STATE_SORT_LEN);

    strncpy(line + STATE_NAME_OFF,
            in->name,
            strnlen(in->name, STATE_NAME_LEN));

    line[STATE_RECORD_LEN] = '\n';
}

int db_state_name_from_id(const char *state_id, char *state_name)
{
    unsigned recno = 0;
    debug_log((DEBUG_TRACE, FUNC_NAME, "Enter:"));

    while (db_read(state_db, recno) > 0) {
        char sid[STATE_ID_MAX + 1];

        strncpy(sid, state_db->buf + STATE_ID_OFF, STATE_ID_LEN);
        sid[STATE_ID_LEN] = '\0';

        if (strcmp(sid, state_id) == 0) {

            strncpy(state_name, state_db->buf + STATE_NAME_OFF, STATE_NAME_LEN);
            state_name[STATE_NAME_LEN] = '\0';

            return 0;   /* found */
        }
        recno++;
    }

    return -1;  /* not found */
}

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
#include "db_staff.h"
#include "db_staff_layout.h"
#include "db_common.h"
#include "db_lock.h"
#include "ui.h"
#include "debug.h"


int db_staff_lookup_display(const char *staff_id, char *out, size_t outlen)
{
    staff_t st;
    long rec = 0;

    debug_log((DEBUG_TRACE, FUNC_NAME, "Enter:"));

    while (db_staff_read(rec, &st) == 0) {
        if (strcmp(st.staff_id, staff_id) == 0) {
            ui_rtrim(st.staff_name);
            snprintf(out, outlen, "%s", st.staff_name);
            return 0;
        }

        rec++;
    }
    debug_log((DEBUG_ERROR, FUNC_NAME, "*** ERROR *** Staff Not Found"));
    return -1;
}


int db_staff_read(long recno, staff_t *out)
{
    debug_log((DEBUG_TRACE, FUNC_NAME, "Enter: fd=%d recno=%ld out=%p", staff_db->fd, recno, out));
    if (staff_db->fd < 0 || recno < 0 || !out) {
        debug_log((DEBUG_ERROR, FUNC_NAME, "Invalid args: fd=%d recno=%ld out=%p", staff_db->fd, recno, out));
        return -1;
    }
    if (db_read(staff_db, recno) <= 0)
        return -1;
    return db_staff_parse_line(staff_db->buf, out);
}

int db_staff_read_by_id(const char *staff_id, staff_t *out, long *out_recno)
{
    int rc;
    debug_log((DEBUG_INFO, FUNC_NAME, "Enter:"));
    rc = db_find(staff_db, staff_id, STAFF_ID_OFF, STAFF_ID_LEN);
    if (rc <= 0)
        return -1;
    if (out_recno)
        *out_recno = staff_db->pos;
    return db_staff_parse_line(staff_db->buf, out);
}

int db_staff_generate_next_id(char *out_id)
{
    staff_t tmp;
    long recno = 0;
    long max_id = 0;
    long next_id;

    debug_log((DEBUG_TRACE, FUNC_NAME, "Enter:"));

    if (!out_id) {
        return -1;
    }

    while (db_staff_read(recno, &tmp) == 0) {

        if (tmp.staff_id[0] != '\0') {
            char *endp;
            long id = strtol(tmp.staff_id, &endp, 10);

            /* accept only fully numeric IDs */
            if (*endp == '\0' && id > max_id) {
                max_id = id;
            }
        }

        recno++;
    }

    next_id = max_id + 1;

    if (next_id >= ID_MAX_VALUE) {
        debug_log((DEBUG_ERROR, FUNC_NAME, "Staff ID space exhausted (max %d)", ID_LEN));
        return -1;
    }
    /* zero-padded fixed width */
    snprintf(out_id, ID_LEN + 1, "%06lu", (unsigned long)next_id);

    return 0;
}

long db_staff_record_count(void)
{
    staff_t tmp;
    long recno = 0;
    long count = 0;

    debug_log((DEBUG_TRACE, FUNC_NAME, "Enter:"));

    while (db_staff_read(recno, &tmp) == 0) {
        /* Skip deleted records */
        if (tmp.staff_status[0] == STAFF_STATUS_DELETED) {
            recno++;
            continue;
        }
        /* Count active records */
        count++;
        recno++;
    }
    debug_log((DEBUG_INFO, FUNC_NAME, "Active staff count=%ld of %ld", count, MAX_STAFF_ENTRIES));
    return count;
}

int db_staff_write(long *recno, const staff_t *in)
{
    char line[STAFF_DISK_LEN];
    off_t off;
    ssize_t rc;

    debug_log((DEBUG_TRACE, FUNC_NAME, "Enter: recno=%ld", recno ? *recno : -1));

    if (in->staff_id[0] == '\0') {
        debug_log((DEBUG_ERROR, FUNC_NAME, "Unable to write staff with empty ID "));
        return -1;
    }

    if (staff_db->fd < 0 || !recno || !in) {
        debug_log((DEBUG_ERROR, FUNC_NAME, "Invalid arguments: fd=%d recno=%p in=%p", staff_db->fd, recno, in));
        return -1;
    }

    /* NEW record: append */
    if (*recno < 0) {
        long count = db_staff_record_count();
        if (count < 0) {
            debug_log((DEBUG_ERROR, FUNC_NAME, "Unable to determine staff count"));
            return -1;
        }

        if (count >= MAX_STAFF_ENTRIES) {
            debug_log((DEBUG_WARN, FUNC_NAME, "Staff limit reached (%ld)", count));
            errno = ENOSPC;   /* optional */
            return -1;
        }
        /* Format record into on-disk layout */
        db_staff_format_line(in, line);

        rc = db_append(staff_db);
        *recno = staff_db->pos;
    } else {
        /* UPDATE existing record */
        /* Format record into on-disk layout */
        db_staff_format_line(in, line);
        rc = db_write(staff_db, *recno);
    }
    if (rc < 0) {
        debug_log((DEBUG_ERROR, FUNC_NAME,
                  "write failed: errno=%d", errno));
        return -1;
    }
    debug_log((DEBUG_TRACE, FUNC_NAME, "STAFF written recno=%ld", *recno));
    return 0;
}

int db_staff_load_all(register StaffList *list)
{
    long rec = 0;
    staff_t c;

    debug_log((DEBUG_TRACE, FUNC_NAME,"Enter: Loading all staff fd=%d", staff_db->fd));
    if (!list)
        return -1;

    list->count = 0;

    while (list->count < MAX_STAFF_ENTRIES) {
        staff_list_rec_t *dst;

        if (db_staff_read(rec, &c) != 0)
            break;

        rec++;   /* always advance disk record */

        /* Skip deleted staff */
        if (c.staff_status[0] == STAFF_STATUS_DELETED)
            continue;

        dst = &list->slots[list->count++];

        strcpy(dst->staff_id,     c.staff_id);
        strcpy(dst->staff_name,   c.staff_name);
        strcpy(dst->staff_phone, c.staff_phone);

        if (list->count >= MAX_STAFF_ENTRIES) {
            debug_log((DEBUG_WARN, FUNC_NAME, "Staff list truncated at MAX_STAFF_ENTRIES=%d", MAX_STAFF_ENTRIES));
        }
    }

    debug_log((DEBUG_TRACE, FUNC_NAME,"Complete: Loading all staff count=%d", list->count));
    return 0;
}

int db_staff_parse_line(const char *line, register staff_t *out)
{
    debug_log((DEBUG_TRACE, FUNC_NAME, "Enter:"));

    if (!line || !out)
        return -1;

    copy_record(out->staff_status,  line + STAFF_STATUS_OFF, STAFF_STATUS_LEN);
    copy_record(out->staff_id,      line + STAFF_ID_OFF,     STAFF_ID_LEN);
    copy_record(out->staff_name,    line + STAFF_NAME_OFF,   STAFF_NAME_LEN);
    copy_record(out->staff_phone,   line + STAFF_PHONE_OFF,  STAFF_PHONE_LEN);
    copy_record(out->staff_notes,   line + STAFF_NOTES_OFF,  STAFF_NOTES_LEN);

    return 0;
}

void db_staff_format_line(register const staff_t *in, char *line)
{
    debug_log((DEBUG_TRACE, FUNC_NAME, "Enter:"));
    if (!in || !line)
        return;

    memset(line, ' ', STAFF_RECORD_LEN);

    /* separators */
    line[STAFF_STATUS_OFF   + STAFF_STATUS_LEN]    = FIELD_SEP;
    line[STAFF_ID_OFF       + STAFF_ID_LEN]        = FIELD_SEP;
    line[STAFF_NAME_OFF     + STAFF_NAME_LEN]      = FIELD_SEP;
    line[STAFF_PHONE_OFF    + STAFF_PHONE_LEN]     = FIELD_SEP;
    /* notes is last – no FIELD_SEP */

    /* Fields */
    strncpy(line + STAFF_STATUS_OFF, in->staff_status,   strnlen(in->staff_id, STAFF_STATUS_LEN));
    strncpy(line + STAFF_ID_OFF,     in->staff_id,       strnlen(in->staff_id, STAFF_ID_LEN));
    strncpy(line + STAFF_NAME_OFF,   in->staff_name,     strnlen(in->staff_name, STAFF_NAME_LEN));
    strncpy(line + STAFF_PHONE_OFF,  in->staff_phone,    strnlen(in->staff_phone, STAFF_PHONE_LEN));
    strncpy(line + STAFF_NOTES_OFF,  in->staff_notes,	 strnlen(in->staff_notes, STAFF_NOTES_LEN));

    line[STAFF_RECORD_LEN] = '\n';
}

/*
* All database writes must go through db_*_write()
*/

#include "db_booking.h"
#include "db_booking_layout.h"
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdio.h>
#include <sys/types.h>
#include "ui.h"
#include "debug.h"
#include "db_lock.h"
#include "db_common.h"
#include "db_common_layout.h"

/* Parse a single record into booking_t */
int db_bk_parse_line(const char *line, register booking_t *out){
    char buf[16];
    debug_log((DEBUG_TRACE, FUNC_NAME, "Enter: "));

    debug_log((DEBUG_TRACE, FUNC_NAME,
        "Line bytes before parse: %02X %02X %02X %02X %02X %02X %02X %02X ...",
        (unsigned char)line[0],
        (unsigned char)line[1],
        (unsigned char)line[2],
        (unsigned char)line[3],
        (unsigned char)line[4],
        (unsigned char)line[5],
        (unsigned char)line[6],
        (unsigned char)line[7]));


    if (!line || !out)
    {
        debug_log((DEBUG_ERROR, FUNC_NAME, "Booking Parse Error:%. line=%line", line));
        debug_log((DEBUG_ERROR, FUNC_NAME, "Booking parse error: line=%p out=%p", (void *)line, (void *)out));
        return -1;
    }

    /* Defensive: wipe struct */
    memset(out, 0, sizeof(*out));

    /* Booking status */
    copy_record(out->booking_status, line + BOOKING_STATUS_OFF, BOOKING_STATUS_LEN);
    out->booking_status[BOOKING_STATUS_LEN] = '\0';

    debug_log((DEBUG_TRACE, FUNC_NAME, "PARSED status='%c' (0x%02X)", out->booking_status[0], (unsigned char)out->booking_status[0]));

    /* Booking ID */
    copy_record(out->booking_id, line + BOOKING_ID_OFF, BOOKING_ID_LEN);
    out->booking_id[BOOKING_ID_LEN] = '\0';

    /* Customer ID */
    copy_record(out->booking_customer_id, line + BOOKING_CUSTOMER_ID_OFF, BOOKING_CUSTOMER_ID_LEN);
    out->booking_customer_id[BOOKING_CUSTOMER_ID_LEN] ='\0';

    /* Date YYYYMMDD */
    copy_record(buf, line + BOOKING_DATE_OFF, BOOKING_DATE_LEN);
    buf[BOOKING_DATE_LEN] = '\0';

    copy_record(out->booking_date, buf,BOOKING_DATE_LEN);
    out->booking_date[BOOKING_DATE_LEN] = '\0';

    /* Start time HHMM */
    copy_record(buf, line + BOOKING_START_TIME_OFF, BOOKING_START_TIME_LEN);
    buf[BOOKING_START_TIME_LEN] = '\0';

    copy_record(out->booking_start_time, buf,BOOKING_START_TIME_LEN);
    out->booking_start_time[BOOKING_START_TIME_LEN] = '\0';

    /* End time HHMM */
    copy_record(buf, line + BOOKING_END_TIME_OFF, BOOKING_END_TIME_LEN);
    buf[BOOKING_END_TIME_LEN] = '\0';

    copy_record(out->booking_end_time, buf,BOOKING_END_TIME_LEN);
    out->booking_end_time[BOOKING_END_TIME_LEN] = '\0';

    /* Staff ID */
    copy_record(out->booking_staff_id, line + BOOKING_STAFF_ID_OFF, BOOKING_STAFF_ID_LEN);
    out->booking_staff_id[BOOKING_STAFF_ID_LEN] = '\0';

    /* State ID */
    copy_record(out->booking_state_id, line + BOOKING_STATE_ID_OFF, BOOKING_STATE_ID_LEN);
    out->booking_state_id[BOOKING_STATE_ID_LEN] = '\0';

    /* Job description */
    copy_record(out->booking_job, line + BOOKING_JOB_OFF, BOOKING_JOB_LEN);
    out->booking_job[BOOKING_JOB_LEN] = '\0';

    return 0;
}

/* Clears the line, Inserts separators deterministically, Guarantees fixed record length*/
void db_bk_format_line(register const booking_t *in, char *line)
{
    int i;
    debug_log((DEBUG_TRACE, FUNC_NAME, "Enter: in=%p line=%p BOOKING_RECORD_LEN=%d", in, line, BOOKING_RECORD_LEN));

    if (!in || !line)
        return;

    debug_log((DEBUG_TRACE, FUNC_NAME, "formatter booking_id='%s' booking_customer_id='%s'", in->booking_id, in->booking_customer_id));

    /* Fill entire record with spaces */
    memset(line, ' ', BOOKING_DISK_LEN);

    /* Field separators */
    line[BOOKING_STATUS_OFF       + BOOKING_STATUS_LEN]       = FIELD_SEP;
    line[BOOKING_ID_OFF           + BOOKING_ID_LEN]           = FIELD_SEP;
    line[BOOKING_CUSTOMER_ID_OFF  + BOOKING_CUSTOMER_ID_LEN]  = FIELD_SEP;
    line[BOOKING_DATE_OFF         + BOOKING_DATE_LEN]         = FIELD_SEP;
    line[BOOKING_START_TIME_OFF   + BOOKING_START_TIME_LEN]   = FIELD_SEP;
    line[BOOKING_END_TIME_OFF     + BOOKING_END_TIME_LEN]     = FIELD_SEP;
    line[BOOKING_STAFF_ID_OFF     + BOOKING_STAFF_ID_LEN]     = FIELD_SEP;
    line[BOOKING_STATE_ID_OFF     + BOOKING_STATE_ID_LEN]     = FIELD_SEP;
    /* job is last – no FIELD_SEP */

    /* Fields */
    line[BOOKING_STATUS_OFF] = (in->booking_status[0] != '\0') ? in->booking_status[0] : BOOKING_STATUS_ACTIVE;

    strncpy(line + BOOKING_ID_OFF,          in->booking_id,          strnlen(in->booking_id,          BOOKING_ID_LEN));
    strncpy(line + BOOKING_CUSTOMER_ID_OFF, in->booking_customer_id, strnlen(in->booking_customer_id, BOOKING_CUSTOMER_ID_LEN));
    strncpy(line + BOOKING_DATE_OFF,        in->booking_date,        strnlen(in->booking_date,        BOOKING_DATE_LEN));
    strncpy(line + BOOKING_START_TIME_OFF,  in->booking_start_time,  strnlen(in->booking_start_time,  BOOKING_START_TIME_LEN));
    strncpy(line + BOOKING_END_TIME_OFF,    in->booking_end_time,    strnlen(in->booking_end_time,    BOOKING_END_TIME_LEN));
    strncpy(line + BOOKING_STAFF_ID_OFF,    in->booking_staff_id,    strnlen(in->booking_staff_id,    BOOKING_STAFF_ID_LEN));
    strncpy(line + BOOKING_STATE_ID_OFF,    in->booking_state_id,    strnlen(in->booking_state_id,    BOOKING_STATE_ID_LEN));
    strncpy(line + BOOKING_JOB_OFF,         in->booking_job,         strnlen(in->booking_job,         BOOKING_JOB_LEN));

    /* Record terminator */
    line[BOOKING_RECORD_LEN] = '\n';

    for (i = 0; i < BOOKING_RECORD_LEN; i++) {
        if (line[i] == '\0') {
            debug_log((DEBUG_ERROR, FUNC_NAME, "*** ERROR *** NUL byte found at offset %d", i));
        }
    }
}

int db_bk_read(long recno, register booking_t *out)
{
    off_t off = (off_t)recno * BOOKING_DISK_LEN;
    char *line = booking_db->buf;

    /* -ve error, 0 EOF. Really ought to make callers distinguish! */
    if (db_read(booking_db, recno) <= 0)
        return -1;
    return db_bk_parse_line(booking_db->buf, out);
}

int db_bk_by_id(const char *booking_id, register booking_t *out, long *out_recno)
{
    booking_t tmp;
    int rc;

    debug_log((DEBUG_TRACE, FUNC_NAME, "Enter:"));

    rc = db_find(booking_db, booking_id, BOOKING_ID_OFF, BOOKING_ID_LEN);
    if (rc <= 0)
        return -1;
    if (out_recno)
        *out_recno = booking_db->pos;
    return db_bk_parse_line(booking_db->buf, out);
}

int db_bk_by_index(const DayBookings *day, int index, booking_t *out)
{
    debug_log((DEBUG_TRACE, FUNC_NAME, "Enter:"));
    if (index < 0 || index >= day->count)
        return -1;

    return db_bk_read(day->recnos[index], out);
}

int db_bk_write(long recno, const booking_t *in)
{
    debug_log((DEBUG_INFO, FUNC_NAME, "Enter: record number=%ld", recno));

    if (recno < 0 || !in) {
        debug_log((DEBUG_ERROR, FUNC_NAME, "Invalid arguments: fd=%d recno=%ld in=%p", booking_db->fd, recno, (void *)in));
        return -1;
    }
    db_bk_format_line(in, booking_db->buf);
    return db_write(booking_db, recno);
}

void db_bk_sort_day_by_time(register DayBookings *day)
{
    int i;
    booking_t a, b;

    debug_log((DEBUG_TRACE, FUNC_NAME, "Enter:"));

    for (i = 0; i < day->count - 1; i++) {
        int j;
        for (j = i + 1; j < day->count; j++) {

            db_bk_read(day->recnos[i], &a);
            db_bk_read(day->recnos[j], &b);

            if (strcmp(a.booking_start_time, b.booking_start_time) > 0) {
                long tmp = day->recnos[i];
                day->recnos[i] = day->recnos[j];
                day->recnos[j] = tmp;
            }
        }
    }
}

int db_bk_append(const booking_t *in)
{
    char line[BOOKING_DISK_LEN];
    ssize_t rc;

    debug_log((DEBUG_TRACE, FUNC_NAME, "Enter:"));

    if (booking_db->fd < 0 || !in) {
        debug_log((DEBUG_ERROR, FUNC_NAME, "Invalid arguments: fd=%d in=%p", booking_db->fd, (void *)in));
        return -1;
    }

    db_bk_format_line(in, line);

    if (lseek(booking_db->fd, 0, SEEK_END) == (off_t)-1) {
        debug_log((DEBUG_ERROR, FUNC_NAME, "lseek(SEEK_END) failed errno=%d", errno));
        return -1;
    }

    rc = write(booking_db->fd, line, BOOKING_DISK_LEN);
    if (rc != BOOKING_DISK_LEN) {
        debug_log((DEBUG_ERROR, FUNC_NAME,
                  "append write failed wanted=%d wrote=%ld errno=%d",
                  BOOKING_DISK_LEN, (long)rc, errno));
        return -1;
    }

    return 0;
}

int db_bk_generate_next_id(char *out_id)
{
    booking_t tmp;
    long recno = 0;
    long max_id = 0;
    long next_id;

    debug_log((DEBUG_TRACE, FUNC_NAME, "Enter:"));

    if (!out_id) {
        debug_log((DEBUG_ERROR, FUNC_NAME, "Failed to generate next booking ID"));
        return -1;
    }

    while (db_bk_read(recno, &tmp) == 0) {

        if (tmp.booking_id[0] != '\0') {
            char *endp;
            long id = strtol(tmp.booking_id, &endp, 10);

            /* accept only fully numeric IDs */
            if (*endp == '\0' && id > max_id) {
                max_id = id;
            }
        }

        recno++;
    }

    next_id = max_id + 1;

    if (next_id >= ID_MAX_VALUE) {
        debug_log((DEBUG_ERROR, FUNC_NAME, "Booking ID space exhausted (max %d)", ID_LEN));
        return -1;
    }
    /* zero-padded fixed width */
    snprintf(out_id, ID_LEN + 1, "%06lu", (unsigned long)next_id);

    return 0;
}


/* Sort Bookings by Start time */
int cmp_booking_time(const void *a, const void *b)
{
    const booking_t *ba = a;
    const booking_t *bb = b;

    debug_log((DEBUG_TRACE, FUNC_NAME, "Enter:"));

    return strcmp(ba->booking_start_time,
                  bb->booking_start_time);
}

int db_bk_build_day_index(int target_date, DayBookings *day)
{
    booking_t b;
    long recno = 0;

    debug_log((DEBUG_TRACE, FUNC_NAME, "Enter:"));

    if (booking_db->fd < 0 || !day)
        return -1;

    day->count = 0;

    while (db_bk_read(recno, &b) == 0) {

        /* Match date and skip deleted bookings */
        if (atoi(b.booking_date) == target_date &&
            b.booking_status[0] != BOOKING_STATUS_DELETED)
        {
            if (day->count < MAX_DAY_BOOKINGS) {
                day->recnos[day->count++] = recno;
            }
        }

        recno++;
    }

    /* Always return sorted results */
    db_bk_sort_day_by_time(day);

    return 0;
}

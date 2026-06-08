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
#include "db_customer.h"
#include "db_customer_layout.h"
#include "db_common.h"
#include "db_lock.h"
#include "ui.h"
#include "debug.h"

int db_cs_lookup_display(const char *customer_id, char *out, size_t outlen)
{
    customer_t st;
    long rec = 0;

    debug_log((DEBUG_INFO, FUNC_NAME, "Enter:"));
    while (db_cs_read(rec, &st) == 0) {
        if (strcmp(st.cs_id, customer_id) == 0) {
            ui_rtrim(st.cs_name);
            ui_rtrim(st.cs_address1);
            snprintf(out, outlen, "%s: %s", st.cs_name, st.cs_address1);
            return 0;
        }

        rec++;
    }
    debug_log((DEBUG_ERROR, FUNC_NAME, "*** ERROR *** Customer Not Found"));
    return -1;
}


int db_cs_read(long recno, customer_t *out)
{
    char line[CUSTOMER_DISK_LEN];
    off_t off = (off_t)recno * CUSTOMER_DISK_LEN;
    ssize_t n;

    debug_log((DEBUG_TRACE, FUNC_NAME, "Enter: fd=%d recno=%ld out=%p", customer_db->fd, recno, out));
    if (customer_db->fd < 0 || recno < 0 || !out) {
        debug_log((DEBUG_ERROR, FUNC_NAME, "Invalid args: fd=%d recno=%ld out=%p", customer_db->fd, recno, out));
        return -1;
    }
    debug_log((DEBUG_TRACE, FUNC_NAME, "offset=%ld (recno=%ld * %d)", (long)off, recno, CUSTOMER_DISK_LEN));

    if (lseek(customer_db->fd, off, SEEK_SET) == (off_t)-1) {
        debug_log((DEBUG_WARN, FUNC_NAME, "Warning: lseek failed for customer read"));
        debug_log((DEBUG_WARN, FUNC_NAME, "Warning: offset=%ld", (long)off));
        return CUSTOMER_DB_EOF;
    }

    n = read(customer_db->fd, line, CUSTOMER_DISK_LEN);
    if (n == 0) {
        debug_log((DEBUG_TRACE, FUNC_NAME, "EOF reached at recno=%ld offset=%ld", recno, (long)off));
        return CUSTOMER_DB_EOF;
    }
    if (n != CUSTOMER_DISK_LEN) {
        debug_log((DEBUG_ERROR, FUNC_NAME, "ERROR: Customer File Length (read %zd, expected %d)", n, CUSTOMER_DISK_LEN));
        return CUSTOMER_DB_ERROR;
    }
    return db_cs_parse_line(line, out);
}

int db_cs_by_id( const char *customer_id, customer_t *out, long *out_recno)
{
    customer_t tmp;
    long recno = 0;

    debug_log((DEBUG_INFO, FUNC_NAME, "Enter:"));

    while (db_cs_read(recno, &tmp) == 0) {
        if (strcmp(tmp.cs_id, customer_id) == 0) {
            memcpy(out, &tmp, sizeof(tmp));   /* struct copy */
            if (out_recno)
                *out_recno = recno;

            debug_log((
                DEBUG_INFO, FUNC_NAME, "Customer found id=%s recno=%ld", customer_id, recno));
            return 0;
        }
        recno++;
    }
    return -1;   /* not found */
}

int db_cs_generate_next_id(char *out_id)
{
    customer_t tmp;
    long recno = 0;
    long max_id = 0;
    long next_id;

    debug_log((DEBUG_TRACE, FUNC_NAME, "Enter:"));

    if (!out_id) {
        return -1;
    }

    while (db_cs_read(recno, &tmp) == 0) {

        if (tmp.cs_id[0] != '\0') {
            char *endp;
            long id = strtol(tmp.cs_id, &endp, 10);

            /* accept only fully numeric IDs */
            if (*endp == '\0' && id > max_id) {
                max_id = id;
            }
        }

        recno++;
    }

    next_id = max_id + 1;

    if (next_id >= ID_MAX_VALUE) {
        debug_log((DEBUG_ERROR, FUNC_NAME, "Customer ID space exhausted (max %d)", ID_LEN));
        return -1;
    }
    /* zero-padded fixed width */
    snprintf(out_id, ID_LEN + 1, "%06lu", (unsigned long)next_id);

    return 0;
}

long db_cs_record_count(void)
{
    customer_t tmp;
    long recno = 0;
    long count = 0;

    debug_log((DEBUG_TRACE, FUNC_NAME, "Enter:"));

    if (customer_db->fd < 0)
        return -1;

    while (db_cs_read(recno, &tmp) == 0) {
        /* Skip deleted records */
        if (tmp.cs_status[0] == CUSTOMER_STATUS_DELETED) {
            recno++;
            continue;
        }
        /* Count active records */
        count++;
        recno++;
    }
    debug_log((DEBUG_INFO, FUNC_NAME, "Active customer count=%ld of %ld", count, MAX_CUSTOMER_ENTRIES));
    return count;
}


int db_cs_write(long *recno, const customer_t *in)
{
    char line[CUSTOMER_DISK_LEN];
    off_t off;
    ssize_t rc;

    debug_log((DEBUG_INFO, FUNC_NAME, "Enter: recno=%ld", recno ? *recno : -1));

    if (in->cs_id[0] == '\0') {
        debug_log((DEBUG_ERROR, FUNC_NAME, "Unable to write customer with empty ID "));
        return -1;
    }

    if (customer_db->fd < 0 || !recno || !in) {
        debug_log((DEBUG_ERROR, FUNC_NAME, "Invalid arguments: fd=%d recno=%p in=%p", customer_db->fd, recno, in));
        return -1;
    }

    /* Format record into on-disk layout */
    db_cs_format_line(in, line);

    /* NEW record: append */
    if (*recno < 0) {
        long count = db_cs_record_count();
        if (count < 0) {
            debug_log((DEBUG_ERROR, FUNC_NAME, "Unable to determine customer count"));
            return -1;
        }

        if (count >= MAX_CUSTOMER_ENTRIES) {
            debug_log((DEBUG_WARN, FUNC_NAME, "Customer limit reached (%ld)", count));
            errno = ENOSPC;   /* optional but nice */
            return -1;
        }

        off = lseek(customer_db->fd, 0, SEEK_END);
        if (off == (off_t)-1) {
            debug_log((DEBUG_ERROR, FUNC_NAME, "lseek(SEEK_END) failed errno=%d", errno));
            return -1;
        }

        *recno = off / CUSTOMER_DISK_LEN;
    }
    else {
        /* UPDATE existing record */
        off = (off_t)(*recno) * CUSTOMER_DISK_LEN;
        if (lseek(customer_db->fd, off, SEEK_SET) == (off_t)-1) {
            debug_log((DEBUG_ERROR, FUNC_NAME, "lseek failed: Unable to update existing record  off=%ld errno=%d",
                      (long)off, errno));
            return -1;
        }
    }

    rc = write(customer_db->fd, line, CUSTOMER_DISK_LEN);
    if (rc != CUSTOMER_DISK_LEN) {
        debug_log((DEBUG_ERROR, FUNC_NAME,
                  "write failed: wanted=%d wrote=%ld errno=%d",
                  CUSTOMER_DISK_LEN, (long)rc, errno));
        return -1;
    }

    /* Ensure data reaches disk */
    fsync(customer_db->fd);

    debug_log((DEBUG_TRACE, FUNC_NAME, "Customer written recno=%ld", *recno));
    return 0;
}

int db_cs_load_page(long start_rec, register CustomerList *list, long *next_rec)
{
    customer_t c;
    long rec = start_rec;
    customer_list_rec_t *dst;

    debug_log((DEBUG_TRACE, FUNC_NAME, "Enter:"));
    list->count = 0;

    while (list->count < CUSTOMER_PAGE_SIZE) {

        if (db_cs_read(rec, &c) != 0)
            break;

        rec++;

        if (c.cs_status[0] == CUSTOMER_STATUS_DELETED)
            continue;

        dst = &list->slots[list->count++];

        strcpy(dst->cs_id,     c.cs_id);
        strcpy(dst->cs_name,   c.cs_name);
        strcpy(dst->cs_phone1, c.cs_phone1);
        strcpy(dst->cs_phone2, c.cs_phone2);
    }
    *next_rec = rec;
    return 0;
}

int db_cs_parse_line(const char *line, register customer_t *out)
{
    debug_log((DEBUG_TRACE, FUNC_NAME, "Enter:"));

    if (!line || !out)
        return -1;

    copy_record(out->cs_status,   line + CUSTOMER_STATUS_OFF,   CUSTOMER_STATUS_LEN);
    copy_record(out->cs_id,       line + CUSTOMER_ID_OFF,       CUSTOMER_ID_LEN);
    copy_record(out->cs_name,     line + CUSTOMER_NAME_OFF,     CUSTOMER_NAME_LEN);
    copy_record(out->cs_phone1,   line + CUSTOMER_PHONE1_OFF,   CUSTOMER_PHONE1_LEN);
    copy_record(out->cs_phone2,   line + CUSTOMER_PHONE2_OFF,   CUSTOMER_PHONE2_LEN);
    copy_record(out->cs_address1, line + CUSTOMER_ADDRESS1_OFF, CUSTOMER_ADDRESS1_LEN);
    copy_record(out->cs_address2, line + CUSTOMER_ADDRESS2_OFF, CUSTOMER_ADDRESS2_LEN);
    copy_record(out->cs_suburb,   line + CUSTOMER_SUBURB_OFF,   CUSTOMER_SUBURB_LEN);
    copy_record(out->cs_state,    line + CUSTOMER_STATE_OFF,    CUSTOMER_STATE_LEN);
    copy_record(out->cs_postcode, line + CUSTOMER_POSTCODE_OFF, CUSTOMER_POSTCODE_LEN);
    copy_record(out->cs_notes,    line + CUSTOMER_NOTES_OFF,    CUSTOMER_NOTES_LEN);

    return 0;
}


void db_cs_format_line(register const customer_t *in, char *line)
{
    debug_log((DEBUG_TRACE, FUNC_NAME, "Enter:"));
    if (!in || !line)
        return;

    memset(line, ' ', CUSTOMER_RECORD_LEN);

    /* separators */
    line[CUSTOMER_STATUS_OFF    + CUSTOMER_STATUS_LEN]    = FIELD_SEP;
    line[CUSTOMER_ID_OFF        + CUSTOMER_ID_LEN]        = FIELD_SEP;
    line[CUSTOMER_NAME_OFF      + CUSTOMER_NAME_LEN]      = FIELD_SEP;
    line[CUSTOMER_PHONE1_OFF    + CUSTOMER_PHONE1_LEN]    = FIELD_SEP;
    line[CUSTOMER_PHONE2_OFF    + CUSTOMER_PHONE2_LEN]    = FIELD_SEP;
    line[CUSTOMER_ADDRESS1_OFF  + CUSTOMER_ADDRESS1_LEN]  = FIELD_SEP;
    line[CUSTOMER_ADDRESS2_OFF  + CUSTOMER_ADDRESS2_LEN]  = FIELD_SEP;
    line[CUSTOMER_SUBURB_OFF    + CUSTOMER_SUBURB_LEN]    = FIELD_SEP;
    line[CUSTOMER_STATE_OFF     + CUSTOMER_STATE_LEN]     = FIELD_SEP;
    line[CUSTOMER_POSTCODE_OFF  + CUSTOMER_POSTCODE_LEN]  = FIELD_SEP;
    /* notes is last – no FIELD_SEP */

    /* Fields */
    strncpy(line + CUSTOMER_STATUS_OFF,
            in->cs_status,
            strnlen(in->cs_id, CUSTOMER_STATUS_LEN));

    strncpy(line + CUSTOMER_ID_OFF,
            in->cs_id,
            strnlen(in->cs_id, CUSTOMER_ID_LEN));

    strncpy(line + CUSTOMER_NAME_OFF,
            in->cs_name,
            strnlen(in->cs_name, CUSTOMER_NAME_LEN));

    strncpy(line + CUSTOMER_PHONE1_OFF,
            in->cs_phone1,
            strnlen(in->cs_phone1, CUSTOMER_PHONE1_LEN));

    strncpy(line + CUSTOMER_PHONE2_OFF,
            in->cs_phone2,
            strnlen(in->cs_phone2, CUSTOMER_PHONE2_LEN));

    strncpy(line + CUSTOMER_ADDRESS1_OFF,
            in->cs_address1,
            strnlen(in->cs_address1, CUSTOMER_ADDRESS1_LEN));

    strncpy(line + CUSTOMER_ADDRESS2_OFF,
            in->cs_address2,
            strnlen(in->cs_address2, CUSTOMER_ADDRESS2_LEN));

    strncpy(line + CUSTOMER_SUBURB_OFF,
            in->cs_suburb,
            strnlen(in->cs_suburb, CUSTOMER_SUBURB_LEN));

    strncpy(line + CUSTOMER_STATE_OFF,
            in->cs_state,
            strnlen(in->cs_state, CUSTOMER_STATE_LEN));

    strncpy(line + CUSTOMER_POSTCODE_OFF,
            in->cs_postcode,
            strnlen(in->cs_postcode, CUSTOMER_POSTCODE_LEN));

    strncpy(line + CUSTOMER_NOTES_OFF,
            in->cs_notes,
            strnlen(in->cs_notes, CUSTOMER_NOTES_LEN));


    line[CUSTOMER_RECORD_LEN] = '\n';
}

int db_cs_op_read(void)
{
    int rc = db_open(customer_db, 0);

    debug_log((DEBUG_INFO, FUNC_NAME, "Enter: "));

    if (rc < 0)
        return rc;
    /* Count open files */
    g_open_files++;
    if (g_open_files > g_peak_open_files)
        g_peak_open_files = g_open_files;
    debug_log((DEBUG_INFO, FUNC_NAME, "OPEN READ fd=%d total=%d peak=%d", customer_db->fd, g_open_files, g_peak_open_files));
    return rc;
}

int db_cs_op_write(void)
{
    int rc = db_open(customer_db, 0);

    debug_log((DEBUG_INFO, FUNC_NAME, "Enter: "));

    if (rc < 0)
        return rc;
    /* Count open files */
    g_open_files++;
    if (g_open_files > g_peak_open_files)
        g_peak_open_files = g_open_files;
    debug_log((DEBUG_INFO, FUNC_NAME, "OPEN WRITE fd=%d total=%d peak=%d", customer_db->fd, g_open_files, g_peak_open_files));
    return rc;
}

/* Close database */
int db_cs_cl_read(void)
{
    debug_log((DEBUG_INFO, FUNC_NAME, "Enter: "));
    if (db_close(customer_db) == 0) {
        g_open_files--;
        debug_log((DEBUG_INFO, FUNC_NAME, "CLOSE READ total=%d", g_open_files));
    }
}

int db_cs_cl_write(void)
{
    debug_log((DEBUG_INFO, FUNC_NAME, "Enter: "));
    if (db_close(customer_db) == 0) {
        g_open_files--;
        debug_log((DEBUG_INFO, FUNC_NAME, "CLOSE WRITE total=%d", g_open_files));
    }
    return 0;
}

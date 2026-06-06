/*
* All database writes must go through db_*_write()
* Records are fixed-length; the trailing '\n' is part of the on-disk format
* and is validated to detect corruption early.
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

int db_customer_lookup_display(int customer_fd, const char *customer_id, char *out, size_t outlen)
{
    debug_log(DEBUG_INFO, FUNC_NAME, "Enter:");
    customer_t st;
    long rec = 0;

    while (db_customer_read(customer_fd, rec, &st) == 0) {
        if (strcmp(st.customer_id, customer_id) == 0) {
            ui_rtrim(st.customer_name);
            ui_rtrim(st.customer_address1);
            snprintf(out, outlen, "%s: %s", st.customer_name, st.customer_address1);
            return 0;
        }

        rec++;
    }
    debug_log(DEBUG_ERROR, FUNC_NAME, "*** ERROR *** Customer Not Found");
    return -1;
}


int db_customer_read(int fd, long recno, customer_t *out)
{
    debug_log(DEBUG_TRACE, FUNC_NAME, "Enter: fd=%d recno=%ld out=%p", fd, recno, out);
    if (fd < 0 || recno < 0 || !out) {
        debug_log(DEBUG_ERROR, FUNC_NAME, "Invalid args: fd=%d recno=%ld out=%p", fd, recno, out);
        return -1;
    }
    char line[CUSTOMER_DISK_LEN];
    off_t off = (off_t)recno * CUSTOMER_DISK_LEN;
    debug_log(DEBUG_TRACE, FUNC_NAME, "offset=%ld (recno=%ld * %d)", (long)off, recno, CUSTOMER_DISK_LEN);

    if (lseek(fd, off, SEEK_SET) == (off_t)-1) {
        debug_log(DEBUG_WARN, FUNC_NAME, "Warning: lseek failed for customer read");
        debug_log(DEBUG_WARN, FUNC_NAME, "Warning: offset=%ld", (long)off);
        return CUSTOMER_DB_EOF;
    }

    ssize_t n = read(fd, line, CUSTOMER_DISK_LEN);
    if (n == 0) {
        debug_log(DEBUG_TRACE, FUNC_NAME, "EOF reached at recno=%ld offset=%ld", recno, (long)off);
        return CUSTOMER_DB_EOF;
    }
    if (n != CUSTOMER_DISK_LEN) {
        debug_log(DEBUG_ERROR, FUNC_NAME, "ERROR: Customer File Length (read %zd, expected %d)", n, CUSTOMER_DISK_LEN);
        return CUSTOMER_DB_ERROR;
    }
    return db_customer_parse_line(line, out);
}

int db_customer_read_by_id( int fd, const char *customer_id, customer_t *out, long *out_recno)
{
    debug_log(DEBUG_INFO, FUNC_NAME, "Enter:");
    customer_t tmp;
    long recno = 0;

    while (db_customer_read(fd, recno, &tmp) == 0) {
        if (strcmp(tmp.customer_id, customer_id) == 0) {
            *out = tmp;   /* struct copy */
            if (out_recno)
                *out_recno = recno;

            debug_log(
                DEBUG_INFO, FUNC_NAME, "Customer found id=%s recno=%ld", customer_id, recno);
            return 0;
        }
        recno++;
    }
    return -1;   /* not found */
}

int db_customer_generate_next_id(int fd, char *out_id)
{
    debug_log(DEBUG_TRACE, FUNC_NAME, "Enter:");
    customer_t tmp;
    long recno = 0;
    long max_id = 0;

    if (!out_id) {
        return -1;
    }

    while (db_customer_read(fd, recno, &tmp) == 0) {

        if (tmp.customer_id[0] != '\0') {
            char *endp;
            long id = strtol(tmp.customer_id, &endp, 10);

            /* accept only fully numeric IDs */
            if (*endp == '\0' && id > max_id) {
                max_id = id;
            }
        }

        recno++;
    }

    long next_id = max_id + 1;

    if (next_id >= ID_MAX_VALUE) {
        debug_log(DEBUG_ERROR, FUNC_NAME, "Customer ID space exhausted (max %d)", ID_LEN);
        return -1;
    }
    /* zero-padded fixed width */
    snprintf(out_id, ID_LEN + 1, "%06lu", (unsigned long)next_id);

    return 0;
}

long db_customer_record_count(int fd)
{
    debug_log(DEBUG_TRACE, FUNC_NAME, "Enter:");

    customer_t tmp;
    long recno = 0;
    long count = 0;

    if (fd < 0)
        return -1;

    while (db_customer_read(fd, recno, &tmp) == 0) {
        /* Skip deleted records */
        if (tmp.customer_status[0] == CUSTOMER_STATUS_DELETED) {
            recno++;
            continue;
        }
        /* Count active records */
        count++;
        recno++;
    }
    debug_log(DEBUG_INFO, FUNC_NAME, "Active customer count=%ld of %ld", count, MAX_CUSTOMER_ENTRIES);
    return count;
}


int db_customer_write(int fd, long *recno, const customer_t *in)
{
    debug_log(DEBUG_INFO, FUNC_NAME, "Enter: recno=%ld", recno ? *recno : -1);

    if (in->customer_id[0] == '\0') {
        debug_log(DEBUG_ERROR, FUNC_NAME, "Unable to write customer with empty ID ");
        return -1;
    }

    char line[CUSTOMER_DISK_LEN];
    off_t off;

    if (fd < 0 || !recno || !in) {
        debug_log(DEBUG_ERROR, FUNC_NAME, "Invalid arguments: fd=%d recno=%p in=%p", fd, recno, in);
        return -1;
    }

    /* Format record into on-disk layout */
    db_customer_format_line(in, line);

    /* NEW record: append */
    if (*recno < 0) {
        long count = db_customer_record_count(fd);
        if (count < 0) {
            debug_log(DEBUG_ERROR, FUNC_NAME, "Unable to determine customer count");
            return -1;
        }

        if (count >= MAX_CUSTOMER_ENTRIES) {
            debug_log(DEBUG_WARN, FUNC_NAME, "Customer limit reached (%ld)", count);
            errno = ENOSPC;   /* optional but nice */
            return -1;
        }

        off = lseek(fd, 0, SEEK_END);
        if (off == (off_t)-1) {
            debug_log(DEBUG_ERROR, FUNC_NAME, "lseek(SEEK_END) failed errno=%d", errno);
            return -1;
        }

        *recno = off / CUSTOMER_DISK_LEN;
    }
    else {
        /* UPDATE existing record */
        off = (off_t)(*recno) * CUSTOMER_DISK_LEN;
        if (lseek(fd, off, SEEK_SET) == (off_t)-1) {
            debug_log(DEBUG_ERROR, FUNC_NAME, "lseek failed: Unable to update existing record  off=%ld errno=%d",
                      (long)off, errno);
            return -1;
        }
    }

    ssize_t rc = write(fd, line, CUSTOMER_DISK_LEN);
    if (rc != CUSTOMER_DISK_LEN) {
        debug_log(DEBUG_ERROR, FUNC_NAME,
                  "write failed: wanted=%d wrote=%ld errno=%d",
                  CUSTOMER_DISK_LEN, (long)rc, errno);
        return -1;
    }

    /* Ensure data reaches disk */
    fsync(fd);

    debug_log(DEBUG_TRACE, FUNC_NAME, "Customer written recno=%ld", *recno);
    return 0;
}

int db_customer_load_page(int fd, long start_rec, CustomerList *list, long *next_rec)
{
    debug_log(DEBUG_TRACE, FUNC_NAME, "Enter:");
    customer_t c;
    long rec = start_rec;

    list->count = 0;

    while (list->count < CUSTOMER_PAGE_SIZE) {

        if (db_customer_read(fd, rec, &c) != 0)
            break;

        rec++;

        if (c.customer_status[0] == CUSTOMER_STATUS_DELETED)
            continue;

        customer_list_rec_t *dst = &list->slots[list->count++];

        strcpy(dst->customer_id,     c.customer_id);
        strcpy(dst->customer_name,   c.customer_name);
        strcpy(dst->customer_phone1, c.customer_phone1);
        strcpy(dst->customer_phone2, c.customer_phone2);
    }
    *next_rec = rec;
    return 0;
}

int db_customer_parse_line(const char *line, customer_t *out)
{
    debug_log(DEBUG_TRACE, FUNC_NAME, "Enter:");

    if (!line || !out)
        return -1;

    SAFE_COPY(out->customer_status,   line + CUSTOMER_STATUS_OFF,   CUSTOMER_STATUS_LEN);
    SAFE_COPY(out->customer_id,       line + CUSTOMER_ID_OFF,       CUSTOMER_ID_LEN);
    SAFE_COPY(out->customer_name,     line + CUSTOMER_NAME_OFF,     CUSTOMER_NAME_LEN);
    SAFE_COPY(out->customer_phone1,   line + CUSTOMER_PHONE1_OFF,   CUSTOMER_PHONE1_LEN);
    SAFE_COPY(out->customer_phone2,   line + CUSTOMER_PHONE2_OFF,   CUSTOMER_PHONE2_LEN);
    SAFE_COPY(out->customer_address1, line + CUSTOMER_ADDRESS1_OFF, CUSTOMER_ADDRESS1_LEN);
    SAFE_COPY(out->customer_address2, line + CUSTOMER_ADDRESS2_OFF, CUSTOMER_ADDRESS2_LEN);
    SAFE_COPY(out->customer_suburb,   line + CUSTOMER_SUBURB_OFF,   CUSTOMER_SUBURB_LEN);
    SAFE_COPY(out->customer_state,    line + CUSTOMER_STATE_OFF,    CUSTOMER_STATE_LEN);
    SAFE_COPY(out->customer_postcode, line + CUSTOMER_POSTCODE_OFF, CUSTOMER_POSTCODE_LEN);
    SAFE_COPY(out->customer_notes,    line + CUSTOMER_NOTES_OFF,    CUSTOMER_NOTES_LEN);

    return 0;
}


void db_customer_format_line(const customer_t *in, char *line)
{
    debug_log(DEBUG_TRACE, FUNC_NAME, "Enter:");
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
            in->customer_status,
            safe_strlen(in->customer_id, CUSTOMER_STATUS_LEN));

    strncpy(line + CUSTOMER_ID_OFF,
            in->customer_id,
            safe_strlen(in->customer_id, CUSTOMER_ID_LEN));

    strncpy(line + CUSTOMER_NAME_OFF,
            in->customer_name,
            safe_strlen(in->customer_name, CUSTOMER_NAME_LEN));

    strncpy(line + CUSTOMER_PHONE1_OFF,
            in->customer_phone1,
            safe_strlen(in->customer_phone1, CUSTOMER_PHONE1_LEN));

    strncpy(line + CUSTOMER_PHONE2_OFF,
            in->customer_phone2,
            safe_strlen(in->customer_phone2, CUSTOMER_PHONE2_LEN));

    strncpy(line + CUSTOMER_ADDRESS1_OFF,
            in->customer_address1,
            safe_strlen(in->customer_address1, CUSTOMER_ADDRESS1_LEN));

    strncpy(line + CUSTOMER_ADDRESS2_OFF,
            in->customer_address2,
            safe_strlen(in->customer_address2, CUSTOMER_ADDRESS2_LEN));

    strncpy(line + CUSTOMER_SUBURB_OFF,
            in->customer_suburb,
            safe_strlen(in->customer_suburb, CUSTOMER_SUBURB_LEN));

    strncpy(line + CUSTOMER_STATE_OFF,
            in->customer_state,
            safe_strlen(in->customer_state, CUSTOMER_STATE_LEN));

    strncpy(line + CUSTOMER_POSTCODE_OFF,
            in->customer_postcode,
            safe_strlen(in->customer_postcode, CUSTOMER_POSTCODE_LEN));

    strncpy(line + CUSTOMER_NOTES_OFF,
            in->customer_notes,
            safe_strlen(in->customer_notes, CUSTOMER_NOTES_LEN));


    line[CUSTOMER_RECORD_LEN] = '\n';
}

int db_customer_open_read(void)
{
    debug_log(DEBUG_INFO, FUNC_NAME, "Enter: ");

    int fd = open(CUSTOMER_DB_FILE, O_RDONLY);  /* Read Only */
    if (fd < 0) {
        debug_log(DEBUG_ERROR, FUNC_NAME, "Failed to open customer db in Read Only mode");
        return -1;
    }

    /* Check customer file */
    struct stat st;
    if (fstat(fd, &st) != 0) {
        debug_log(DEBUG_ERROR, FUNC_NAME, "Cannot stat customer database");
        close(fd);
        return -1;
    }

    if (db_validate_fixed_records(fd, CUSTOMER_DISK_LEN, CUSTOMER_DB_FILE) != 0) {
        debug_log(DEBUG_ERROR, FUNC_NAME, "Customer DB corrupt");
        close(fd);
        return -1;
    }

    /* Count open files */
    g_open_files++;
    if (g_open_files > g_peak_open_files)
        g_peak_open_files = g_open_files;

    debug_log(DEBUG_INFO, FUNC_NAME, "OPEN READ fd=%d total=%d peak=%d", fd, g_open_files, g_peak_open_files);
    return fd;
}

int db_customer_open_write(void)
{
    debug_log(DEBUG_INFO, FUNC_NAME, "Enter: ");

    /* Create Lock File */
    if (db_lock_exclusive(CUSTOMER_DB_FILE) != 0){
       debug_log(DEBUG_ERROR, FUNC_NAME, "Failed to create customer db lock file");
       return -1;
    }

    /* Open for Writing */
    int fd = open(CUSTOMER_DB_FILE, O_RDWR);  /* Read Write */
    if (fd < 0) {
        debug_log(DEBUG_ERROR, FUNC_NAME, "Failed to open customer");
        db_unlock(CUSTOMER_DB_FILE);
        return -1;
    }
    /* Check customer file */
    struct stat st;
    if (fstat(fd, &st) != 0) {
        debug_log(DEBUG_ERROR, FUNC_NAME, "Cannot stat customer database");
        close(fd);
        db_unlock(CUSTOMER_DB_FILE);
        return -1;
    }
    if (db_validate_fixed_records(fd, CUSTOMER_DISK_LEN, CUSTOMER_DB_FILE) != 0) {
        debug_log(DEBUG_ERROR, FUNC_NAME, "Customer database corrupt");
        close(fd);
        db_unlock(CUSTOMER_DB_FILE);
        return -1;
    }
    /* Count open files */
    g_open_files++;
    if (g_open_files > g_peak_open_files)
        g_peak_open_files = g_open_files;

    debug_log(DEBUG_INFO, FUNC_NAME, "OPEN WRITE fd=%d total=%d peak=%d", fd, g_open_files, g_peak_open_files);

    return fd;
}

/* Close database */
int db_customer_close_read(int fd)
{
    debug_log(DEBUG_INFO, FUNC_NAME, "Enter: ");
    if (fd < 0)
        return 0;
    if (close(fd) == 0) {
        g_open_files--;
        debug_log(DEBUG_INFO, FUNC_NAME, "CLOSE READ fd=%d total=%d", fd, g_open_files);
    }
    return 0;
}

int db_customer_close_write(int fd)
{
    debug_log(DEBUG_INFO, FUNC_NAME, "Enter: ");
    if (fd >= 0){
        if (close(fd) == 0) {
            g_open_files--;
            debug_log(DEBUG_INFO, FUNC_NAME, "CLOSE WRITE fd=%d total=%d", fd, g_open_files);
        }
    }
    db_unlock(CUSTOMER_DB_FILE);
    return 0;
}

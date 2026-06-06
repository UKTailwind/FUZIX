#include <ctype.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include "debug.h"
#include "date.h"
#include "ui.h"
#include "ui_common.h"
#include "booking_detail.h"
#include "db_common.h"
#include "db_booking.h"
#include "db_customer.h"
#include "db_state.h"
#include "db_staff.h"
#include "customer_list.h"
#include "state_select.h"
#include "staff_select.h"

/* -------------------- Internal Types -------------------- */
typedef struct {
    booking_t      orig;
    unsigned char  guard_before_work;
    booking_t      work;
    book_mode_t    mode;
    int            field;
    long           recno;  /* -1 for new records */

    /* UI Edit buffers (strings) */
    char edit_status[BOOKING_STATUS_MAX +1];
    char edit_date[UI_DATE_DD_MM_YYYY_LEN +1];
    char edit_start[UI_TIME_HHMM_COLON_LEN +1];
    char edit_end[UI_TIME_HHMM_COLON_LEN +1];
    char edit_customer_id[BOOKING_CUSTOMER_ID_MAX +1];
    char edit_staff_id[BOOKING_STAFF_ID_MAX +1];
    char edit_state_id[BOOKING_STATE_ID_MAX+1];
    char edit_job[BOOKING_JOB_MAX+1];

    /* Cached display values (to avoid DB lookups during draw) */
    char customer_display[64];
    char staff_display[64];
    char state_display[STATE_NAME_MAX +1];

} book_form_t;


typedef struct {
    const char *label;
    char *ptr;
    size_t maxlen;
    int row, col;
    int mode;
} ui_field_t;

/* -------------------- Field Definitions -------------------- */
#define LABEL_COL  1
#define VALUE_COL  20
#define FIRST_ROW  5
#define FIELD_WIDTH 60

#define Y_OFFSET 5
#define X_OFFSET 20

#ifdef DEBUG_ENABLED
static void debug_check_field_overlap(ui_field_t *fields, int count)
{
    debug_log(DEBUG_TRACE, FUNC_NAME, "Enter:");
    int i;
    for (i = 0; i < count - 1; i++) {
        char *end = fields[i].ptr + fields[i].maxlen;
        char *next = fields[i + 1].ptr;

        debug_log(DEBUG_TRACE, "field_check",
            "field %d end=%p next_start=%p gap=%ld",
            i, end, next, (long)(next - end));
    }
}
#endif

/* Field indexes for booking detail form (must match bind_fields() order) */
enum {
    FIELD_ID = 0,
    FIELD_DATE,
    FIELD_START,
    FIELD_END,
    FIELD_CUSTOMER,
    FIELD_STAFF,
    FIELD_STATE,
    FIELD_JOB,
    FIELD_COUNT
};

static ui_field_t fields[FIELD_COUNT];

static int lookup_customer_display(const char *id, char *out, size_t outlen)
{
    int fd = db_customer_open_read();
    if (fd < 0)
        return -1;

    int rc = db_customer_lookup_display(fd, id, out, outlen);

    db_customer_close_read(fd);
    return rc;
}

static int lookup_state_display(const char *id, char *out, size_t outlen)
{
    (void)outlen;   /* Unused */

    int fd = db_state_open();
    if (fd < 0)
        return -1;

    int rc = db_state_name_from_id(fd, id, out);

    db_state_close(fd);
    return rc;
}

static int lookup_staff_display(const char *id, char *out, size_t outlen)
{
    int fd = db_staff_open();
    if (fd < 0)
        return -1;

    int rc = db_staff_lookup_display(fd, id, out, outlen);

    db_staff_close(fd);
    return rc;
}

static void populate_display_cache(book_form_t *f)
{
    debug_log(DEBUG_TRACE, FUNC_NAME, "Enter: ");
    lookup_customer_display(f->edit_customer_id, f->customer_display, sizeof(f->customer_display));
    lookup_state_display(f->edit_state_id,       f->state_display,    sizeof(f->state_display));
    lookup_staff_display(f->edit_staff_id,       f->staff_display,    sizeof(f->staff_display));
}

static void bind_fields(book_form_t *f)
{
    memset(fields, 0, sizeof(fields));

    /* Field behaviour:
       UI_FIELD_SKIP   = not selectable
       UI_FIELD_EDIT   = selectable + editable
       UI_FIELD_SELECT = selectable popup field (F2 etc)
    */

    fields[FIELD_ID] = (ui_field_t){ "ID",                f->work.booking_id,  BOOKING_ID_MAX,          2, 15, UI_FIELD_SKIP };
    fields[FIELD_DATE] = (ui_field_t){ "Date",            f->edit_date,        BOOKING_DATE_MAX,        3, 15, UI_FIELD_EDIT };
    fields[FIELD_START] = (ui_field_t){ "Start Time",     f->edit_start,       BOOKING_START_TIME_MAX,  4, 15, UI_FIELD_EDIT };
    fields[FIELD_END] = (ui_field_t){ "End Time",         f->edit_end,         BOOKING_END_TIME_MAX,    5, 15, UI_FIELD_EDIT };
    fields[FIELD_CUSTOMER] = (ui_field_t){ "Customer",    f->edit_customer_id, BOOKING_CUSTOMER_ID_MAX, 6, 15, UI_FIELD_SELECT};
    fields[FIELD_STAFF] = (ui_field_t){ "Staff",          f->edit_staff_id,    BOOKING_STAFF_ID_MAX,    7, 15, UI_FIELD_SELECT};
    fields[FIELD_STATE] = (ui_field_t){ "State",          f->edit_state_id,    BOOKING_STATE_ID_MAX,    8, 15, UI_FIELD_SELECT};
    fields[FIELD_JOB] = (ui_field_t){ "Job",              f->edit_job,         BOOKING_JOB_MAX,         9, 15, UI_FIELD_EDIT };
#ifdef DEBUG_ENABLED
    debug_check_field_overlap(fields, FIELD_COUNT);
#endif
}

static void booking_work_to_edit(book_form_t *f)
{
    debug_log(DEBUG_INFO, FUNC_NAME, "Enter:");
    debug_log(DEBUG_INFO, FUNC_NAME, "work.booking_customer_id='%s'", f->work.booking_customer_id);

    f->edit_status[0] = f->work.booking_status[0];  /* copy actual status char */
    f->edit_status[1] = '\0';                       /* add null terminator safely */

    /* Date: YYYYMMDD -> DD/MM/YYYY */
    yyyymmdd_to_dd_mm_yyyy( f->work.booking_date, f->edit_date, sizeof(f->edit_date) );

    /* Start time: HHMM -> HH:MM */
    hhmm_to_hhmm_colon( f->work.booking_start_time, f->edit_start, sizeof(f->edit_start) );

    /* End time: HHMM -> HH:MM */
    hhmm_to_hhmm_colon( f->work.booking_end_time, f->edit_end, sizeof(f->edit_end) );

    /* Plain string copies */
    copy_record(f->edit_customer_id, f->work.booking_customer_id, BOOKING_CUSTOMER_ID_MAX);
    copy_record(f->edit_staff_id, f->work.booking_staff_id,       BOOKING_STAFF_ID_MAX);
    copy_record(f->edit_state_id,    f->work.booking_state_id,    BOOKING_STATE_ID_MAX);
    copy_record(f->edit_job,         f->work.booking_job,         BOOKING_JOB_MAX);

    debug_log(DEBUG_TRACE, FUNC_NAME,
        "MECH edit=[%s] len=%zu term=%02x",
        f->edit_staff_id,
        strlen(f->edit_staff_id),
        (unsigned char)f->edit_staff_id[BOOKING_STAFF_ID_MAX]);

    debug_log(DEBUG_TRACE, FUNC_NAME,
        "STATE edit=[%s] len=%zu term=%02x",
        f->edit_state_id,
        strlen(f->edit_state_id),
        (unsigned char)f->edit_state_id[BOOKING_STATE_ID_MAX]);

    debug_log(DEBUG_TRACE, FUNC_NAME,
        "JOB edit=[%s] len=%zu term=%02x",
        f->edit_job,
        strlen(f->edit_job),
        (unsigned char)f->edit_job[BOOKING_JOB_MAX]);
}

static void booking_edit_to_work(book_form_t *f)
{
    f->work.booking_status[0] = f->edit_status[0];

    /* Date: DD/MM/YYYY -> YYYYMMDD */
    dd_mm_yyyy_to_yyyymmdd(f->edit_date, f->work.booking_date, sizeof(f->work.booking_date));

    /* Start time: HH:MM -> HHMM */
    hhmm_colon_to_hhmm(f->edit_start, f->work.booking_start_time, sizeof(f->work.booking_start_time));

    /* End time: HH:MM -> HHMM */
    hhmm_colon_to_hhmm(f->edit_end, f->work.booking_end_time, sizeof(f->work.booking_end_time));

    /* Plain string copies */

    debug_log(DEBUG_INFO, FUNC_NAME, "edit_customer_id BEFORE copy = '%s'", f->edit_customer_id);
    copy_record(f->work.booking_customer_id, f->edit_customer_id,   BOOKING_CUSTOMER_ID_MAX);
    debug_log(DEBUG_INFO, FUNC_NAME, "work_customer_id AFTER copy = '%s'", f->work.booking_customer_id);

    copy_record(f->work.booking_staff_id,    f->edit_staff_id,      BOOKING_STAFF_ID_MAX);
    copy_record(f->work.booking_state_id,    f->edit_state_id,      BOOKING_STATE_ID_MAX);
    copy_record(f->work.booking_job,         f->edit_job,           BOOKING_JOB_MAX);
}

/* -------------------- Drawing -------------------- */

static int first_selectable_field(void)
{
    debug_log(DEBUG_TRACE, FUNC_NAME, "Enter:");
    int i;
    for (i = 0; i < FIELD_COUNT; i++) {
        if (fields[i].mode == UI_FIELD_EDIT)
            return i;
    }
    return 0;  /* fallback (should never happen)*/
}

static void draw_footer(const book_form_t *f)
{
    debug_log(DEBUG_TRACE, FUNC_NAME, "Enter:");

    /* Help line */
    if (f->mode == BOOK_VIEW) {
        ui_puts(UI_HELP_ROW, 1, "Esc=Back");
    } else {
        ui_puts(UI_HELP_ROW, 1, "Esc=Save/Exit,  Arrow Keys=Navigate, F2=Select, Shift Del=Delete Booking");
    }

    /* Command line */
    ui_puts(UI_COMMAND_ROW, 1,"Command:                                                    ");
    ui_puts(UI_COMMAND_ROW, 9,"");    /*Position the cursor*/

    /* Status line */
    switch (f->mode) {
    case BOOK_VIEW:
        ui_status("Viewing booking");
        break;
    case BOOK_EDIT:
        ui_status("Editing booking");
        break;
    case BOOK_CREATE:
        ui_status("Creating new booking");
        break;
    }
}

static void draw_form(const book_form_t *f)
{
    debug_log(DEBUG_TRACE, FUNC_NAME, "Enter:");
    char buf[128];

    ui_cls();
    ui_puts(1, 1, "Booking Details                                                    By D.Pollard");
/*
    ui_puts(4, 1, "---------1---------2---------3---------4---------5---------6---------7---------8");
*/
    ui_puts(4, 1, "--------------------------------------------------------------------------------");
    int i;
    for (i = 0; i < FIELD_COUNT; i++) {
        int row = FIRST_ROW + i;

        /* Label */
        snprintf(buf, sizeof(buf), "%-16s:", fields[i].label);
        ui_puts(row, LABEL_COL, buf);

        /* Value */
        const char *val;

        if (i == FIELD_STATE) {
           if (f->state_display[0] != '\0')
                val = f->state_display;
           else
                val = f->edit_state_id;
        }

        else if (i == FIELD_CUSTOMER) {
            if (f->customer_display[0] != '\0')
                val = f->customer_display;
            else
               val = f->edit_customer_id;
        }

        else if (i == FIELD_STAFF) {
            if (f->staff_display[0] != '\0')
                val = f->staff_display;
            else
                val = f->edit_staff_id;
        }

        else {
            val = fields[i].ptr;
        }

        if (i == f->field && f->mode != BOOK_VIEW && fields[i].mode != UI_FIELD_SKIP)
        {
            ui_attr_reverse_on();
            ui_puts_padded(row, VALUE_COL, val, FIELD_WIDTH);
            ui_attr_reverse_off();
        }
        else
        {
            ui_puts_padded(row, VALUE_COL, val, FIELD_WIDTH);
        }
    }
    draw_footer(f);
    fflush(stdout);
}

/* -------------------- Navigation -------------------- */

static void move_field(book_form_t *f, int delta)
{
    debug_log(DEBUG_TRACE, FUNC_NAME, "Enter:");
    int i = f->field;

    for (;;) {
        i += delta;
        if (i < 0 || i >= FIELD_COUNT)
            return;
        if (fields[i].mode != UI_FIELD_SKIP || f->mode == BOOK_VIEW)
        {
            f->field = i;
            return;
        }
    }
}

static int edit_current_field(book_form_t *form)
{
    debug_log(DEBUG_INFO, FUNC_NAME, "Enter:");

    ui_field_t *fld = &fields[form->field];
    if (fld->mode != UI_FIELD_EDIT)
        return ui_read_key();

    if (form->field < 0 || form->field >= FIELD_COUNT) {
        debug_log(DEBUG_ERROR, FUNC_NAME, "Field index out of range: %d", form->field);
        return 0;
    }

    debug_log(DEBUG_TRACE, FUNC_NAME, "Editing field %d: row=%d col=%d maxlen=%zu",
        form->field, fld->row, fld->col, fld->maxlen);

    edit_state_t es = {
        .buf         = fld->ptr,
        .maxlen      = fld->maxlen,
        .cursor_pos  = 0,
        .insert_mode = 1
    };

    debug_log(DEBUG_TRACE, FUNC_NAME, "BEFORE edit: field=%d buf=%p maxlen=%zu", form->field, es.buf, es.maxlen);

    dump_bytes(DEBUG_TRACE,"STATUS BEFORE EDIT", form->work.booking_status, sizeof(form->work.booking_status));

    int rc = ui_edit_field(&es, FIRST_ROW + form->field, VALUE_COL);
    ui_force_terminate(&es);

    debug_log(DEBUG_INFO, FUNC_NAME, "AFTER edit: field=%d buf=%p maxlen=%zu", form->field, es.buf, es.maxlen);

    /* ---- force termination ---- */
    es.buf[es.maxlen] = '\0';

    /* ---- dump NEXT field only if valid ---- */
    if (form->field + 1 < FIELD_COUNT) {
        dump_bytes(DEBUG_TRACE, "NEXT FIELD HEAD", fields[form->field + 1].ptr, 8);
    }

    /* ---- dump booking_status immediately after edit ---- */
    dump_bytes(DEBUG_TRACE, "STATUS MID-EXIT", form->work.booking_status,sizeof(form->work.booking_status));

    /* ---- sanity for date field ---- */
    if (fld->ptr == form->edit_date) {
        dump_bytes(DEBUG_TRACE, "edit_date AFTER edit", form->edit_date, UI_DATE_DD_MM_YYYY_LEN + 4);
    }

    return rc;
}

static int booking_detail_handle_exit(book_form_t *form, int fd)
{
    debug_log(DEBUG_TRACE, FUNC_NAME, "Enter:");
    debug_log(DEBUG_TRACE, FUNC_NAME, "Output 1: status='%c' (0x%02X)", form->work.booking_status[0],
        (unsigned char)form->work.booking_status[0]);

    dump_bytes(DEBUG_TRACE, "status AT ENTRY", form->work.booking_status, sizeof(form->work.booking_status) + 2);

    int changed = 0;

    char tmp_date[BOOKING_DATE_LEN +1];
    char tmp_start[BOOKING_START_TIME_LEN +1];
    char tmp_end[BOOKING_END_TIME_LEN +1];

    debug_log(DEBUG_TRACE, FUNC_NAME,
        "edit_date raw bytes: [%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X]",
        form->edit_date[0], form->edit_date[1], form->edit_date[2],
        form->edit_date[3], form->edit_date[4], form->edit_date[5],
        form->edit_date[6], form->edit_date[7], form->edit_date[8],
        form->edit_date[9], form->edit_date[10]);

    /* Convert UI buffers → DB format */
    dd_mm_yyyy_to_yyyymmdd(form->edit_date, tmp_date, sizeof(tmp_date));
    hhmm_colon_to_hhmm(form->edit_start, tmp_start, sizeof(tmp_start));
    hhmm_colon_to_hhmm(form->edit_end,   tmp_end,   sizeof(tmp_end));

    /* Sync UI edits into work buffer for change detection */
    booking_edit_to_work(form);

    debug_log(DEBUG_TRACE, FUNC_NAME,
        "Output 2: status='%c' (0x%02X)",
        form->work.booking_status[0],
        (unsigned char)form->work.booking_status[0]);

    if (form->work.booking_status[0] != form->orig.booking_status[0])
        changed = 1;
    if (strcmp(tmp_date, form->orig.booking_date) != 0)
        changed = 1;
    if (strcmp(tmp_start, form->orig.booking_start_time) != 0)
        changed = 1;
    if (strcmp(tmp_end, form->orig.booking_end_time) != 0)
        changed = 1;
    if (strncmp(form->work.booking_job, form->orig.booking_job, BOOKING_JOB_LEN) != 0)
        changed = 1;
    if (strncmp(form->work.booking_staff_id, form->orig.booking_staff_id, BOOKING_STAFF_ID_LEN) != 0)
        changed = 1;
    if (strncmp(form->work.booking_state_id, form->orig.booking_state_id, BOOKING_STATE_ID_LEN) != 0)
        changed = 1;
    if (strncmp(form->work.booking_customer_id, form->orig.booking_customer_id, BOOKING_CUSTOMER_ID_MAX) != 0)
        changed = 1;

    debug_log(DEBUG_TRACE, FUNC_NAME,
        "Output 3: status='%c' (0x%02X)",
        form->work.booking_status[0],
        (unsigned char)form->work.booking_status[0]);

    debug_log(DEBUG_TRACE, FUNC_NAME,
        "cmp: orig date='%s' tmp='%s' | orig start='%s' tmp='%s' | orig end='%s' tmp='%s'",
        form->orig.booking_date, tmp_date,
        form->orig.booking_start_time, tmp_start,
        form->orig.booking_end_time, tmp_end);

    if (!changed) {
        ui_status("No changes");
        sleep(1);
        return 0;
    }

    ui_status("Save Changes (Y/N)");
    ui_puts(UI_COMMAND_ROW, 1, "Command: ");

    debug_log(DEBUG_TRACE, FUNC_NAME, "Output 4: status='%c' (0x%02X)",
        form->work.booking_status[0],
        (unsigned char)form->work.booking_status[0]);

    for (;;) {
        int key = ui_read_key();

        switch (key) {
        case 'Y':
        case 'y':
            /* Open Booking DB Read Write mode */
            int write_fd = db_booking_open_write();
            if (write_fd <0){
                ui_status("Failed to Open Booking DB in RW mode");
                sleep(2);
                return -1;
            }

            /* Rebuild DB record from UI buffers */
            debug_log(DEBUG_TRACE, FUNC_NAME, "Output 5: status='%c' (0x%02X)", form->work.booking_status[0], (unsigned char)form->work.booking_status[0]);

            /* booking_date: DD/MM/YYYY → YYYYMMDD */
            dd_mm_yyyy_to_yyyymmdd(form->edit_date, form->work.booking_date, sizeof(form->work.booking_date));

            debug_log(DEBUG_TRACE, FUNC_NAME, "Output 6: status='%c' (0x%02X)", form->work.booking_status[0], (unsigned char)form->work.booking_status[0]);

            /* times: HH:MM → HHMM */
            hhmm_colon_to_hhmm(form->edit_start, form->work.booking_start_time, sizeof(form->work.booking_start_time));
            hhmm_colon_to_hhmm(form->edit_end, form->work.booking_end_time, sizeof(form->work.booking_end_time));

            booking_edit_to_work(form);

            dump_bytes(DEBUG_TRACE, "work booking_status+id", &form->work.booking_status, sizeof(form->work.booking_status) + sizeof(form->work.booking_id));
            dump_bytes(DEBUG_TRACE, "status BEFORE save", form->work.booking_state_id, 8);


            /* Write to DB */
            debug_log(DEBUG_TRACE, FUNC_NAME,  "writing record: status='%s' date='%s' start='%s' end='%s'",
               form->work.booking_status, form->work.booking_date, form->work.booking_start_time, form->work.booking_end_time);

            debug_log(DEBUG_TRACE, FUNC_NAME,
            "booking_t dump: status='%s' id='%s' start='%s' end='%s'",
                form->work.booking_status, form->work.booking_id, form->work.booking_start_time, form->work.booking_end_time);

            /* New Record generate ID*/
            if (form->recno == -1) {
                if (db_booking_generate_next_id(write_fd, form->work.booking_id) != 0) {
                    /* Unable to generate booking ID */
                    db_booking_close_write(write_fd);
                    ui_status("ERROR: Booking Not Saved, Unable to generate booking ID");
                     sleep(4);
                    return BOOK_DETAIL_ERROR;
                }

                if (db_booking_append(write_fd, &form->work) != 0) {
                    /* Unable to append new booking to file */
                    db_booking_close_write(write_fd);
                    ui_status("ERROR: Booking Not Saved, Unable to append new record to data base");
                    sleep(4);
                    return -1;
                }
           } else {
               /* Existing record: overwrite */
               if (db_booking_write(write_fd, form->recno, &form->work) != 0) {
                    /* Unable to  overwrite existing record */
                    db_booking_close_write(write_fd);
                    ui_status("ERROR: Booking Not Saved, Unable to overwrite existing record");
                    sleep(4);
                    return -1;
               }
           }
            db_booking_close_write(write_fd);
            ui_status("Booking saved");
            sleep(1);
            return BOOK_DETAIL_SAVED;

        case 'N':
        case 'n':
            db_booking_close_read(fd);
            ui_status("Changes discarded");
            return BOOK_DETAIL_NO_CHANGE;

        case UI_KEY_ESC:
            ui_status("Save cancelled");
            sleep(1);
            return -1;   /* back to editor */
            dump_bytes(DEBUG_TRACE, "work booking_status+id", &form->work.booking_status, sizeof(form->work.booking_status) + sizeof(form->work.booking_id));

        default:
            /* ignore everything else */
            break;
        }
    }
}

static void draw_single_field(const book_form_t *f, int i, int highlight)
{
    char buf[128];
    int row = FIRST_ROW + i;

    /* Label */
    snprintf(buf, sizeof(buf), "%-16s:", fields[i].label);
    ui_puts(row, LABEL_COL, buf);

    /* Value selection (same logic as draw_form) */
    const char *val;

    if (i == FIELD_STATE) {
        val = (f->state_display[0] != '\0') ? f->state_display : f->edit_state_id;
    }
    else if (i == FIELD_CUSTOMER) {
        val = (f->customer_display[0] != '\0') ? f->customer_display : f->edit_customer_id;
    }
    else if (i == FIELD_STAFF) {
        val = (f->staff_display[0] != '\0') ? f->staff_display : f->edit_staff_id;
    }
    else {
        val = fields[i].ptr;
    }

    /* Draw */
    if (highlight && f->mode != BOOK_VIEW && fields[i].mode != UI_FIELD_SKIP) {
        ui_attr_reverse_on();
        ui_puts_padded(row, VALUE_COL, val, FIELD_WIDTH);
        ui_attr_reverse_off();
    } else {
        ui_puts_padded(row, VALUE_COL, val, FIELD_WIDTH);
    }
}

static void update_field_highlight(const book_form_t *f, int prev)
{
    if (prev == f->field)
        return;

    draw_single_field(f, prev, 0);          /* old → normal */
    draw_single_field(f, f->field, 1);      /* new → reverse */

    fflush(stdout);
}

/* -------------------- Entry Point -------------------- */
int run_booking_detail(int booking_fd, const char *booking_id, book_mode_t mode)
{
    debug_log(DEBUG_INFO, FUNC_NAME, "Enter:");
    book_form_t form;
    booking_t   book;
    long        recno = -1;

    memset(&form, 0, sizeof(form));
    form.guard_before_work = 0xAA;   /* <-- Debug Code */
    memset(&book, 0, sizeof(book));
    int fd = booking_fd;  /* File descriptor passed in from booking_list. */

    /* ---------------- Load / Create booking ---------------- */
    switch (mode) {

    case BOOK_VIEW:
    case BOOK_EDIT:
        if (!booking_id) {
            debug_log(DEBUG_ERROR, FUNC_NAME, "*** ERROR: *** NULL booking_id in VIEW/EDIT mode");
            ui_status("ERROR: NULL booking_id in VIEW/EDIT mode");
            sleep(4);
            return -1;
        }

        if (db_booking_read_by_id(fd, booking_id, &book, &recno) != 0) {
            db_booking_close_read(fd);
            debug_log(DEBUG_ERROR, FUNC_NAME, "*** Error: *** Booking Not Found");
            ui_status("ERROR: Booking not found");
            sleep(4);
            return -1;
        }
        debug_log(DEBUG_TRACE, FUNC_NAME, "AFTER READ: status='%c' (0x%02X)", form.orig.booking_status[0], (unsigned char)form.orig.booking_status[0]);
        debug_log(DEBUG_TRACE, FUNC_NAME, "sizeof booking_status=%zu booking_id=%zu", sizeof(form.orig.booking_status), sizeof(form.orig.booking_id));
        dump_bytes(DEBUG_TRACE, "orig booking_status+id", (unsigned char *)form.orig.booking_status, sizeof(form.orig.booking_status) + sizeof(form.orig.booking_id) + 1);
        break;

    case BOOK_CREATE:
        book.booking_status[0] = BOOKING_STATUS_ACTIVE;
        book.booking_status[1] = '\0';
        break;
    }

    /* ---------------- Initialise form ---------------- */

    form.work  = book;

    /* DEBUG TESTING Preserve booking status explicitly */
    form.work.booking_status[0] = book.booking_status[0];
    form.work.booking_status[1] = '\0';
    debug_log(DEBUG_TRACE, FUNC_NAME, "INIT status after read = '%c' (0x%02X)", form.work.booking_status[0], (unsigned char)form.work.booking_status[0]);
    /*==================================================*/

    form.orig  = book;
    form.recno = recno;
    form.mode  = mode;

    if (mode == BOOK_CREATE) {
        date_today_str(form.edit_date);
        strcpy(form.edit_start, "0800");
        strcpy(form.edit_end,   "1000");
    }

    booking_work_to_edit(&form);
    bind_fields(&form);

    debug_log(DEBUG_TRACE, FUNC_NAME, "ADDR work.booking_status = %p", form.work.booking_status);
    debug_log(DEBUG_TRACE, FUNC_NAME, "ADDR edit_status        = %p",  form.edit_status);
    debug_log(DEBUG_TRACE, FUNC_NAME, "ADDR edit_date          = %p",  form.edit_date);
    debug_log(DEBUG_TRACE, FUNC_NAME, "ADDR edit_start         = %p",  form.edit_start);
    debug_log(DEBUG_TRACE, FUNC_NAME, "ADDR edit_end           = %p",  form.edit_end);
    debug_log(DEBUG_TRACE, FUNC_NAME, "ADDR edit_staff_id   = %p",     form.edit_staff_id);
    debug_log(DEBUG_TRACE, FUNC_NAME, "ADDR edit_state_id      = %p",  form.edit_state_id);
    debug_log(DEBUG_TRACE, FUNC_NAME, "ADDR edit_job           = %p",  form.edit_job);

    int i;
    for (i = 0; i < FIELD_COUNT; i++) {
        debug_log(DEBUG_TRACE, FUNC_NAME,
            "After call to bind_fields :- fields[%d]: ptr=%p maxlen=%zu editable=%d",
            i, fields[i].ptr, fields[i].maxlen, fields[i].mode);
    }

    /* Set first selectable field */
    form.field = first_selectable_field();
    populate_display_cache(&form);
    draw_form(&form);

    /* ---------------- Main loop ---------------- */

    for (;;) {

        /*draw_form(&form); */

        /* ---------- EDIT / CREATE ---------- */
        if (mode == BOOK_EDIT || mode == BOOK_CREATE) {

            int key = edit_current_field(&form);
            debug_log(DEBUG_INFO, FUNC_NAME, "Key read = 0x%04X", key);
            debug_log(DEBUG_TRACE, FUNC_NAME, "GUARD before work = 0x%02X", form.guard_before_work);

            switch (key) {
                case UI_KEY_UP:
                case UI_KEY_SHIFT_TAB: {
                    int prev = form.field;
                    move_field(&form, -1);
                    update_field_highlight(&form, prev);
                    break;
                }

                case UI_KEY_DOWN:
                case UI_KEY_TAB:
                case UI_KEY_ENTER: {
                    int prev = form.field;
                    move_field(&form, +1);
                    update_field_highlight(&form, prev);
                    break;
                }

                case UI_KEY_SHIFT_DELETE:
                    if (mode == BOOK_EDIT) {
                        form.work.booking_status[0] = BOOKING_STATUS_DELETED;
                        form.work.booking_status[1] = '\0';
                        ui_status("Booking marked as deleted");
                        sleep(1);
                    }
                    break;

                case UI_KEY_F2:
                    /* State Field Popup */
                    if (form.mode != BOOK_VIEW && form.field == FIELD_STATE) {
                        state_select(form.edit_state_id);  /* handles updating edit & work buffers */
                        lookup_state_display(form.edit_state_id, form.state_display, sizeof(form.state_display));
                        draw_form(&form);
                    }
                    /* Customer Lookup */
                    else if (form.mode != BOOK_VIEW && form.field == FIELD_CUSTOMER) {
                        char selected_id[BOOKING_CUSTOMER_ID_MAX +1];
                        if (run_customer_list(CUSTOMER_MODE_SELECT, form.edit_customer_id, selected_id) == 0) {
                            copy_record(form.edit_customer_id, selected_id, BOOKING_CUSTOMER_ID_MAX);
                            lookup_customer_display(form.edit_customer_id, form.customer_display, sizeof(form.customer_display));
                        }
                        draw_form(&form);
                    }
                    /* Staff Field Popup */
                    else if (form.mode != BOOK_VIEW && form.field == FIELD_STAFF) {
                        staff_select(form.edit_staff_id);  /* handles updating edit & work buffers */
                        lookup_staff_display(form.edit_staff_id, form.staff_display, sizeof(form.staff_display));
                        draw_form(&form);
                    }
                    break;

                case UI_KEY_ESC:
                    return booking_detail_handle_exit(&form, fd);
            }
            continue;
        }

        /* ---------- VIEW ---------- */
        {
            int key = ui_read_key();
            if (key == UI_KEY_ESC) {
                return 0;
            }
        }
    }
}



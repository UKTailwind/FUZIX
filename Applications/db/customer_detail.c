#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include "debug.h"
#include "ui.h"
#include "ui_common.h"
#include "customer_detail.h"
#include "db_customer.h"
#include "db_lock.h"

/* -------------------- Internal Types -------------------- */
typedef struct {
    customer_t orig;
    customer_t work;
    cust_mode_t mode;
    int field;
    long recno;      /* -1 for new records */
} cust_form_t;

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
#define FIELD_COUNT 10
#define FIRST_ROW  5
#define FIELD_WIDTH 60
#define Y_OFFSET 5
#define X_OFFSET 20

static ui_field_t fields[FIELD_COUNT];

static void bind_fields(cust_form_t *f)
{
    fields[0] = (ui_field_t){ "ID",        f->work.customer_id,       CUSTOMER_ID_MAX,        2, 15, 0 };
    fields[1] = (ui_field_t){ "Name",      f->work.customer_name,     CUSTOMER_NAME_MAX,      3, 15, 1 };
    fields[2] = (ui_field_t){ "Phone 1",   f->work.customer_phone1,   CUSTOMER_PHONE1_MAX,    4, 15, 1 };
    fields[3] = (ui_field_t){ "Phone 2",   f->work.customer_phone2,   CUSTOMER_PHONE2_MAX,    5, 15, 1 };
    fields[4] = (ui_field_t){ "Address 1", f->work.customer_address1, CUSTOMER_ADDRESS1_MAX,  6, 15, 1 };
    fields[5] = (ui_field_t){ "Address 2", f->work.customer_address2, CUSTOMER_ADDRESS2_MAX,  7, 15, 1 };
    fields[6] = (ui_field_t){ "Suburb",    f->work.customer_suburb,   CUSTOMER_SUBURB_MAX,    8, 15, 1 };
    fields[7] = (ui_field_t){ "State",     f->work.customer_state,    CUSTOMER_STATE_MAX,     9, 15, 1 };
    fields[8] = (ui_field_t){ "Postcode",  f->work.customer_postcode, CUSTOMER_POSTCODE_MAX, 10, 15, 1 };
    fields[9] = (ui_field_t){ "Notes",     f->work.customer_notes,    CUSTOMER_NOTES_MAX,    12, 15, 1 };
}

/* -------------------- Drawing -------------------- */

static int first_selectable_field(void)
{
    debug_log(DEBUG_TRACE, FUNC_NAME, "Enter:");

   int i;
    for (i = 0; i < FIELD_COUNT; i++) {
        if (fields[i].mode)
            return i;
    }
    debug_log(DEBUG_ERROR, FUNC_NAME, "Fallback: Should Never Happen");
    return 0;  /* fallback (should never happen) */
}

static void draw_footer(const cust_form_t *f)
{
    debug_log(DEBUG_TRACE, FUNC_NAME, "Enter:");
    /* Help line */
    if (f->mode == CUST_VIEW) {
        ui_puts(UI_HELP_ROW, 1, "Esc=Back");
    } else {
        ui_puts(UI_HELP_ROW, 1, "Esc=Save/Exit,  Arrow Keys= Navigate, Shift Del= Delete Customer");
    }

    /* Command line */
    ui_puts(UI_COMMAND_ROW, 1,"Command:                                                    ");
    ui_puts(UI_COMMAND_ROW, 9,"");    /* Position the cursor */

    /* Status line */
    switch (f->mode) {
    case CUST_VIEW:
        ui_status("Viewing customer");
        break;
    case CUST_EDIT:
        ui_status("Editing customer");
        break;
    case CUST_CREATE:
        ui_status("Creating new customer");
        break;
    }
}

static void draw_form(const cust_form_t *f)
{
    debug_log(DEBUG_TRACE, FUNC_NAME, "Enter:");
    char  buf[128];

    ui_cls();
    ui_puts(1, 1, " Mechanic Workshop Customer Details                                By D.Pollard");
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
        const char *val = fields[i].ptr;
        debug_log(DEBUG_INFO, FUNC_NAME, "field=%d len=%d width=%d val='%s'", i, strlen(val), FIELD_WIDTH, val);

        if (i == f->field && f->mode != CUST_VIEW && fields[i].mode) {
            ui_attr_reverse_on();
            ui_puts_padded(row, VALUE_COL, val, FIELD_WIDTH);
            ui_attr_reverse_off();
        } else {
            ui_puts_padded(row, VALUE_COL, val, FIELD_WIDTH);
        }
    }
    draw_footer(f);
    fflush(stdout);
}

/* -------------------- Navigation -------------------- */

static void move_field(cust_form_t *f, int delta)
{
    debug_log(DEBUG_TRACE, FUNC_NAME, "Enter:");
    int i = f->field;

    for (;;) {
        i += delta;
        if (i < 0 || i >= FIELD_COUNT)
            return;

        if (fields[i].mode || f->mode == CUST_VIEW) {
            f->field = i;
            return;
        }
    }
}

static int edit_current_field(cust_form_t *form)
{
    ui_field_t *fld = &fields[form->field];
    debug_log(DEBUG_INFO, FUNC_NAME, "Editing field %d: row=%d, col=%d, maxlen=%zu", form->field, fld->row, fld->col, fld->maxlen);

    edit_state_t es = {
        .buf        = fld->ptr,
        .maxlen     = fld->maxlen,
        .cursor_pos = 0,
        .insert_mode = 1
    };

    return ui_edit_field(&es, FIRST_ROW + form->field, VALUE_COL);
}

static int customer_detail_handle_exit(cust_form_t *form, int fd)
{
    debug_log(DEBUG_INFO, FUNC_NAME, "Enter:");

    /* No changes → just exit */
    if (memcmp(&form->orig, &form->work, sizeof(customer_t)) == 0) {
        ui_status("No changes");
        sleep(1);

        /* Close Read Only File */
        db_customer_close_read(fd);
        return 0;
    }

    ui_status("Save Changes (Y/N)");
    ui_puts(UI_COMMAND_ROW, 1, "Command: ");

    for (;;) {
        int key = ui_read_key();

        switch (key) {
        case 'Y':
        case 'y':
            /* Close Customer DB Read Only mode */
            db_customer_close_read(fd);

            /* Open Customer DB Read Write mode */
            fd = db_customer_open_write();
            if (fd <0){
                /* Failed to open Customer DB in RW mode */
                ui_status("ERROR: Customer Not Saved, Unable to open Customer File in RW mode");
                sleep(2);
                return -1;
            }
            /* Assign ID for new customer */
            if (form->recno < 0) {
                if (db_customer_generate_next_id(fd, form->work.customer_id) != 0) {
                    /* unable to generate new customer id */
                    db_customer_close_write(fd);
                    ui_status("ERROR: Customer Not Saved, Unable to generate new customer ID");
                    sleep(2);
                    return -1;
                }
            }
            /* Save the Customer */
            if (db_customer_write(fd, &form->recno, &form->work) != 0) {
                /* Unable to create customer */
                db_customer_close_write(fd);
                ui_status("ERROR: Customer Not Saved, Unable to save customer record");
                sleep(2);
                return -1;   /* stay in editor */
            }
            db_customer_close_write(fd);
            ui_status("Customer saved");
            sleep(1);
            return CUST_DETAIL_SAVED;

        case 'N':
        case 'n':
            db_customer_close_read(fd);
            ui_status("Changes discarded");
            return CUST_DETAIL_NO_CHANGE;

        case UI_KEY_ESC:
            ui_status("Save cancelled");
            sleep(1);
            return -1;   /* back to editor */

        default:
            /* ignore everything else */
            break;
        }
    }
}

static void draw_single_field(const cust_form_t *f, int i, int highlight)
{
    char buf[128];
    int row = FIRST_ROW + i;

    /* Label */
    snprintf(buf, sizeof(buf), "%-16s:", fields[i].label);
    ui_puts(row, LABEL_COL, buf);

    /* Value selection (same logic as draw_form) */
    const char *val;

    val = fields[i].ptr;

    /* Draw */
    if (highlight && f->mode != CUST_VIEW && fields[i].mode != UI_FIELD_SKIP) {
        ui_attr_reverse_on();
        ui_puts_padded(row, VALUE_COL, val, FIELD_WIDTH);
        ui_attr_reverse_off();
    } else {
        ui_puts_padded(row, VALUE_COL, val, FIELD_WIDTH);
    }
}

static void update_field_highlight(const cust_form_t *f, int prev)
{
    if (prev == f->field)
        return;

    draw_single_field(f, prev, 0);          /* old → normal */
    draw_single_field(f, f->field, 1);      /* new → reverse */

    fflush(stdout);
}

/* -------------------- Entry Point -------------------- */

int run_customer_detail(const char *customer_id, cust_mode_t mode)
{
    debug_log(DEBUG_INFO, FUNC_NAME, "Enter:");
    cust_form_t form;
    memset(&form, 0, sizeof(form));

    int fd = db_customer_open_read();
    customer_t cust;

    memset(&cust, 0, sizeof(cust));

    if (fd < 0) {
        debug_log(DEBUG_ERROR, FUNC_NAME, "Error: Unalbe to open Cuatomer DB");
        ui_status("ERROR: Unable to open customer DB");
        sleep(4);
        return -1;
    }

    long recno = -1;

    /* ---- Load customer only for EDIT / VIEW ---- */
    if (mode == CUST_EDIT || mode == CUST_VIEW) {

        if (!customer_id) {
            debug_log(DEBUG_ERROR, FUNC_NAME, "NULL customer_id in EDIT/VIEW mode");
            return -1;
        }

        if (db_customer_read_by_id(fd, customer_id, &cust, &recno) != 0) {
            db_customer_close_read(fd);
            debug_log(DEBUG_ERROR, FUNC_NAME, "Error: Customer Not Found");
            ui_status("ERROR: Customer not found");
            sleep(2);
            return -1;
        }

    }
    /* ---- CREATE mode ---- */
    else if (mode == CUST_CREATE)
    {
        debug_log(DEBUG_INFO, FUNC_NAME, "Create new customer");

        memset(&cust, 0, sizeof(cust));
        cust.customer_id[0] = '\0';     /* not assigned yet */
        cust.customer_status[0] = CUSTOMER_STATUS_ACTIVE;
        cust.customer_status[1] = '\0';
        recno = -1;                     /* no record number yet */
    }
    form.orig = cust;
    form.work = cust;
    form.recno = recno;
    form.mode = mode;

    /* Copy loaded customer into form */
    memcpy(&form.work, &cust, sizeof(customer_t));

    /* Keep original for cancel/revert logic later */
    form.orig = form.work;

    bind_fields(&form);

    /* Set first selectable field */
    form.field = first_selectable_field();
    bind_fields(&form);
    draw_form(&form);

    /* -------------- Main loop  -------------------*/

    for (;;)
    {
       /* ---------- EDIT / CREATE -------  */
       if (form.mode == CUST_EDIT || form.mode == CUST_CREATE)
        {
            debug_log(DEBUG_TRACE, FUNC_NAME, "CUST_EDIT=%i CUST_CREATE=%i", CUST_EDIT, CUST_CREATE);
            int key = edit_current_field(&form);

            switch (key)
            {
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

                case UI_KEY_ESC:
                    return customer_detail_handle_exit(&form, fd);

                case UI_KEY_SHIFT_DELETE:
                    debug_log(DEBUG_INFO, FUNC_NAME, "Shift Delete Detected");
                    form.work.customer_status[0] = CUSTOMER_STATUS_DELETED;
                    form.work.customer_status[1] = '\0';

                    ui_status("Customer marked as deleted");
                    sleep(1);
                    break;
            }
        continue;  /* redraw + re-edit next field */
        }

        /* VIEW MODE */
        int key = ui_read_key();

        switch (key) {
            case UI_KEY_ESC:
                return 0;
        }
    }
}

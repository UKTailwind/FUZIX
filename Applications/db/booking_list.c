#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include "db_common.h"
#include "booking_list.h"
#include "booking_detail.h"
#include "db_booking.h"
#include "db_state.h"
#include "db_customer.h"
#include "ui.h"
#include "ui_common.h"
#include "debug.h"
#include "date.h"

#define MAX_BOOKING_ROWS 16

typedef struct {
    int goto_active;
    char goto_text[16];   /* "DD/MM/YYYY" */
    int goto_pos;
    int invalid_date;
} booking_input_state_t;

/* Internal helpers forward declaration */
static int state_lookup_name(const char *state_id, char *out, size_t outlen);
static int customer_lookup_name(const char *customer_id, char *out, size_t outlen);
static void draw_booking_row(const DayBookings *day, int screen_row, int idx, int highlight);

/* Selection */
static int selected_idx = 0;   /* index into day->slots[] */

/* ---------- Booking Screen drawing with dynamic scrolling ---------- */
static void draw_screen(const char *date, const DayBookings *day,
                        int start_idx, int selected_idx)
{
    char line[100];
    int i, row = 6;
    int max_rows = MAX_BOOKING_ROWS;
    int yyyymmdd = atoi(date);
    char date_disp[11];

    debug_log((DEBUG_INFO, FUNC_NAME, "Enter:"));
    ui_cls();
    ui_puts(1, 1, "Booking Management System                                             By D.Pollard");
    yyyymmdd_to_dd_mm_yyyy(date, date_disp, sizeof(date_disp));

    snprintf(line, sizeof(line),
        "Date: %s (%s)",
        date_disp,
        day_name(yyyymmdd));

    ui_puts(2, 1, line);
    ui_puts(4, 1, "Start End    State            Customer Name        Job");
/*
    ui_puts(5, 1, "---------1---------2---------3---------4---------5---------6---------7---------8");
*/
    ui_puts(5, 1, "--------------------------------------------------------------------------------");

    for (i = 0; i < max_rows && start_idx + i < day->count; i++) {
        int idx = start_idx + i;
        draw_booking_row(day, row + i, idx, (idx == selected_idx));
    }
    ui_puts(22, 1,  "->Next Day <-Prev Day, Shift-> Next Week, C=Create E=Edit G=Goto Date Esc=Quit");
    ui_puts(UI_COMMAND_ROW, 1, "Command: ");
    fflush(stdout);
}

static void draw_booking_row(const DayBookings *day,
                             int screen_row,
                             int idx,
                             int highlight)
{
    booking_t s;
    char startbuf[6];
    char endbuf[6];
    char line[100];
    char state_name[STATE_NAME_MAX +1];
    char customer_name[CUSTOMER_NAME_MAX + 1];

    debug_log((DEBUG_TRACE, FUNC_NAME, "Enter:"));
    if (idx < 0 || idx >= day->count)
        return;

    if (db_bk_by_index(day, idx, &s) != 0)
        return;

    hhmm_to_hhmm_colon(s.booking_start_time, startbuf, sizeof(startbuf));
    hhmm_to_hhmm_colon(s.booking_end_time,   endbuf,   sizeof(endbuf));


    if (state_lookup_name(s.booking_state_id, state_name, sizeof(state_name)) != 0) {
        strncpy(state_name, s.booking_state_id, sizeof(state_name) - 1);
        state_name[sizeof(state_name) - 1] = '\0';
    }

    if (customer_lookup_name(s.booking_customer_id, customer_name, sizeof(customer_name)) != 0) {
        strncpy(customer_name, s.booking_customer_id, sizeof(customer_name) - 1);
        customer_name[sizeof(customer_name) - 1] = '\0';
    }


    if (highlight)
        ui_attr_rv_on();

    sprintf(line,
        "%5s %5s  %-16.16s %-20.20s %-20.20s",
        startbuf,
        endbuf,
        state_name,
        customer_name,
        s.booking_job);

    ui_puts(screen_row, 1, line);

    if (highlight)
        ui_attr_rv_off();
}

/* Select a new date for booking */
static void goto_date(int new_date, int *current_date,
                      char *datestr, DayBookings *day, int *start_idx)
{
    debug_log((DEBUG_TRACE, FUNC_NAME, "Enter:"));
    *start_idx = 0;
    selected_idx = 0;
    *current_date = new_date;
    snprintf(datestr, 9, "%08u", (unsigned)new_date);

    /* Build index instead of loading full records */
    if (db_bk_build_day_index(new_date, day) < 0) {
        debug_log((DEBUG_WARN, FUNC_NAME, "No bookings for %08u", (unsigned)new_date));
        day->count = 0;
    }

    *start_idx = 0;
}

static int days_in_month(int month, int year)
{
    static const int days[] =
    {
        31,28,31,30,31,30,
        31,31,30,31,30,31
    };

    debug_log((DEBUG_INFO, FUNC_NAME, "Enter:"));

    if (month == 2)
    {
        /* Leap year */
        if ((year % 4 == 0 && year % 100 != 0)
            || (year % 400 == 0))
        {
            return 29;
        }
    }

    return days[month - 1];
}

static int parse_ddmmyyyy(const char *str, int *out_date)
{

    int day, month, year;
    int maxday;

    debug_log((DEBUG_INFO, FUNC_NAME, "Enter:"));

    /* Basic format validation */
    if (strlen(str) != 10)
        return -1;

    if (str[2] != '/' || str[5] != '/')
        return -1;

    if (sscanf(str, "%2d/%2d/%4d",
               &day, &month, &year) != 3)
    {
        return -1;
    }

    /* Basic range checks */
    if (year < 2000 || year > 2099)
        return -1;

    if (month < 1 || month > 12)
        return -1;

    maxday = days_in_month(month, year);

    if (day < 1 || day > maxday)
        return -1;

    *out_date = (year * 10000)
              + (month * 100)
              + day;

    return 0;
}

static int state_lookup_name(const char *state_id, char *out, size_t outlen)
{
    state_t st;
    long rec = 0;

    debug_log((DEBUG_INFO, FUNC_NAME, "Enter:"));

    while (db_state_read(rec, &st) == 0) {
        if (strcmp(st.state_id, state_id) == 0) {
            strncpy(out, st.name, outlen - 1);
            out[outlen - 1] = '\0';
            return 0;
        }
        rec++;
    }
    return -1;
}

static int customer_lookup_name(const char *customer_id, char *out, size_t outlen)
{
    customer_t st;
    long rec = 0;

    debug_log((DEBUG_INFO, FUNC_NAME, "Enter:"));

    while (db_cs_read(rec, &st) == 0) {
        if (strcmp(st.cs_id, customer_id) == 0) {
            strncpy(out, st.cs_name, outlen - 1);
            out[outlen - 1] = '\0';
            return 0;
        }
        rec++;
    }
    return -1;
}

/*================ Booking screen main loop ======================*/
void run_booking_list(void)
{
    char datestr[9];
    int current_date;
    static DayBookings day;

    int start_idx = 0;

    int running = 1;
    int selection_moved = 0;

    int rc;

    selected_idx = 0;
    debug_log((DEBUG_INFO, FUNC_NAME, "Enter:"));

    rc = db_open(booking_db, 0);

    if (rc < 0) {
        debug_log((DEBUG_ERROR, FUNC_NAME, "Unable to open Booking database"));
        ui_status("Unable to open booking database");
        sleep(2);
        goto cleanup;
    }

    rc =  db_open(state_db, 0);
    if (rc < 0) {
        debug_log((DEBUG_ERROR, FUNC_NAME, "Unable to open State database"));
        ui_status("Unable to open state database");
        sleep(2);
        goto cleanup;
    }

    rc =  db_open(customer_db, 0);
    if (rc < 0) {
        debug_log((DEBUG_ERROR, FUNC_NAME, "Unable to open Customer database"));
        ui_status("Unable to open customer database");
        sleep(2);
        goto cleanup;
    }

    date_today_str(datestr);
    current_date = atoi(datestr);
    goto_date(current_date, &current_date, datestr, &day, &start_idx);

    running = 1;

    draw_screen(datestr, &day, start_idx, selected_idx);

    while (running) {
        int prev_selected_idx = selected_idx;
        int prev_start_idx = start_idx;
        int page_changed = 0;
        char status[80];
        int ch;

        selection_moved = 0;

        /*Status bar (depends on what is visible right now) */
        if (day.count == 0)
        {
            snprintf(status, sizeof(status), "No bookings for this day");
        }
        else
        {
            int shown = day.count - start_idx;
            if (shown > MAX_BOOKING_ROWS)
            {
                shown = MAX_BOOKING_ROWS;
            }
            snprintf(status, sizeof(status), "Showing %d-%d of %d bookings", start_idx + 1,start_idx + shown, day.count);
        }
        ui_status(status);
        ch = ui_read_key();

        debug_log((DEBUG_TRACE, FUNC_NAME, "key=%d '%c'", ch, (ch >= 32 && ch < 127) ? ch : '?'));

        if (ch == UI_KEY_NONE){
            debug_log((DEBUG_INFO, FUNC_NAME, "Invalid key UI_KEY_NONE received"));
            continue;
        }
        if (ch < 0) {
            debug_log((DEBUG_INFO, FUNC_NAME, "Invalid key ignored: %d", ch));
            continue;
        }

        switch (ch) {
            case UI_KEY_ESC:
                running=0;  /* Exit */
                debug_log((DEBUG_INFO, FUNC_NAME, "Key: Esc Pressed"));
                continue;  /* exit the while loop */

            /* Day navigation */
            case UI_KEY_RIGHT: /* Right Arrow / Next Day */
                debug_log((DEBUG_INFO, FUNC_NAME, "Key: Right Pressed"));
                goto_date(add_days(current_date, 1), &current_date, datestr, &day, &start_idx);
                page_changed = 1;
                break;
            case UI_KEY_LEFT: /* Left Arrow / Previous Day */
                debug_log((DEBUG_INFO, FUNC_NAME, "Key: Left Pressed"));
                goto_date(add_days(current_date, -1), &current_date, datestr, &day, &start_idx);
                page_changed = 1;
                break;

            /* Week navigation */
            case UI_KEY_SHIFT_RIGHT: /* Shift Right = next week */
                debug_log((DEBUG_INFO, FUNC_NAME, "Key:Shift Right Pressed"));
                goto_date(add_days(current_date, 7), &current_date, datestr, &day, &start_idx);
                page_changed = 1;
                break;

            case UI_KEY_SHIFT_LEFT: /* Shift Left = previous week */
                debug_log((DEBUG_INFO, FUNC_NAME, "Key: Shift Left Pressed"));
                goto_date(add_days(current_date, -7), &current_date, datestr, &day, &start_idx);
                page_changed = 1;
                break;

            /* Paging within a day */
            case UI_KEY_PGDN:    /* Page Down /  Scroll Down */
                debug_log((DEBUG_INFO, FUNC_NAME, "Key: Page Down Pressed"));
                if (start_idx + MAX_BOOKING_ROWS < day.count){
                    start_idx += MAX_BOOKING_ROWS;
                    selected_idx = start_idx;
                    page_changed = 1;
                }
                break;

            case UI_KEY_PGUP:    /* Page Up  / Scroll Up */
                debug_log((DEBUG_INFO, FUNC_NAME, "Key: Key Page Up Pressed"));
                if (start_idx >= MAX_BOOKING_ROWS){
                    start_idx -= MAX_BOOKING_ROWS;
                    selected_idx = start_idx;
                    page_changed = 1;
                }
                else{
                    start_idx = 0;
                    selected_idx = start_idx;
                    page_changed = 1;
                }
                break;

            case UI_KEY_UP:
                debug_log((DEBUG_INFO, FUNC_NAME, "Key: Up Pressed"));
                if (selected_idx > 0) {
                    selected_idx--;
                    selection_moved = 1;
                }
                break;

            case UI_KEY_DOWN:
                debug_log((DEBUG_INFO, FUNC_NAME, "Key: Down Pressed"));
                if (selected_idx + 1 < day.count) {
                    selected_idx++;
                    selection_moved = 1;
                }
                break;

            case 'V':
            case 'v':
                debug_log((DEBUG_INFO, FUNC_NAME, "Key: V Pressed for view mode"));
                if (day.count > 0) {
                    booking_t b;
                    if (db_bk_by_index(&day, selected_idx, &b) == 0) {
                        run_booking_detail(b.booking_id, BOOK_VIEW);
                        page_changed = 1;
                    }
                }
                break;

            case 'e':
            case 'E':
            case UI_KEY_F2:
                debug_log((DEBUG_INFO, FUNC_NAME, "Key: E for edit or F2 Pressed for edit mode"));
                if (day.count > 0) {
                    booking_t b;
                    if (db_bk_by_index(&day, selected_idx, &b) == 0) {
                        run_booking_detail(b.booking_id, BOOK_EDIT);
                    }
                    /* Reload day after edit */
                    goto_date(current_date, &current_date, datestr, &day, &start_idx);
                    page_changed = 1;
                }
                break;

            case 'c':
            case 'C':
                debug_log((DEBUG_INFO, FUNC_NAME, "Key: C pressed for create mode"));
                rc = run_booking_detail(NULL, BOOK_CREATE);
                if (rc == BOOK_DETAIL_SAVED) {
                    /* Reload current day after create */
                    goto_date(current_date, &current_date, datestr, &day, &start_idx);
                    page_changed = 1;
                    selected_idx = 0;
                }
                debug_log((DEBUG_TRACE, FUNC_NAME, "After booking_detail screen New"));
                page_changed = 1;
                break;

            case 'g':
            case 'G':
            {
                char buf[16];
                int new_date;
                ui_puts(22, 1, "Date format DD/MM/YYYY, Esc=Quit                                               ");
                ui_puts(UI_COMMAND_ROW, 1, "Command: Goto Date: ");
                if (ui_read_line(UI_COMMAND_ROW, 22, buf, sizeof(buf)) > 0)
                {
                    if (parse_ddmmyyyy(buf, &new_date) == 0)
                    {
                        goto_date(new_date, &current_date, datestr, &day, &start_idx);
                        page_changed = 1;
                    }
                    else
                    {
                        ui_status("Invalid date. Use DD/MM/YYYY");
                        sleep(2);
                        page_changed = 1;
                    }
                }
                page_changed = 1;
                break;
            }
            default:
               break;
        }
        if (page_changed) {
            draw_screen(datestr, &day, start_idx, selected_idx);
            page_changed = 0;
        }
        else
        if (selection_moved) {
            /* keep selection visible */
            if (selected_idx < start_idx) {
                start_idx = selected_idx;
                page_changed = 1;
            }
            else if (selected_idx >= start_idx + MAX_BOOKING_ROWS) {
                start_idx = selected_idx - MAX_BOOKING_ROWS + 1;
                page_changed = 1;
            }
            if (page_changed) {
                debug_log((DEBUG_TRACE, FUNC_NAME, "paged_changed TRUE."));
                draw_screen(datestr, &day, start_idx, selected_idx);
                page_changed = 0;
            }
            else {
                int base_row = 6;
                /* redraw old row (remove highlight) */
                draw_booking_row(&day,
                    base_row + (prev_selected_idx - prev_start_idx),
                    prev_selected_idx,
                    0);
                /* redraw new row (add highlight) */
                draw_booking_row(&day,
                    base_row + (selected_idx - start_idx),
                    selected_idx, 1);
                fflush(stdout);
            }
        }
    }

cleanup:
    db_close(customer_db);
    db_close(state_db);
    db_close(booking_db);
}

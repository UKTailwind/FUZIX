#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "staff_select.h"
#include "ui_common.h"
#include "ui.h"
#include "debug.h"
#include "db_staff.h"
#include "db_booking.h"

#define STAFF_POPUP_WIDTH 40

static staff_t staff[MAX_STAFF_ENTRIES];
static int staff_count = 0;

/* Load staffs from database into staff[] */
static int load_staff(void)
{
    int rc = db_staff_open();
    long rec = 0;

    debug_log((DEBUG_INFO, FUNC_NAME, "Enter:"));

    if (rc < 0)
        return -1;

    staff_count = 0;

    while (staff_count < MAX_STAFF_ENTRIES) {

        if (db_staff_read(rec, &staff[staff_count]) != 0)
            break;

        staff_count++;
        rec++;
    }

    db_staff_close();

/*
* TODO Sort by staff name
* TODO Sort by workflow order
*/
/*
    for (int i = 0; i < state_count - 1; i++) {
        for (int j = i + 1; j < state_count; j++) {

            if (states[i].sort_order > states[j].sort_order) {

                state_t tmp = states[i];
                states[i] = states[j];
                states[j] = tmp;
            }
        }
    }

*/
    debug_log((DEBUG_INFO, FUNC_NAME, "Loaded %d staff", staff_count));
    return staff_count;
}


/* Draw popup frame and border */
static void draw_popup(int start_row, int start_col, int rows, int cols) {
    int r;

    debug_log((DEBUG_INFO, FUNC_NAME, "Enter:"));

    for (r = 0; r < rows; r++) {
        int c;
        for (c = 0; c < cols; c++) {
            ui_puts(start_row + r, start_col + c, (r == 0 || r == rows-1 || c == 0 || c == cols-1) ? "+" : " ");
        }
    }
}

/* Draw the list of staff */
static void draw_staff(int start_row, int start_col, int selected) {
    int i;

    debug_log((DEBUG_INFO, FUNC_NAME, "Enter:"));

    for (i = 0; i < staff_count; i++) {
        if (i == selected) ui_attr_rv_on();
        ui_puts(start_row + i, start_col, staff[i].staff_name);
        if (i == selected) ui_attr_rv_off();
    }
}

/* Main staff selection function */
int staff_select(char *out_staff_id) {
    int selected = 0;
    int i;
    int start_row;
    int start_col;

    debug_log((DEBUG_INFO, FUNC_NAME, "Enter:"));

    if (load_staff() <= 0) return -1;

    /* Find current staff so popup opens on it */
    for (i = 0; i < staff_count; i++) {
        if (strcmp(staff[i].staff_id, out_staff_id) == 0) {
            selected = i;
            break;
        }
    }

    start_row = 7;
    start_col = 14;

    draw_popup(start_row-1, start_col-1, staff_count+2, STAFF_POPUP_WIDTH);
    draw_staff(start_row, start_col, selected);

    while (1) {
        int key = ui_read_key();
        switch (key) {
            case UI_KEY_UP:
                if (selected > 0) selected--;
                break;
            case UI_KEY_DOWN:
                if (selected < staff_count - 1) selected++;
                break;
            case UI_KEY_ENTER:
                /* Copy the selected staff_id (max usable = STAFF_ID_MAX) */
                if (strcmp(out_staff_id, staff[selected].staff_id) != 0) {
                    strncpy(out_staff_id, staff[selected].staff_id, STAFF_ID_MAX);
                    out_staff_id[STAFF_ID_MAX] = '\0';
                    return 1;   /* staff changed */
                }

                return 0;       /* no change */

            case UI_KEY_ESC:
                return -1;  /*/ cancelled */
        }

        draw_staff(start_row, start_col, selected);
    }
}

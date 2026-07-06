#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "db_common.h"
#include "state_select.h"
#include "ui_common.h"
#include "ui.h"
#include "debug.h"
#include "db_state.h"
#include "db_booking.h"

#define STATE_POPUP_WIDTH 30

static state_t states[MAX_STATE_ENTRIES];
static int state_count = 0;

/* Load states from database into states[] */
static int load_states(void)
{
    int rc = db_open(state_db, 0);
    long rec = 0;
    int i;

    debug_log((DEBUG_INFO, FUNC_NAME, "Enter:"));

    if (rc < 0)
        return -1;

    state_count = 0;

    while (state_count < MAX_STATE_ENTRIES) {

        if (db_state_read(rec, &states[state_count]) != 0)
            break;

        state_count++;
        rec++;
    }

    db_close(state_db);

    /* Sort by workflow order */
    for (i = 0; i < state_count - 1; i++) {
        int j;
        for (j = i + 1; j < state_count; j++) {

            if (states[i].sort_order > states[j].sort_order) {
                state_t tmp;
                memcpy(&tmp, states + i, sizeof(state_t));
                memcpy(states + i, states + j, sizeof(state_t));
                memcpy(states + j, &tmp, sizeof(state_t));
            }
        }
    }

    debug_log((DEBUG_INFO, FUNC_NAME, "Loaded %d states", state_count));

    return state_count;
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

/* Draw the list of states */
static void draw_states(int start_row, int start_col, int selected) {
    int i;
    debug_log((DEBUG_INFO, FUNC_NAME, "Enter:"));

    for (i = 0; i < state_count; i++) {
        if (i == selected) ui_attr_rv_on();
        ui_puts(start_row + i, start_col, states[i].name);
        if (i == selected) ui_attr_rv_off();
    }
}

/* Main state selection function */
int state_select(char *out_state_id) {
    int selected = 0;
    int i;
    int start_row, start_col;

    debug_log((DEBUG_INFO, FUNC_NAME, "Enter:"));

    if (load_states() <= 0) return -1;

    /* Find current state so popup opens on it */
    for (i = 0; i < state_count; i++) {
        if (strcmp(states[i].state_id, out_state_id) == 0) {
            selected = i;
            break;
        }
    }

    start_row = 7;
    start_col = 14;

    draw_popup(start_row-1, start_col-1, state_count+2, STATE_POPUP_WIDTH);
    draw_states(start_row, start_col, selected);

    while (1) {
        int key = ui_read_key();
        switch (key) {
            case UI_KEY_UP:
                if (selected > 0) selected--;
                break;
            case UI_KEY_DOWN:
                if (selected < state_count - 1) selected++;
                break;
            case UI_KEY_ENTER:
                /* Copy the selected state_id (max usable = STATE_ID_MAX) */
                if (strcmp(out_state_id, states[selected].state_id) != 0) {
                    strncpy(out_state_id, states[selected].state_id, STATE_ID_MAX);
                    out_state_id[STATE_ID_MAX] = '\0';
                    return 1;   /* state changed */
                }

                return 0;       /* no change */

            case UI_KEY_ESC:
                return -1;  /* cancelled */
        }

        draw_states(start_row, start_col, selected);
    }
}

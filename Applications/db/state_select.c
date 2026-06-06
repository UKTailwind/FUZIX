#include <stdio.h>
#include <string.h>
#include <unistd.h>

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
    debug_log(DEBUG_INFO, FUNC_NAME, "Enter:");

    int fd = db_state_open();
    if (fd < 0)
        return -1;

    state_count = 0;
    long rec = 0;

    while (state_count < MAX_STATE_ENTRIES) {

        if (db_state_read(fd, rec, &states[state_count]) != 0)
            break;

        state_count++;
        rec++;
    }

    db_state_close(fd);

    /* Sort by workflow order */
    int i;
    for (i = 0; i < state_count - 1; i++) {
        int j;
        for (j = i + 1; j < state_count; j++) {

            if (states[i].sort_order > states[j].sort_order) {

                state_t tmp = states[i];
                states[i] = states[j];
                states[j] = tmp;
            }
        }
    }

    debug_log(DEBUG_INFO, FUNC_NAME, "Loaded %d states", state_count);

    return state_count;
}


/* Draw popup frame and border */
static void draw_popup(int start_row, int start_col, int rows, int cols) {
    debug_log(DEBUG_INFO, FUNC_NAME, "Enter:");

    int r;
    for (r = 0; r < rows; r++) {
        int c;
        for (c = 0; c < cols; c++) {
            ui_puts(start_row + r, start_col + c, (r == 0 || r == rows-1 || c == 0 || c == cols-1) ? "+" : " ");
        }
    }
}

/* Draw the list of states */
static void draw_states(int start_row, int start_col, int selected) {
    debug_log(DEBUG_INFO, FUNC_NAME, "Enter:");

    int i;
    for (i = 0; i < state_count; i++) {
        if (i == selected) ui_attr_reverse_on();
        ui_puts(start_row + i, start_col, states[i].name);
        if (i == selected) ui_attr_reverse_off();
    }
}

/* Main state selection function */
int state_select(char *out_state_id) {
    debug_log(DEBUG_INFO, FUNC_NAME, "Enter:");

    if (load_states() <= 0) return -1;

    int selected = 0;
    /* Find current state so popup opens on it */
    int i;
    for (i = 0; i < state_count; i++) {
        if (strcmp(states[i].state_id, out_state_id) == 0) {
            selected = i;
            break;
        }
    }

    int start_row = 7, start_col = 14;

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

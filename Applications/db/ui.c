#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <fcntl.h>
#include <errno.h>
#include "ui_common.h"
#include "time.h"
#include "debug.h"
#include "ui.h"
#include "db_booking.h"
#include "ui_keyboard_parser.h"
#include "term.h"

#define ESC_TIMEOUT_MS 300

static const kb_backend_t *ui_kb_backend = &kb_backend_ansi;

/*------------------ Keyboard Input  ---------------------- */

void ui_set_keyboard_backend(const kb_backend_t *backend)
{
    ui_kb_backend = backend;
}

int ui_read_line(int row, int col, char *buf, int maxlen)
{
    int len = 0;
    int key;
    int cur_col = col;
    char tmp[2];

    debug_log(DEBUG_INFO, FUNC_NAME, "Enter:");

    buf[0] = '\0';

    while (1)
    {
        key = ui_read_key();

        if (key >= 32 && key < 127 && len < maxlen - 1) {
            buf[len++] = key;
            buf[len] = '\0';
            tmp[0] = key;
            tmp[1] = 0;
            ui_puts(row, cur_col, tmp);
            cur_col++;
        }

        switch (key)
        {
            case UI_KEY_ENTER:
                debug_log(DEBUG_INFO, FUNC_NAME, "Pressed UI_KEY_ENTER (loop 1)");
                buf[len] = '\0';
                return len;   /* finished */

            case UI_KEY_BACKSPACE:
                debug_log(DEBUG_TRACE, FUNC_NAME, "Pressed UI_KEY_BACKSPACE");
                if (len > 0 && cur_col > col) {
                    len--;
                    cur_col--;
                    buf[len] = '\0';

                    ui_puts(row, cur_col, " ");
                    ui_set_cursor(row, cur_col);
                }
                break;

            case UI_KEY_ESC:
                debug_log(DEBUG_TRACE, FUNC_NAME,"EXIT triggered by key=%d", key);
                buf[0] = '\0';
                return -1;   /* aborted */

            default:
                debug_log(DEBUG_TRACE, FUNC_NAME, "Unknown key = %d", key);
                break;
        }
    }
}

int decode_escape_sequence(const unsigned char *buf, int len)
{
    debug_log(DEBUG_TRACE, FUNC_NAME, "Enter:");
    if (len >= 6){
        debug_log(DEBUG_TRACE, FUNC_NAME, "INVALID Length >=6");
        return UI_KEY_INVALID;
    }
    /* ===== Detect incomplete sequences FIRST ===== */
    if (buf[0] == 0x1B) {
        if (len == 2 && buf[1] == '['){
            /* ESC [  (definitely incomplete) */
            debug_log(DEBUG_TRACE, FUNC_NAME, "Escape Sequence to short");
            return UI_KEY_INCOMPLETE;
        }
        if (len >= 3 && buf[1] == '[') {
            /* ESC [ 1 ... (likely longer sequence) */
            /* If sequence ends with a letter, it's complete (arrows, etc.) */
            if (buf[len - 1] >= 'A' && buf[len - 1] <= 'Z'){
                /* Sequence ends with a letter, it's complete (arrows, etc.) */
                ; /* let it fall through */
            }
            else if (buf[len - 1] == '~'){
                /* Sequence ends with '~', it's complete (PgUp, PgDn, etc.) */
                ; /* let it fall through */
            }
            else{
                /* Otherwise, still incomplete */
                debug_log(DEBUG_TRACE, FUNC_NAME, "Escape Sequence does not match known format");
                return UI_KEY_INCOMPLETE;
            }
        }

      /* ESC O (function key prefix) */
        if (len == 2 && buf[1] == 'O')
            return UI_KEY_INCOMPLETE;
    }

    /* Detect ESC alone */
    if (len == 1 && buf[0] == 0x1B)
        return UI_KEY_ESC;

    /* ESC [ X */
    if (len == 3 && buf[0] == 0x1B && buf[1] == '[') {
        switch (buf[2]) {
            case 'A': return UI_KEY_UP;
            case 'B': return UI_KEY_DOWN;
            case 'C': return UI_KEY_RIGHT;
            case 'D': return UI_KEY_LEFT;
            case 'H': return UI_KEY_HOME;
            case 'F': return UI_KEY_END;
            case 'Z': return UI_KEY_SHIFT_TAB;
        }
    }

    /* Function Keys */
    if (len == 3) {
        if (memcmp(buf, "\x1BOP", 3) == 0) return UI_KEY_F1;
        if (memcmp(buf, "\x1BOQ", 3) == 0) return UI_KEY_F2;
        if (memcmp(buf, "\x1BOR", 3) == 0) return UI_KEY_F3;
        if (memcmp(buf, "\x1BOS", 3) == 0) return UI_KEY_F4;
    }

    /* F5–F12 (longer sequences) */
    if (len >= 5 && buf[0] == 0x1B && buf[1] == '[') {
        if (memcmp(buf, "\x1B[15~", 5) == 0) return UI_KEY_F5;
        if (memcmp(buf, "\x1B[17~", 5) == 0) return UI_KEY_F6;
        if (memcmp(buf, "\x1B[18~", 5) == 0) return UI_KEY_F7;
        if (memcmp(buf, "\x1B[19~", 5) == 0) return UI_KEY_F8;
        if (memcmp(buf, "\x1B[20~", 5) == 0) return UI_KEY_F9;
        if (memcmp(buf, "\x1B[21~", 5) == 0) return UI_KEY_F10;
        if (memcmp(buf, "\x1B[23~", 5) == 0) return UI_KEY_F11;
        if (memcmp(buf, "\x1B[24~", 5) == 0) return UI_KEY_F12;
    }

    /* ESC [ n ~ sequences */
    if (len == 4 && buf[3] == '~') {
        switch (buf[2]) {
            case '2': return UI_KEY_INSERT;
            case '3': return UI_KEY_DELETE;
            case '5': return UI_KEY_PGUP;
            case '6': return UI_KEY_PGDN;
        }
    }

    /* Shift arrows */
    if (len == 6 && memcmp(buf, "\x1B[1;2C", 6) == 0)
        return UI_KEY_SHIFT_RIGHT;

    if (len == 6 && memcmp(buf, "\x1B[1;2D", 6) == 0)
        return UI_KEY_SHIFT_LEFT;

    /* Shift + Delete */
    if (len == 6 && memcmp(buf, "\x1B[3;2~", 6) == 0)
        return UI_KEY_SHIFT_DELETE;

    return UI_KEY_INVALID;
}

static void draw_field_reverse(int row, int col, const char *buf, int cursor_pos)
{
    debug_log(DEBUG_INFO, FUNC_NAME, "Enter:");
    debug_log(DEBUG_INFO, FUNC_NAME, "row=%d, col=%d cusor_pos=%d", row, col, cursor_pos);
    ui_attr_rv_on();
    ui_puts(row, col, buf);               /* Output Full Line */
    ui_attr_rv_off();
}

void ui_force_terminate(edit_state_t *es)
{
    /* ui_edit_field() edits exactly maxlen characters and does NOT
     * guarantee NUL termination. Callers must enforce '\0'.
     */
    es->buf[es->maxlen] = '\0';
}

void ui_move_cursor(int row, int col)
{
    debug_log(DEBUG_INFO, FUNC_NAME, "Enter:");
    debug_log(DEBUG_INFO, FUNC_NAME, "Position Cursor row=%d col=%d", row, col);
    term_set_cursor(row, col);
    fflush(stdout);
}

int ui_edit_field(edit_state_t *e, int row, int col)
{
    int key;

    debug_log(DEBUG_TRACE, FUNC_NAME, "Enter:");

    /* Initial full draw */
    draw_field_reverse(row, col, e->buf, e->cursor_pos);
    e->cursor_pos = 0;
    ui_move_cursor(row, col);
    debug_log(DEBUG_INFO, FUNC_NAME, "Cursor forced to start");

    while (1)
    {
        debug_log(DEBUG_INFO, FUNC_NAME, "Top of While Loop: cursor_pos=%d buf='%.25s'", e->cursor_pos, e->buf);
        debug_log(DEBUG_INFO, FUNC_NAME,"Top of While Loop: row=%d", row);

        if (e->cursor_pos < 0){
            e->cursor_pos = 0;
        }
        if (e->cursor_pos >= e->maxlen){
            e->cursor_pos = e->maxlen - 1;
            debug_log(DEBUG_INFO, FUNC_NAME, "maxlen check: cursor_pos=%d", e->cursor_pos);
        }

        /* Wait for user input (terminal cursor blinks here) */
        key = ui_read_key();
        debug_log(DEBUG_TRACE, FUNC_NAME, "Decoded key = %d (cursor=%d)", key, e->cursor_pos);

        if (key == UI_KEY_NONE) {
            debug_log(DEBUG_TRACE, FUNC_NAME, "Ignoring unknown key");
            continue;
        }

        switch (key)
        {
            case UI_KEY_LEFT:
                debug_log(DEBUG_INFO, FUNC_NAME, "case UI_KEY_LEFT:");
                if (e->cursor_pos > 0)
                    e->cursor_pos--;
                ui_move_cursor(row, col + e->cursor_pos);
                break;

           case UI_KEY_RIGHT:
                debug_log(DEBUG_INFO, FUNC_NAME, "case UI_KEY_RIGHT:");

                if (e->cursor_pos < e->maxlen - 1)
                    e->cursor_pos++;

                ui_move_cursor(row, col + e->cursor_pos);
                break;

            case UI_KEY_BACKSPACE:
                debug_log(DEBUG_TRACE, FUNC_NAME, "case UI_KEY_BACKSPACE:");
                if (e->cursor_pos > 0)
                {
                    memmove(&e->buf[e->cursor_pos - 1], &e->buf[e->cursor_pos], e->maxlen - e->cursor_pos);
                    e->buf[e->maxlen - 1] = ' ';
                    e->cursor_pos--;
                    draw_field_reverse(row, col, e->buf, e->cursor_pos);
                    ui_move_cursor(row, col + e->cursor_pos);
                }
                break;

            case UI_KEY_DELETE:
                debug_log(DEBUG_TRACE, FUNC_NAME, "case UI_KEY_DELETE:");

                if (e->cursor_pos < e->maxlen - 1)
                {
                    memmove(&e->buf[e->cursor_pos], &e->buf[e->cursor_pos + 1], e->maxlen - e->cursor_pos - 1);
                    e->buf[e->maxlen - 1] = ' ';
                    draw_field_reverse(row, col, e->buf, e->cursor_pos);
                    ui_move_cursor(row, col + e->cursor_pos);
                }
                break;

            case UI_KEY_INSERT:
                debug_log(DEBUG_TRACE, FUNC_NAME, "Insert mode %s", e->insert_mode ? "ON" : "OFF");

                e->insert_mode = !e->insert_mode;    /* Toggle Insert / Over Type mode */
                ui_status(e->insert_mode ? "Insert mode" : "Overwrite mode");
                break;

            case UI_KEY_UP:
            case UI_KEY_DOWN:
            case UI_KEY_ENTER:
            case UI_KEY_TAB:
            case UI_KEY_SHIFT_TAB:
            case UI_KEY_SHIFT_DELETE:
            case UI_KEY_F1:
            case UI_KEY_F2:
            case UI_KEY_ESC:
                debug_log(DEBUG_TRACE, FUNC_NAME, "case: Field change or Function Key pressed");
                return key; /* do NOT consume these key presses */

            default:
                if (key >= 32 && key <= 126 && e->cursor_pos < e->maxlen)
                {
                    debug_log(DEBUG_TRACE, FUNC_NAME, "case default:");

                    if (e->insert_mode)
                    {
                        if (e->cursor_pos < e->maxlen - 1)
                        {
                            /* Shift right only if there is room */
                            memmove(&e->buf[e->cursor_pos + 1], &e->buf[e->cursor_pos], e->maxlen - e->cursor_pos - 1);
                        }

                        /* Always allow write */
                        e->buf[e->cursor_pos] = (char)key;
                        if (e->cursor_pos < e->maxlen - 1)
                            e->cursor_pos++;
                    }

                    else
                    {
                        /* OVERWRITE MODE */
                        e->buf[e->cursor_pos] = (char)key;
                        if (e->cursor_pos < e->maxlen - 1)
                            e->cursor_pos++;
                    }

                    /* Redraw entire field */
                    debug_log(DEBUG_TRACE, FUNC_NAME, "Position Cursor row=%d col=%d", row, e->cursor_pos);
                    draw_field_reverse(row, col, e->buf, e->cursor_pos);
                    ui_move_cursor(row, col + e->cursor_pos);
                }
                break;
        }
    }
}

int ui_read_key(void)
{
    static kb_parser_t kb;
    static int initialised = 0;
    uint8_t b;
    int key;
    int n;

    debug_log(DEBUG_TRACE, FUNC_NAME, "Enter:");

    if (!initialised) {
        kb_init(&kb, ui_kb_backend);
        term_set_vmin_vtime(0, 1);   /* 100ms timeout */
        initialised = 1;
    }
    for (;;) {
        n = read(0, &b, 1);
        if (n == 1) {
            key = kb_feed(&kb, b);
            if (key > 0)
                return key;
        } else {
            /* timeout:  advance parser */
            debug_log(DEBUG_TRACE, FUNC_NAME, "TICK CALLED");
            key = kb_tick(&kb);
            debug_log(DEBUG_TRACE, FUNC_NAME, "TICK RESULT=%d", key);
            if (key > 0)
                return key;
        }
    }
}

/* ----------------- General UI functions ----------------- */

/* Convert "HHMM" to "HH:MM" */
void hhmm_to_hhmm_colon(const char *in, char *out, size_t outsz)
{
    if (!in || strlen(in) < 4 || outsz <  UI_TIME_HHMM_COLON_LEN) {
        if (outsz > 0) out[0] = '\0';
        return;
    }

    out[0] = in[0];
    out[1] = in[1];
    out[2] = ':';
    out[3] = in[2];
    out[4] = in[3];
    out[5] = '\0';
}

/* Convert "HH:MM" to "HHMM" */
void hhmm_colon_to_hhmm(const char *in, char *out, size_t outsz)
{
    if (!in || strlen(in) < 5 || outsz < 5) {
        if (outsz > 0) out[0] = '\0';
        return;
    }

    out[0] = in[0];
    out[1] = in[1];
    out[2] = in[3];
    out[3] = in[4];
    out[4] = '\0';
}

/* Convert YYMMDD to DD/MM/YYYY  */
void yyyymmdd_to_dd_mm_yyyy(const char *in, char *out, size_t outsz)
{
    if (!in || strlen(in) < 8 || outsz < UI_DATE_DD_MM_YYYY_LEN) {
        if (outsz > 0) out[0] = '\0';
        return;
    }

    /* DD/MM/YYYY */
    out[0] = in[6];
    out[1] = in[7];
    out[2] = '/';
    out[3] = in[4];
    out[4] = in[5];
    out[5] = '/';
    out[6] = in[0];
    out[7] = in[1];
    out[8] = in[2];
    out[9] = in[3];
    out[10] = '\0';
}

/* Convert DD/MM/YYYY to YYYYMMDD */
void dd_mm_yyyy_to_yyyymmdd(const char *in, char *out, size_t outsz)
{
    #define YYYYMMDD_LEN 8

    /* Need 8 chars + NUL */
    if (!in || strlen(in) < 10 || outsz < (YYYYMMDD_LEN + 1)) {
        debug_log(DEBUG_WARN, FUNC_NAME, "reject: in='%s' len=%zu outsz=%zu", in ? in : "(null)", in ? strlen(in) : 0, outsz);
        if (outsz > 0) out[0] = '\0';
        return;
    }

    out[0] = in[6];
    out[1] = in[7];
    out[2] = in[8];
    out[3] = in[9];
    out[4] = in[3];
    out[5] = in[4];
    out[6] = in[0];
    out[7] = in[1];
    out[8] = '\0';
}

/* Right trim spaces from string */
void ui_rtrim(char *s)
{
    char *p;
    if (!s || !*s)
        return;

    p = s + strlen(s) - 1;

    while (p >= s && *p == ' ') {
        *p-- = '\0';
    }
}

/* TODO Update all calls to ui_set_cursor*()  */
void ui_set_cursor(int y, int x)
{
    term_set_cursor(y,x);
}

/* TODO Update all calls to ui_cls() */
void ui_cls(void)
{
    term_cls();
}

void ui_puts(int y, int x, const char *str)
{
    term_set_cursor(y, x);
    term_puts(str);
}

void ui_puts_padded(int row, int col, const char *val, int width)
{
    char out[128];
    snprintf(out, sizeof(out), "%-*s", width, val);
    ui_puts(row, col, out);   /* reuse correct cursor logic */
}

void ui_term_raw_on(void)
{
    term_raw_on();
}

void ui_term_raw_off(void)
{
    term_raw_off();
}

/* ----------------- Cursor helpers ----------------- */

void ui_save_cursor(void)
{
    term_save_cursor();
}

void ui_restore_cursor(void)
{
    term_restore_cursor();
}

/* ----------------- Attribute helpers ----------------- */
/* Revers Text On / Off */
void ui_attr_rv_on(void)
{
    term_rv_on();
}

void ui_attr_rv_off(void)
{
    term_rv_off();
}

/* ----------------- Status bar ----------------- */

void ui_status(const char *msg)
{
    char line[81];

    debug_log(DEBUG_TRACE, FUNC_NAME, "Enter:");
    debug_log(DEBUG_TRACE, FUNC_NAME,"msg: %s", msg);
    ui_save_cursor();
    if (!msg)
        msg = "";

    /* pad/truncate to 78 characters (status bar width) */
    snprintf(line, sizeof(line), " %-78.78s ", msg);

    ui_attr_rv_on();
    ui_puts(24, 1, line);
    ui_attr_rv_off();

    ui_restore_cursor();
    fflush(stdout);
}

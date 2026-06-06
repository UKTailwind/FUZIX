#include "ui_keyboard_parser.h"
#include "ui_common.h"
#include <string.h>
#include "debug.h"

#define KB_ESC_TIMEOUT 3

int kb_decode_sequence(kb_parser_t *kb, const char *seq)
{
    debug_log(DEBUG_TRACE, FUNC_NAME, "Enter:");
    int i;
    for (i = 0; kb->backend->seq_table[i].seq != NULL; i++) {
        if (strcmp(seq, kb->backend->seq_table[i].seq) == 0)
        return kb->backend->seq_table[i].key;
    }
    return UI_KEY_INVALID;
}

/* --- Public API --- */

void kb_init(kb_parser_t *kb, const kb_backend_t *backend)
{
    debug_log(DEBUG_TRACE, FUNC_NAME, "Enter:");
    kb->backend = backend;
    kb->state = KB_STATE_IDLE;
    kb->len = 0;
}

/* Core parser */
int kb_feed(kb_parser_t *kb, uint8_t byte)
{
    debug_log(DEBUG_INFO, FUNC_NAME, "Enter:");
    debug_log(DEBUG_TRACE, FUNC_NAME, "byte=%d (0x%02X) '%c'", byte, byte, (byte >= 32 && byte < 127) ? byte : '.');
    debug_log(DEBUG_INFO, FUNC_NAME, "state=%d byte=0x%02X '%c'", kb->state, byte, (byte >= 32 && byte < 127) ? byte : '.');

    switch (kb->state) {

    case KB_STATE_IDLE:
        debug_log(DEBUG_TRACE, FUNC_NAME, "case KB_STATE_IDLE");
        debug_log(DEBUG_TRACE, FUNC_NAME, "byte=0x%02X '%c'", byte, byte);

        if (byte == 0x1B) {
            kb->state = KB_STATE_ESC;
            kb->esc_countdown = kb->backend->esc_timeout;
            return 0;
        }

        /* Normalise control keys */
        switch (byte) {
            case '\r':   /* CR */
            case '\n':   /* LF (some systems use this instead) */
            return UI_KEY_ENTER;

            case '\t':
                return UI_KEY_TAB;

            case 127:    /* DEL (most terminals send this for backspace) */
                debug_log(DEBUG_TRACE, FUNC_NAME, "case 127");
                return UI_KEY_BACKSPACE;

            case 8:      /* BS (some systems send this instead) */
                debug_log(DEBUG_TRACE, FUNC_NAME, "case 8");
                return UI_KEY_BACKSPACE;
        }

        /* Printable or other raw character */
        return byte;

    case KB_STATE_ESC:
    {
        debug_log(DEBUG_TRACE, FUNC_NAME, "case KB_STATE_ESC");

        int ret = kb->backend->handle_esc(kb, byte);
        /* IMPORTANT: if backend produced a final key, reset state */
        if (ret != 0) {
            kb->state = KB_STATE_IDLE;
        }
        return ret;
    }
    case KB_STATE_CSI:
        debug_log(DEBUG_TRACE, FUNC_NAME, "case KB_STATE_CSI ");
        if (kb->len < sizeof(kb->seq_buf) - 1) {
            kb->seq_buf[kb->len++] = byte;
            kb->seq_buf[kb->len] = '\0';
        } else {
            /* Overflow → reset */
            debug_log(DEBUG_TRACE, FUNC_NAME, "Overflow Reset");
            kb->state = KB_STATE_IDLE;
            kb->len = 0;
            /*kb->pending = -1;*/
            return UI_KEY_INVALID;
        }

        if (kb->backend->is_terminator(byte)){
            debug_log(DEBUG_TRACE, FUNC_NAME, "SEQ='%s'", kb->seq_buf);
            int key = kb_decode_sequence(kb, kb->seq_buf);
            kb->state = KB_STATE_IDLE;
            kb->len = 0;
            return key;
        }
        return 0;

    case KB_STATE_SS3:
        debug_log(DEBUG_TRACE, FUNC_NAME, "case KB_STATE_SS3");

        kb->state = KB_STATE_IDLE;

        switch (byte) {
            case 'P': return UI_KEY_F1;
            case 'Q': return UI_KEY_F2;
            case 'R': return UI_KEY_F3;
            case 'S': return UI_KEY_F4;
            default:
                return UI_KEY_INVALID;
        }
    }
    /* Should never happen */
    debug_log(DEBUG_ERROR, FUNC_NAME, "SHOULD NEVER HAPPEN");
    kb->state = KB_STATE_IDLE;
    return UI_KEY_INVALID;
}

int kb_tick(kb_parser_t *kb)
{
    debug_log(DEBUG_TRACE, FUNC_NAME, "Enter:");

    if (kb->state == KB_STATE_ESC) {

        if (kb->esc_countdown > 0) {
            kb->esc_countdown--;
            debug_log(DEBUG_TRACE, FUNC_NAME, "kb countdown=%d", kb->esc_countdown);
            if (kb->esc_countdown == 0) {
                kb->state = KB_STATE_IDLE;
                kb->len = 0;
                kb->seq_buf[0] = '\0';
                return UI_KEY_ESC;
            }
        }
    }
    return 0;
}

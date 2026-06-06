#include "ui_keyboard_parser.h"
#include "kb_definition_tables.h"
#include "ui_common.h"
#include "debug.h"

#define ANSI_ESC_TIMEOUT 3

static int kb_ansi_is_terminator(char c)
{
    debug_log(DEBUG_TRACE, FUNC_NAME, "Enter:");
    return (c >= '@' && c <= '~');
}

static int kb_ansi_handle_esc(kb_parser_t *kb, uint8_t byte)
{
    debug_log(DEBUG_TRACE, FUNC_NAME, "ANSI ESC handler byte=%c", byte);

    if (byte == kb->backend->csi_intro) {
        kb->state = KB_STATE_CSI;
        kb->len = 0;
        kb->seq_buf[kb->len++] = kb->backend->csi_intro;
        return 0;
    }

    if (byte == kb->backend->ss3_intro) {
        kb->state = KB_STATE_SS3;
        return 0;
    }

    kb->state = KB_STATE_IDLE;
    return UI_KEY_ESC;
}

const kb_backend_t kb_backend_ansi =
{
    kb_ansi_seq_table,
    '[',                    /* csi_intro                            */
    'O',                    /*  ss3_intro                           */
    ANSI_ESC_TIMEOUT,       /* ~300ms with VTIME=1 (100ms per tick) */
    kb_ansi_is_terminator,
    kb_ansi_handle_esc
};

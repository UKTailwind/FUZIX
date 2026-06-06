#include "ui_keyboard_parser.h"
#include "kb_definition_tables.h"
#include "ui_common.h"
#include "debug.h"

static int kb_vt52_is_terminator(char c)
{
    (void)c;
    return 1;
}

static int kb_vt52_handle_esc(kb_parser_t *kb, uint8_t byte)
{
    debug_log(DEBUG_TRACE, FUNC_NAME, "VT-52 ESC handler byte=%c", byte);

    char seq[2];

    seq[0] = byte;
    seq[1] = '\0';

    return kb_decode_sequence(kb, seq);
}

const kb_backend_t kb_backend_vt52 =
{
    kb_vt52_seq_table,
    0,
    0,
    20,    /* VT52 ESC timeout (untested on real hardware) */
    kb_vt52_is_terminator,
    kb_vt52_handle_esc
};


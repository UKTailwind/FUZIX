
#include "ui_keyboard_parser.h"
#include "ui_common.h"

/* --- ANSI Decode Table --- */
const kb_seq_map_t kb_ansi_seq_table[] = {
    {"[A", UI_KEY_UP},
    {"[B", UI_KEY_DOWN},
    {"[C", UI_KEY_RIGHT},
    {"[D", UI_KEY_LEFT},

    {"[1;2D", UI_KEY_SHIFT_LEFT},
    {"[1;2C", UI_KEY_SHIFT_RIGHT},

    {"[5~", UI_KEY_PGUP},
    {"[6~", UI_KEY_PGDN},

    {"[H", UI_KEY_HOME},
    {"[F", UI_KEY_END},

    {"[2~", UI_KEY_INSERT},
    {"[3~", UI_KEY_DELETE},

    {"[15~", UI_KEY_F5},
    {"[17~", UI_KEY_F6},
    {"[18~", UI_KEY_F7},
    {"[19~", UI_KEY_F8},
    {"[20~", UI_KEY_F9},
    {"[21~", UI_KEY_F10},
    {0, 0}
};


#include "ui_keyboard_parser.h"
#include "ui_common.h"

/* --- VT-52 Decode Table --- */
const kb_seq_map_t kb_vt52_seq_table[] = {
    {"A", UI_KEY_UP},
    {"B", UI_KEY_DOWN},
    {"C", UI_KEY_RIGHT},
    {"D", UI_KEY_LEFT},
    {"H", UI_KEY_HOME},
    {0, 0}
};

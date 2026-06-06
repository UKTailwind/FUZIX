#ifndef UI_COMMON_H
#define UI_COMMON_H

#define UI_HELP_ROW     22
#define UI_COMMAND_ROW  23
#define UI_STATUS_ROW   24

#define UI_KEY_ESC    0x1000
#define UI_KEY_NONE       (-1)
#define UI_KEY_INCOMPLETE (-2)
#define UI_KEY_INVALID    (-3)

#define UI_KEY_UP     0x1001
#define UI_KEY_DOWN   0x1002
#define UI_KEY_LEFT   0x1003
#define UI_KEY_RIGHT  0x1004
#define UI_KEY_PGUP   0x1005
#define UI_KEY_PGDN   0x1006
#define UI_KEY_HOME   0x1007
#define UI_KEY_END    0x1008
#define UI_KEY_INSERT 0x1009
#define UI_KEY_DELETE 0x100A
#define UI_KEY_ENTER  0x100B
#define UI_KEY_TAB    0x100C
#define UI_KEY_BACKSPACE    0x100D

#define UI_KEY_SHIFT_LEFT   0x1010
#define UI_KEY_SHIFT_RIGHT  0x1011
#define UI_KEY_SHIFT_DELETE 0x1012
#define UI_KEY_F1           0x1013
#define UI_KEY_F2           0x1014
#define UI_KEY_F3           0x1015
#define UI_KEY_F4           0x1016
#define UI_KEY_F5           0x1017
#define UI_KEY_F6           0x1018
#define UI_KEY_F7           0x1019
#define UI_KEY_F8           0x101A
#define UI_KEY_F9           0x101B
#define UI_KEY_F10          0x101C
#define UI_KEY_F11          0x101D
#define UI_KEY_F12          0x101E

#define UI_KEY_SHIFT_TAB    0x101F

#define UI_TIME_HHMM_COLON_LEN  5     /*Space for Null included when needed (not here) */
#define UI_DATE_YYYYMMDD_LEN    8     /* " */
#define UI_DATE_DD_MM_YYYY_LEN  10    /* " */

#define UI_FIELD_SKIP      0   /* not selectable */
#define UI_FIELD_EDIT      1   /* selectable + editable */
#define UI_FIELD_SELECT    2   /* selectable but not editable */

typedef struct {
    char *buf;          /* field buffer being edited */
    int   maxlen;       /* max length of buffer */
    int   cursor_pos;   /* current cursor position */
    int insert_mode;    /* 0 = overwrite, 1 = insert */
} edit_state_t;

#endif /* UI_COMMON_H */

#ifndef UI_H
#define UI_H

#include "ui_common.h" 
#include "ui_keyboard_parser.h"

/* Keyboard input */
void ui_set_keyboard_backend(const kb_backend_t *backend);
int ui_edit_field(edit_state_t *e, int row, int col);
int ui_read_line(int row, int col, char *buf, int maxlen);

int ui_read_key(void);

/* General screen helpers */
void ui_set_cursor(int y, int x);
void ui_cls(void);
void ui_puts(int y, int x, const char *str);
void ui_puts_padded(int row, int col, const char *val, int width);

/* Status bar */
void ui_status(const char *msg);

/* Cursor save/restore */
void ui_save_cursor(void);
void ui_restore_cursor(void);

/* Text attributes */
void ui_attr_rv_on(void);
void ui_attr_rv_off(void);

/* Time formatting helpers */
void hhmm_to_hhmm_colon(const char *in, char *out, size_t outsz);
void hhmm_colon_to_hhmm(const char *in, char *out, size_t outsz);

/* Date formatting helpers */
void yyyymmdd_to_dd_mm_yyyy(const char *in, char *out, size_t outsz);
void dd_mm_yyyy_to_yyyymmdd(const char *in, char *out, size_t outsz);

/*C String Termination Guarantee */
void ui_force_terminate(edit_state_t *es);

/* Other string manipulation */
void ui_rtrim(char *s);

#endif /* UI_H */

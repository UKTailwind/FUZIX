#ifndef TERM_H
#define TERM_H

void term_cls(void);
void term_set_cursor(int y, int x);
void term_puts(const char *str);
void term_save_cursor(void);
void term_restore_cursor(void);
void term_rv_on(void);
void term_rv_off(void);
void term_raw_on(void);
void term_raw_off(void);
void term_set_vmin_vtime(int vmin, int vtime);

#endif /* TERM_H */




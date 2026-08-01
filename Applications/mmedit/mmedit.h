/* mmedit - the MMBasic full-screen editor as a Fuzix file editor.
 *
 * This header is the seam between MMBasic's editor code and Fuzix.
 * The key codes are MMBasic's own (Hardware_Includes.h) so the ported
 * dispatch compiles unchanged; the terminal underneath is an ordinary
 * VT100, and inkey() turns its escape sequences back into these.
 */

#ifndef MMEDIT_H
#define MMEDIT_H

/* --- MMBasic key codes, verbatim ----------------------------------------- */
#define K_ESC       0x1B
#define K_DEL       0x7F

#define K_UP        0x80
#define K_DOWN      0x81
#define K_LEFT      0x82
#define K_RIGHT     0x83
#define K_INSERT    0x84
#define K_HOME      0x86
#define K_END       0x87
#define K_PUP       0x88
#define K_PDOWN     0x89

#define K_F1        0x91
#define K_F2        0x92
#define K_F3        0x93
#define K_F4        0x94
#define K_F5        0x95
#define K_F6        0x96
#define K_F7        0x97
#define K_F8        0x98
#define K_F9        0x99
#define K_F10       0x9A
#define K_F11       0x9B
#define K_F12       0x9C

/* Shift adds 0x20 throughout - MMBasic's own rule, so SHIFT_F3 is
 * 0xB3 and shifted cursor keys land in 0xA0..0xA9. */
#define K_SHIFT(k)  ((k) + 0x20)
#define K_SHIFT_F3  0xB3
#define K_SHIFT_F4  0xB4
#define K_SHIFT_F5  0xB5
#define K_SHIFT_F8  0xB8

#define CTRLKEY(c)  ((c) & 0x1F)

/* --- terminal ------------------------------------------------------------- */
extern int scr_rows, scr_cols;      /* from TIOCGWINSZ, 80x40 on the PC3 */

int  term_open(void);               /* raw mode; 0 ok, -1 failed */
void term_close(void);              /* restore; safe to call twice */

/* Non-blocking like MMBasic's MMInkey: 0 when nothing is waiting.
 * A lone ESC is returned as K_ESC once no continuation arrives. */
int  inkey(void);

/* --- screen output -------------------------------------------------------- */
void scr_puts(const char *s);
void scr_putc(char c);
void scr_flush(void);
void scr_cls(void);
void scr_goto(int row, int col);    /* 0-based */
void scr_eol(void);                 /* erase to end of line */
void scr_colour(int fg, int bg);    /* ANSI 0-7, -1 leaves alone */
void scr_normal(void);
void scr_inverse(int on);
void scr_cursor(int on);
void scr_wrap(int on);              /* DECAWM */

/* --- the edit buffer ------------------------------------------------------ */
/* Flat, in this process's own SRAM: measured at 44 MB/s against the
 * PSRAM arena's 12, and a process has ~240 KB to play with, so the
 * arena is not needed.  See PC3-EDITOR-REVIEW.md. */
#define EDBUF_SIZE (120 * 1024)

extern unsigned char *EdBuff;
extern int EdBuffSize;
extern int nbrlines;                /* lines currently in the buffer */

int  file_load(const char *name);   /* -1 = error, sets errno */
int  file_save(const char *name);
int  file_backup(const char *name); /* <name>.bak, as MMBasic does */
int  buf_count_lines(void);
unsigned char *buf_line(int n);     /* start of line n, or NULL */

/* --- the editor ----------------------------------------------------------- */
/* MMBasic's own entry point.  x,y is where the cursor starts.  The key
 * that ended the session is left in editor_exit_key: F2 means "save,
 * exit and run", which on a PicoMite handed the program to the
 * interpreter and here is the caller's business. */
void FullScreenEditor(int x, int y, char *fname, int edit_buff_size,
                      int cmdfile);
extern int editor_exit_key;
extern int VWidth, VHeight;
extern unsigned char *txtp;

#endif

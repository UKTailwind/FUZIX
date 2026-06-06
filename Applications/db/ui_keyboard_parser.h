#ifndef UI_KEYBOARD_PARSER_H
#define UI_KEYBOARD_PARSER_H
#define KB_SEQ_BUF_SIZE 16

#include <stdint.h>

/* Parser states */
typedef enum {
    KB_STATE_IDLE = 0,
    KB_STATE_ESC,
    KB_STATE_CSI,
    KB_STATE_SS3
} kb_state_t;

/* Parser instance */
/* sequence table */
typedef struct {
    const char *seq;
    int key;
} kb_seq_map_t;

/* forward declare parser (needed for function pointers) */
typedef struct kb_parser_t kb_parser_t;

int kb_decode_sequence(kb_parser_t *kb, const char *seq);

/* backend */
typedef struct kb_backend_t {
    const kb_seq_map_t *seq_table;
    char csi_intro;
    char ss3_intro;
    int esc_timeout;
    int (*is_terminator)(char c);
    int (*handle_esc)(kb_parser_t *kb, uint8_t byte);

} kb_backend_t;

extern const kb_backend_t kb_backend_vt52;
extern const kb_backend_t kb_backend_ansi;

/* parser */
typedef struct kb_parser_t {
    uint8_t state;
    uint8_t len;
    uint8_t esc_countdown;
    char    seq_buf[KB_SEQ_BUF_SIZE];
    const kb_backend_t *backend;
} kb_parser_t;
/* removed  uint8_t esc_timer; */
/* removed  int     pending;*/


/* API */

/* Initialise parser */
void kb_init(kb_parser_t *kb, const kb_backend_t *backend);

extern const kb_backend_t kb_backend_ansi;

/* Feed one byte into parser
   Returns:
     0             -> no key yet
     >0 (UI_KEY_*) -> key event ready
*/
int kb_feed(kb_parser_t *kb, uint8_t byte);

/* Advance time (call periodically)
   Returns:
     0 or key event (same as kb_feed)
*/
int kb_tick(kb_parser_t *kb);

#endif

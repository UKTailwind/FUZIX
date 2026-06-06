#ifndef DB_STATE_H
#define DB_STATE_H
#include <stddef.h>

#define MAX_STATE_ENTRIES 8

#define STATE_ID_MAX      6
#define STATE_NAME_MAX   16

typedef struct {
    char state_id[STATE_ID_MAX + 1];
    int  sort_order;
    char name[STATE_NAME_MAX + 1];
} state_t;

/* parsing / formatting */
int db_state_parse_line(const char *line, state_t *out);
void db_state_format_line(const state_t *in, char *line);

/* record-level I/O */
int db_state_read(int fd, long recno, state_t *out);
int db_state_write(int fd, long recno, const state_t *in);
int db_state_name_from_id(int fd, const char *state_id, char *state_name);


/* db_open and close */
int db_state_open(void);
int db_state_close(int fd);

#endif

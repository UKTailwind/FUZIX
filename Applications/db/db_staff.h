#ifndef DB_STAFF_H
#define DB_STAFF_H
#include <stddef.h>

#define STAFF_DB_FILE	"data/staff.db"

extern struct dbase *staff_db;

#define MAX_STAFF_ENTRIES 15

#define STAFF_STATUS_MAX     1
#define STAFF_ID_MAX         6
#define STAFF_NAME_MAX       25
#define STAFF_PHONE_MAX      12
#define STAFF_NOTES_MAX      76

/* staff_status values */
#define STAFF_STATUS_ACTIVE   'A'  /* Active */
#define STAFF_STATUS_DELETED  'D'  /* Soft Delete */

typedef struct {
    char staff_status[STAFF_STATUS_MAX + 1];
    char staff_id[STAFF_ID_MAX + 1];
    char staff_name[STAFF_NAME_MAX + 1];
    char staff_phone[STAFF_PHONE_MAX + 1];
    char staff_notes[STAFF_NOTES_MAX + 1];
} staff_t;


typedef struct {
    char staff_status[STAFF_STATUS_MAX + 1];
    char staff_id[STAFF_ID_MAX + 1];
    char staff_name[STAFF_NAME_MAX + 1];
    char staff_phone[STAFF_PHONE_MAX + 1];
} staff_list_rec_t;


/* ---- Staff view ---- */
typedef struct {
    int count;
    staff_list_rec_t slots[MAX_STAFF_ENTRIES];
} StaffList;

/* parsing / formatting */
int db_staff_parse_line(const char *line, staff_t *out);
void db_staff_format_line(const staff_t *in, char *line);

/* record-level I/O */
int db_staff_read(long recno, staff_t *out);
int db_staff_write(long *recno, const staff_t *in);

/* Helpers */
int db_staff_load_all(StaffList *list);
int db_staff_read_by_id(const char *staff_id, staff_t *out, long *out_recno);
int db_staff_generate_next_id(char *out_id);
int db_staff_lookup_display(const char *staff_id, char *out, size_t outlen);

#endif

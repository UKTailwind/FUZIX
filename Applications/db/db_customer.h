#ifndef DB_CUSTOMER_H
#define DB_CUSTOMER_H
#include <stddef.h>

#define CUSTOMER_DB_FILE "data/customer.db"
#define MAX_CUSTOMER_ENTRIES 100
#define CUSTOMER_PAGE_SIZE 17

#define CUSTOMER_DB_OK          0
#define CUSTOMER_DB_EOF         1
#define CUSTOMER_DB_ERROR      -1

#define CUSTOMER_STATUS_MAX     1
#define CUSTOMER_ID_MAX         6
#define CUSTOMER_NAME_MAX       25
#define CUSTOMER_PHONE1_MAX     12
#define CUSTOMER_PHONE2_MAX     12
#define CUSTOMER_ADDRESS1_MAX   30
#define CUSTOMER_ADDRESS2_MAX   30
#define CUSTOMER_SUBURB_MAX     30
#define CUSTOMER_STATE_MAX      5
#define CUSTOMER_POSTCODE_MAX   6
#define CUSTOMER_NOTES_MAX      60

/* cs_status values */
#define CUSTOMER_STATUS_ACTIVE   'A'  /* Active */
#define CUSTOMER_STATUS_DELETED  'D'  /* Soft Delete */

typedef struct {
    char cs_status[CUSTOMER_STATUS_MAX + 1];
    char cs_id[CUSTOMER_ID_MAX + 1];
    char cs_name[CUSTOMER_NAME_MAX + 1];
    char cs_phone1[CUSTOMER_PHONE1_MAX + 1];
    char cs_phone2[CUSTOMER_PHONE2_MAX + 1];
    char cs_address1[CUSTOMER_ADDRESS1_MAX + 1];
    char cs_address2[CUSTOMER_ADDRESS2_MAX + 1];
    char cs_suburb[CUSTOMER_ADDRESS2_MAX +1 ];
    char cs_state[CUSTOMER_STATE_MAX + 1];
    char cs_postcode[CUSTOMER_POSTCODE_MAX + 1];
    char cs_notes[CUSTOMER_NOTES_MAX + 1];
} customer_t;


typedef struct {
    char cs_status[CUSTOMER_STATUS_MAX + 1];
    char cs_id[CUSTOMER_ID_MAX + 1];
    char cs_name[CUSTOMER_NAME_MAX + 1];
    char cs_phone1[CUSTOMER_PHONE1_MAX + 1];
    char cs_phone2[CUSTOMER_PHONE2_MAX + 1];
} customer_list_rec_t;


/* ---- Customer view ---- */
typedef struct {
    int count;
    customer_list_rec_t slots[CUSTOMER_PAGE_SIZE];
} CustomerList;


/* parsing / formatting */
int db_cs_parse_line(const char *line, customer_t *out);
void db_cs_format_line(const customer_t *in, char *line);

/* record-level I/O */
int db_cs_read(int fd, long recno, customer_t *out);
int db_cs_write(int fd, long *recno, const customer_t *in);

/* Helpers */
int db_cs_load_page(int fd, long start_rec, CustomerList *list, long *next_rec);
int db_cs_by_id(int fd, const char *customer_id, customer_t *out, long *out_recno);
int db_cs_generate_next_id(int fd, char *out_id);
int db_cs_lookup_display(int customer_fd, const char *customer_id, char *out, size_t outlen);

/* db_open and close */
int db_cs_op_read(void);
int db_cs_op_write(void);
int db_cs_cl_read(int fd);
int db_cs_cl_write(int fd);

#endif

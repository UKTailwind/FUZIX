#ifndef DB_H
#define DB_H
#include <stddef.h>
#include "db_booking_layout.h"

extern struct dbase *booking_db;

#define BOOKING_DB_FILE "data/booking.db"
#define MAX_DAY_BOOKINGS 20
#define BOOKING_STATUS_MAX        1
#define BOOKING_ID_MAX            6
#define BOOKING_CUSTOMER_ID_MAX   6
#define BOOKING_DATE_MAX          10
#define BOOKING_START_TIME_MAX    5
#define BOOKING_END_TIME_MAX      5
#define BOOKING_STAFF_ID_MAX   6
#define BOOKING_STATE_ID_MAX      6
#define BOOKING_JOB_MAX           41

#define BOOKING_DB_OK          0
#define BOOKING_DB_EOF         1
#define BOOKING_DB_ERROR      -1

/* booking_status values */
#define BOOKING_STATUS_ACTIVE   'A'  /* Active */
#define BOOKING_STATUS_DELETED  'D'  /* Soft Delete */

typedef struct {
    char booking_status[BOOKING_STATUS_MAX +1];
    char booking_id[ID_LEN +1];
    char customer_id[ID_LEN +1];
    char booking_date[BOOKING_DATE_MAX +1];
    char booking_start_time[BOOKING_START_TIME_MAX +1];
    char booking_end_time[BOOKING_END_TIME_MAX +1];
    char booking_customer_id[BOOKING_CUSTOMER_ID_MAX +1];
    char booking_staff_id[ID_LEN +1];
    char booking_state_id[ID_LEN + 1];

    char booking_job[BOOKING_JOB_LEN +1];
} booking_t;


/* ---- Day view ---- */
typedef struct {
    int count;
    long recnos[MAX_DAY_BOOKINGS];
} DayBookings;

/* parsing / formatting */
int  db_bk_parse_line(const char *line, booking_t *out);
void db_bk_format_line(const booking_t *in, char *line);
void db_bk_sort_day_by_time(DayBookings *day);

/* record-level I/O */
int db_bk_read(long recno, booking_t *out);
int db_bk_write(long recno, const booking_t *in);
int db_bk_by_id(const char *booking_id, booking_t *out, long *recno);
int db_bk_generate_next_id(char *booking_id);
int db_bk_append(const booking_t *in);
int db_bk_by_index(const DayBookings *day, int index, booking_t *out);

int db_bk_build_day_index(int target_date, DayBookings *day);
int db_load_day(int ymd, DayBookings *day);

#endif

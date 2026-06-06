#ifndef DB_H
#define DB_H
#include <stddef.h>
#include "db_booking_layout.h"

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
    char  booking_end_time[BOOKING_END_TIME_MAX +1];
    char booking_customer_id[BOOKING_CUSTOMER_ID_MAX +1];
    char booking_staff_id[ID_LEN +1];
    char booking_state_id[ID_LEN + 1];

    char booking_job[JOB_LEN +1];
} booking_t;


/* ---- Day view ---- */
typedef struct {
    int count;
    long recnos[MAX_DAY_BOOKINGS];
} DayBookings;

/* parsing / formatting */
int  db_booking_parse_line(const char *line, booking_t *out);
void db_booking_format_line(const booking_t *in, char *line);
void db_booking_sort_day_by_time(int fd, DayBookings *day);

/* record-level I/O */
int db_booking_read(int fd, long recno, booking_t *out);
int db_booking_write(int fd, long recno, const booking_t *in);
int db_booking_read_by_id(int fd, const char *booking_id, booking_t *out, long *recno);
int db_booking_generate_next_id(int fd, char *booking_id);
int db_booking_append(int fd, const booking_t *in);
int db_booking_read_by_index(int fd, const DayBookings *day, int index, booking_t *out);

int db_booking_build_day_index(int fd, int target_date, DayBookings *day);

/* db_open and close */
int db_booking_open_read();
int db_booking_open_write();
int db_booking_close_read(int fd);
int db_booking_close_write(int fd);
int db_load_day(int fd, int ymd, DayBookings *day);

#endif

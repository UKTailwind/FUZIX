#ifndef BOOKING_DETAIL_H
#define BOOKING_DETAIL_H

#include "db_booking.h"

/* Public modes */
typedef enum {
    BOOK_VIEW,
    BOOK_EDIT,
    BOOK_CREATE
} book_mode_t;

typedef enum {
    BOOK_DETAIL_ERROR     = -1,
    BOOK_DETAIL_NO_CHANGE = 0,
    BOOK_DETAIL_SAVED     = 1,
    BOOK_DETAIL_CANCELLED = 2
} book_detail_result_t;

/* Public API */
int run_booking_detail(int booking_fd, const char *booking_id, book_mode_t mode);

#endif

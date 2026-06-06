/*
 * db_booking_layout.h
 *
 * All constants in this file define on-disk record layout.
 * Changing any value here will invalidate existing data files.

 * Every _LEN must be respected identically by:
 * formatter
 * parser
 * struct definitions
 * separator placement

*/

#ifndef DB_LAYOUT_H
#define DB_LAYOUT_H

#include "db_common_layout.h"

#define BOOKING_STATUS_BASE_LEN   1
#define DATE_LEN       8    /* YYYYMMDD */
#define TIME_LEN       4    /* HHMM */

/* ============================================================
 * BOOKING record layout
 * ============================================================ */
#define JOB_LEN           41

/*
 * Booking Table Field order:
 * Booking_status | BookingID | CustomerID | Date | StartTime | EndTime | StaffID |Booking_State | Job
*/

/* ---- Booking Status ---*/
#define BOOKING_STATUS_OFF 0
#define BOOKING_STATUS_LEN  BOOKING_STATUS_BASE_LEN

/* ---- Booking ID ---- */
#define BOOKING_ID_OFF (BOOKING_STATUS_OFF + BOOKING_STATUS_LEN + FIELD_SEP_LEN)
#define BOOKING_ID_LEN   ID_LEN

/* ---- Customer ID ---- */
#define BOOKING_CUSTOMER_ID_OFF (BOOKING_ID_OFF + BOOKING_ID_LEN + FIELD_SEP_LEN)
#define BOOKING_CUSTOMER_ID_LEN   ID_LEN

/* ---- Date ---- */
#define BOOKING_DATE_OFF (BOOKING_CUSTOMER_ID_OFF + BOOKING_CUSTOMER_ID_LEN + FIELD_SEP_LEN)
#define BOOKING_DATE_LEN          DATE_LEN

/* ---- Start time ---- */
#define BOOKING_START_TIME_OFF (BOOKING_DATE_OFF + BOOKING_DATE_LEN + FIELD_SEP_LEN)
#define BOOKING_START_TIME_LEN    TIME_LEN

/* ---- End time ---- */
#define BOOKING_END_TIME_OFF  (BOOKING_START_TIME_OFF + BOOKING_START_TIME_LEN + FIELD_SEP_LEN)
#define BOOKING_END_TIME_LEN      TIME_LEN

/* ---- Staff ID ---- */
#define BOOKING_STAFF_ID_OFF (BOOKING_END_TIME_OFF + BOOKING_END_TIME_LEN + FIELD_SEP_LEN)
#define BOOKING_STAFF_ID_LEN   ID_LEN

/* ---- State ---- */
#define BOOKING_STATE_ID_OFF (BOOKING_STAFF_ID_OFF + BOOKING_STAFF_ID_LEN + FIELD_SEP_LEN)
#define BOOKING_STATE_ID_LEN        ID_LEN

/* ---- Job description ---- */
#define BOOKING_JOB_OFF (BOOKING_STATE_ID_OFF + BOOKING_STATE_ID_LEN + FIELD_SEP_LEN)
#define BOOKING_JOB_LEN           JOB_LEN

/* ---- Total record length  ---- */
#define BOOKING_RECORD_LEN (BOOKING_JOB_OFF + BOOKING_JOB_LEN)
#define BOOKING_DISK_LEN (BOOKING_RECORD_LEN + 1)

#endif /* DB_LAYOUT_H */

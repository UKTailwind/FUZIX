/*
 * db_staff_layout.h
 *
 * All constants in this file define on-disk record layout.
 * Changing any value here will invalidate existing data files.
 *
 * Every _LEN must be respected identically by:
 * formatter
 * parser
 * struct definitions
 * separator placement
 */

#ifndef DB_STAFF_LAYOUT_H
#define DB_STAFF_LAYOUT_H

#include "db_common_layout.h"

/* ============================================================
 * STAFF record layout
 * ============================================================ */
#define STAFF_STATUS_BASE_LEN   1
#define STAFF_NAME_BASE_LEN     25
#define STAFF_PHONE_BASE_LEN    12
#define STAFF_NOTES_BASE_LEN    60

/*
* STAFF Table Field order:
* staff_status|staff_ID|staff_Name|staff_phone|staff_notes\n
*/

/* ---- STAFF Status ---*/
#define STAFF_STATUS_OFF 0
#define STAFF_STATUS_LEN  STAFF_STATUS_BASE_LEN

/* ---- STAFF ID ---- */
#define STAFF_ID_OFF (STAFF_STATUS_OFF + STAFF_STATUS_LEN + FIELD_SEP_LEN)
#define STAFF_ID_LEN (ID_LEN)

/* ---- STAFF name ---- */
#define STAFF_NAME_OFF (STAFF_ID_OFF + STAFF_ID_LEN + FIELD_SEP_LEN)
#define STAFF_NAME_LEN STAFF_NAME_BASE_LEN

/* ---- STAFF_phone ---- */
#define STAFF_PHONE_OFF (STAFF_NAME_OFF + STAFF_NAME_LEN + FIELD_SEP_LEN)
#define STAFF_PHONE_LEN STAFF_PHONE_BASE_LEN

/* ---- STAFF_notes ---- */
#define STAFF_NOTES_OFF (STAFF_PHONE_OFF + STAFF_PHONE_LEN + FIELD_SEP_LEN)
#define STAFF_NOTES_LEN STAFF_NOTES_BASE_LEN

/* ---- Total record length ---- */
#define STAFF_RECORD_LEN (STAFF_NOTES_OFF + STAFF_NOTES_LEN)

/* Include '\n' */
#define STAFF_DISK_LEN (STAFF_RECORD_LEN + 1)

#endif /* DB_STAFF_LAYOUT_H */

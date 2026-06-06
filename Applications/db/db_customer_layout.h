/*
 * db_customer_layout.h
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

#ifndef DB_CUSTOMER_LAYOUT_H
#define DB_CUSTOMER_LAYOUT_H

#include "db_common_layout.h"

/* ============================================================
 * CUSTOMER record layout
 * ============================================================ */
#define CUSTOMER_STATUS_BASE_LEN   1
#define CUSTOMER_NAME_BASE_LEN     25
#define CUSTOMER_PHONE1_BASE_LEN   12
#define CUSTOMER_PHONE2_BASE_LEN   12
#define CUSTOMER_ADDRESS1_BASE_LEN 30
#define CUSTOMER_ADDRESS2_BASE_LEN 30
#define CUSTOMER_SUBURB_BASE_LEN   30
#define CUSTOMER_STATE_BASE_LEN    5
#define CUSTOMER_POSTCODE_BASE_LEN 6
#define CUSTOMER_NOTES_BASE_LEN    60

/*
* Customer Table Field order:
* customer_status|customer_ID|customer_Name|customer_phone1|customer_phone2|customer_address1|customer_address2|customer_subburb|customer_state|customer_postcode|customer_notes\n
*/

/* ---- Customer Status ---*/
#define CUSTOMER_STATUS_OFF 0
#define CUSTOMER_STATUS_LEN  CUSTOMER_STATUS_BASE_LEN

/* ---- Customer ID ---- */
#define CUSTOMER_ID_OFF (CUSTOMER_STATUS_OFF + CUSTOMER_STATUS_LEN + FIELD_SEP_LEN)
#define CUSTOMER_ID_LEN (ID_LEN)

/* ---- Customer name ---- */
#define CUSTOMER_NAME_OFF (CUSTOMER_ID_OFF + CUSTOMER_ID_LEN + FIELD_SEP_LEN)
#define CUSTOMER_NAME_LEN CUSTOMER_NAME_BASE_LEN

/* ---- customer_phone1 ---- */
#define CUSTOMER_PHONE1_OFF (CUSTOMER_NAME_OFF + CUSTOMER_NAME_LEN + FIELD_SEP_LEN)
#define CUSTOMER_PHONE1_LEN CUSTOMER_PHONE1_BASE_LEN

/* ---- customer_phone2 ---- */
#define CUSTOMER_PHONE2_OFF (CUSTOMER_PHONE1_OFF + CUSTOMER_PHONE1_LEN + FIELD_SEP_LEN)
#define CUSTOMER_PHONE2_LEN CUSTOMER_PHONE2_BASE_LEN

/* ---- customer_address1 ---- */
#define CUSTOMER_ADDRESS1_OFF (CUSTOMER_PHONE2_OFF + CUSTOMER_PHONE2_LEN + FIELD_SEP_LEN)
#define CUSTOMER_ADDRESS1_LEN CUSTOMER_ADDRESS1_BASE_LEN

/* ---- customer_address2 ---- */
#define CUSTOMER_ADDRESS2_OFF (CUSTOMER_ADDRESS1_OFF + CUSTOMER_ADDRESS1_LEN + FIELD_SEP_LEN)
#define CUSTOMER_ADDRESS2_LEN CUSTOMER_ADDRESS2_BASE_LEN

/* ---- customer_suburb ---- */
#define CUSTOMER_SUBURB_OFF (CUSTOMER_ADDRESS2_OFF + CUSTOMER_ADDRESS2_LEN + FIELD_SEP_LEN)
#define CUSTOMER_SUBURB_LEN CUSTOMER_SUBURB_BASE_LEN

/* ---- customer_state ---- */
#define CUSTOMER_STATE_OFF (CUSTOMER_SUBURB_OFF + CUSTOMER_SUBURB_LEN + FIELD_SEP_LEN)
#define CUSTOMER_STATE_LEN CUSTOMER_STATE_BASE_LEN

/* ---- customer_postcode ---- */
#define CUSTOMER_POSTCODE_OFF (CUSTOMER_STATE_OFF + CUSTOMER_STATE_LEN + FIELD_SEP_LEN)
#define CUSTOMER_POSTCODE_LEN CUSTOMER_POSTCODE_BASE_LEN

/* ---- customer_notes ---- */
#define CUSTOMER_NOTES_OFF (CUSTOMER_POSTCODE_OFF + CUSTOMER_POSTCODE_LEN + FIELD_SEP_LEN)
#define CUSTOMER_NOTES_LEN CUSTOMER_NOTES_BASE_LEN

/* ---- Total record length ---- */
#define CUSTOMER_RECORD_LEN (CUSTOMER_NOTES_OFF + CUSTOMER_NOTES_LEN)

/* Include '\n' */
#define CUSTOMER_DISK_LEN (CUSTOMER_RECORD_LEN + 1)

#endif /* DB_CUSTOMER_LAYOUT_H */

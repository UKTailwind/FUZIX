/*
 * db_state_layout.h
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

#ifndef DB_STATE_LAYOUT_H
#define DB_STATE_LAYOUT_H

#include "db_common_layout.h"

/* ============================================================
 * STATE record layout
 * ============================================================ */
#define STATE_ORDER_BASE_LEN    4
#define STATE_NAME_BASE_LEN     16

/*
 * State Table Field order:
 *  StateID | SortOrder | StatusName
 */

/* ---- State ID ---- */
#define STATE_ID_OFF   0
#define STATE_ID_LEN   ID_LEN

/* ---- Sort order ---- */
#define STATE_SORT_OFF (STATE_ID_OFF + STATE_ID_LEN + FIELD_SEP_LEN)
#define STATE_SORT_LEN STATE_ORDER_BASE_LEN

/* ---- State name ---- */
#define STATE_NAME_OFF (STATE_SORT_OFF + STATE_SORT_LEN + FIELD_SEP_LEN)
#define STATE_NAME_LEN STATE_NAME_BASE_LEN

/* ---- Total record length ---- */
#define STATE_RECORD_LEN (STATE_NAME_OFF + STATE_NAME_LEN)

/* Include '\n' */
#define STATE_DISK_LEN (STATE_RECORD_LEN + 1)

#endif /* DB_STATE_LAYOUT_H */

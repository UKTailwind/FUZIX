#ifndef STAFF_SELECT_H
#define STAFF_SELECT_H

#include <stddef.h>

/**
 * Display a small popup to select a booking staff member.
 * 
 * @param out_staff_id Buffer to receive selected state ID (BOOKING_STAFF_ID_MAX)
 * @return 0 if a selection was made, -1 if cancelled (Esc)
 */
int staff_select(char *out_staff_id);

#endif

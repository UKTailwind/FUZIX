#ifndef STATE_SELECT_H
#define STATE_SELECT_H

#include <stddef.h>

/**
 * Display a small popup to select a booking state.
 * 
 * @param out_state_id Buffer to receive selected state ID (BOOKING_STATE_ID_MAX)
 * @return 0 if a selection was made, -1 if cancelled (Esc)
 */
int state_select(char *out_state_id);

#endif

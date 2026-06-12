#--------------------------------------------------------------------------
# Execute this script to build the booking program to run on Linux
# The resulting executable has been tested on Debian.
# Use the included Makefile(s) when building for FUZIX. 
#--------------------------------------------------------------------------
rm -f *.o bin/booking

# To enable/disable debugging
#DEBUG_FLAGS="-DDEBUG_ENABLED"
#DEBUG_FLAGS=""

# This controls whether functions and constants become visible from headers
CFLAGS=-D_DEFAULT_SOURCE

# Build the dbgen program.
echo "Building dbgen program..."
gcc -std=c89 -Wall -Wextra -Werror -o bin/dbgen dbgen.c
echo "Building dbgen complete"

# Execute dbgen to build the layout files.
echo "Generating DB layouts..."
bin/dbgen BOOKING < booking-layout.def > db_booking_layout.h
bin/dbgen CUSTOMER < customer-layout.def > db_customer_layout.h
bin/dbgen STAFF < staff-layout.def > db_staff_layout.h
bin/dbgen STATE < state-layout.def > db_state_layout.h
echo "Generating DB layouts complete"

echo "Building booking program..."
gcc -std=c89 -Wno-long-long -Wall -Wextra -Werror $CFLAGS $DEBUG_FLAGS -o bin/booking main.c db_booking.c booking_list.c \
   booking_detail.c debug.c ui.c date.c db_lock.c db_state.c db_common.c customer_list.c db_customer.c keyboard_test.c\
   customer_detail.c state_select.c staff_select.c db_staff.c\
   ui_keyboard_parser.c term.c kb_definition_ansi.c kb_definition_vt52.c kb_backend_ansi.c kb_backend_vt52.c
echo "Building booking complete"

echo "Script Complete"

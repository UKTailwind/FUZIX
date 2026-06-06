#include "db_layout.h"
#include <stdio.h>

void db_layout_runtime_check(void)
{
    printf("=== Booking Record Layout Check ===\n");

    printf("Fields:\n");
    printf("BookingID        off=%d len=%d\n", BOOKING_ID_OFF, BOOKING_ID_LEN);
    printf("CustomerID       off=%d len=%d\n", BOOKING_CUSTOMER_ID_OFF, BOOKING_CUSTOMER_ID_LEN);
    printf("Date             off=%d len=%d\n", BOOKING_DATE_OFF, BOOKING_DATE_LEN);
    printf("StartTime        off=%d len=%d\n", BOOKING_START_TIME_OFF, BOOKING_START_TIME_LEN);
    printf("EndTime          off=%d len=%d\n", BOOKING_END_TIME_OFF, BOOKING_END_TIME_LEN);
    printf("MechanicID       off=%d len=%d\n", BOOKING_MECHANIC_ID_OFF, BOOKING_MECHANIC_ID_LEN);
    printf("Status           off=%d len=%d\n", BOOKING_STATUS_OFF, BOOKING_STATUS_LEN);
    printf("Job              off=%d len=%d\n", BOOKING_JOB_OFF, BOOKING_JOB_LEN);

    printf("\nCalculated totals:\n");
    printf("BOOKING_RECORD_LEN = %d\n", BOOKING_RECORD_LEN);
    printf("BOOKING_DISK_LEN   = %d (includes newline)\n", BOOKING_DISK_LEN);

    int awk_file_length = 88; /* from previous awk check */
    if (BOOKING_DISK_LEN != awk_file_length) {
        printf("!!! MISMATCH: BOOKING_DISK_LEN (%d) != on-disk record length (%d)\n",
               BOOKING_DISK_LEN, awk_file_length);
    } else {
        printf("✅ BOOKING_DISK_LEN matches on-disk record length (%d)\n", awk_file_length);
    }

    printf("=== End of layout check ===\n\n");
}

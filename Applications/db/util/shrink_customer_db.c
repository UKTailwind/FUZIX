#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "db_customer_layout.h"

/* ---- OLD layout (before change) ---- */
#define OLD_CUSTOMER_NOTES_LEN 76

#define OLD_CUSTOMER_RECORD_LEN \
    (CUSTOMER_NOTES_OFF + OLD_CUSTOMER_NOTES_LEN)

#define OLD_CUSTOMER_DISK_LEN \
    (OLD_CUSTOMER_RECORD_LEN + 1)

/* ---- NEW layout (from header) ---- */
/* CUSTOMER_DISK_LEN already defined */

int main(void)
{
    FILE *in  = fopen("customer.db", "rb");
    FILE *out = fopen("customer_new.db", "wb");

    if (!in || !out) {
        perror("fopen");
        return 1;
    }

    unsigned char oldrec[OLD_CUSTOMER_DISK_LEN];
    unsigned char newrec[CUSTOMER_DISK_LEN];

while (fread(oldrec, 1, OLD_CUSTOMER_DISK_LEN, in) == OLD_CUSTOMER_DISK_LEN) {

    memset(newrec, ' ', CUSTOMER_DISK_LEN);   /* <-- CRITICAL */

    memcpy(newrec, oldrec, CUSTOMER_RECORD_LEN);

    newrec[CUSTOMER_RECORD_LEN] = '\n';

    fwrite(newrec, 1, CUSTOMER_DISK_LEN, out);
}


    fclose(in);
    fclose(out);

    return 0;
}

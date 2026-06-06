#include "db.h"
#include "db_layout.h"
#include <stdio.h>
#include <string.h>

#include <fcntl.h>
#include <unistd.h>

int main(void)
{
    int fd;
    booking_t b, b2;
    long recno = 0;

    /* test harness – database will be truncated */
    fd = open("data/mechanic.db", O_CREAT | O_RDWR | O_TRUNC, 0644);
    if (fd < 0) {
        perror("open data/mechanic.db");
        return 1;
    }

    memset(&b, 0, sizeof(b));

    /* Test record 1 */
    strcpy(b.booking_id,  "001001");
    strcpy(b.customer_id, "002001");
    strcpy(b.mechanic_id, "003001");
    b.ymd = 20251228;
    b.start_min = 410;
    b.end_min   = 470;
    b.status    = 'B';
    strcpy(b.job, "Brake pads front");

    if (db_write_booking(fd, 0, &b) < 0) {
        perror("db_write_booking");
        return 1;
    }

    /* Test record 2 */
    strcpy(b.booking_id,  "001002");
    strcpy(b.customer_id, "002002");
    strcpy(b.mechanic_id, "003002");
    b.ymd = 20251228;
    b.start_min = 510;
    b.end_min   = 570;
    b.status    = 'B';
    strcpy(b.job, "Brake pads rear");

    if (db_write_booking(fd, 1, &b) < 0) {
        perror("db_write_booking");
        return 1;
    }

    /* Test record 3 */
    strcpy(b.booking_id,  "001003");
    strcpy(b.customer_id, "002003");
    strcpy(b.mechanic_id, "003003");
    b.ymd = 20251228;
    b.start_min = 610;
    b.end_min   = 670;
    b.status    = 'B';
    strcpy(b.job, "Replace Radiator");

    if (db_write_booking(fd, 2, &b) < 0) {
        perror("db_write_booking");
        return 1;
    }

    memset(&b2, 0, sizeof(b2));

    printf("Reading all records:\n");
    while (db_read_booking(fd, recno, &b2) == 0) {
        printf("Record %ld:\n", recno);
        printf("  booking_id  = %s\n", b.booking_id);
        printf("  customer_id = %s\n", b.customer_id);
        printf("  mechanic_id = %s\n", b.mechanic_id);
        printf("  date        = %d\n", b.ymd);
        printf("  start_min   = %d\n", b.start_min);
        printf("  end_min     = %d\n", b.end_min);
        printf("  status      = %c\n", b.status);
        printf("  job         = %s\n", b.job);
        printf("\n");
        recno++;
    }

    close(fd);
    return 0;
}


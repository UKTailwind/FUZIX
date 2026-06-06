#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>      /* for printf / debug_log if needed */
#include <stdlib.h>     /* just in case                     */
#include "debug.h"      /* debug_log macro                  */
#include "db_common.h"

int db_validate_fixed_records(int fd, size_t record_len, const char *dbname)
{
    debug_log(DEBUG_TRACE, FUNC_NAME, "Enter: ");
    debug_log(DEBUG_TRACE, FUNC_NAME, "Expected record_len=%d", record_len);

    unsigned char buf[record_len];
    off_t offset = 0;
    long recno = 1;

    while (1) {
        ssize_t n = read(fd, buf, record_len);

        if (n == 0) {
            /* clean EOF */
            return 0;
        }

        if (n != (ssize_t)record_len) {
            debug_log(DEBUG_ERROR, FUNC_NAME,
                "%s: short record at record %ld "
                "(read %zd bytes, expected %zu, offset=%ld)",
                dbname, recno, n, record_len, (long)offset);
            return -1;
        }

        if (buf[record_len - 1] != '\n') {
            debug_log(DEBUG_ERROR, FUNC_NAME,
                "%s: short record or missing newline at or near record %ld (offset=%ld)", dbname, recno, (long)offset);
            return -1;
        }
        (void)dbname;  /*dbname is only used with debugging. This causes a compile time error when debugging is disabled */

        recno++;
        offset += record_len;
    }
}

/* Copy a record ensuring it ends zero terminated and any space
   unused in the record is clear, Note that the record buffer is 1 byte
   longer than the size given. */
void copy_record(void *to, const void *from, size_t length)
{
    char *tp = to;	/* Be strict about void * and char * */
    strncpy(to, from, length);
    tp[length] = 0;
}

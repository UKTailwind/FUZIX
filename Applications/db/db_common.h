/* db_common.h */
#ifndef DB_COMMON_H
#define DB_COMMON_H

#include <stddef.h>

int db_validate_fixed_records(int fd, size_t record_len, const char *dbname);

/* Safe fixed-field copy */
#define SAFE_COPY(dest, src, len)       \
    do {                               \
        strncpy((dest), (src), (len)); \
        (dest)[(len)] = '\0';          \
    } while (0)

#endif

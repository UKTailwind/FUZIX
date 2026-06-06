/* db_common.h */
#ifndef DB_COMMON_H
#define DB_COMMON_H

#include <stddef.h>

int db_validate_fixed_records(int fd, size_t record_len, const char *dbname);

/* Safe fixed-field copy */
/* The passed len is one less than the actual in memory size. This is
   possibly something that should change */
void copy_record(void *to, const void *from, size_t len);

#endif

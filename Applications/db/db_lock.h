/* db_lock.h */
#ifndef DB_LOCK_H
#define DB_LOCK_H

int db_lock_shared(const char *filename);
int db_lock_exclusive(const char *filename);
int db_unlock(const char *filename);

#endif

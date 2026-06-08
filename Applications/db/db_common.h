/* db_common.h */
#ifndef DB_COMMON_H
#define DB_COMMON_H

#include <stddef.h>

struct dbase {
   char *name;			/* Name of database */
   int fd;			/* File handle or -1 */
   unsigned lock;		/* 0 LOCK_SH LOCK_EX */
   unsigned reclen;		/* Record length */
   unsigned pos;		/* Current record number */
   unsigned base;		/* Offset of records from start of file */
   char *buf;			/* Pointer to buffer to use */
};

/* Set up access to a dabase */
/* reclen can go away once we make the database have a header */
struct dbase *db_setup(const char *name, unsigned reclen);
/* Open the database */
int db_open(struct dbase *db, unsigned rw);
/* Close it */
int db_close(struct dbase *d);
/* Read a record into the internal buffer */
int db_read(struct dbase *db, unsigned record);
/* Write a record to the internal buffer */
int db_write(struct dbase *db, unsigned record);

/* Safe fixed-field copy */
/* The passed len is one less than the actual in memory size. This is
   possibly something that should change */
void copy_record(void *to, const void *from, size_t len);

#endif

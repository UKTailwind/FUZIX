#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>      /* for printf / debug_log if needed */
#include <stdlib.h>     /* just in case                     */
#include <string.h>
#include <fcntl.h>
#include <sys/file.h>
#include "debug.h"      /* debug_log macro                  */
#include "db_common.h"

/* Really we need some kind of error codes here */
struct dbase *db_setup(const char *name, unsigned reclen)
{
    struct stat st;
    struct dbase *db;
    /* Allocate the dbase, the name and a record buffer */
    db = malloc(sizeof(struct dbase) + strlen(name) + 1 + reclen);
    if (db == NULL)
        return NULL;
    db->buf = (char *)(db + 1);
    db->name = db->buf + reclen;
    strcpy(db->name, name);
    db->lock = 0;
    db->base = 0;	/* No headers yet */
    db->pos = 0;
    db->reclen = reclen;
    db->fd = open(name, O_RDONLY);
    if (db->fd == -1)
        goto fail;
    if (fstat(db->fd, &st) == -1)
        goto fail2;
    if (st.st_size % reclen) {
         fprintf(stderr, "Length error %u v %u\n", st.st_size, reclen);
         goto fail2;
    }
    /* We will one day read the headers and do stuff here */
    close(db->fd);
    db->fd = -1;
    return db;
    /* Errors */
fail2:
    close(db->fd);
fail:
    free(db);
    return NULL;
}

int db_open(struct dbase *db, unsigned rw)
{
    if (db->fd != -1)
        close(db->fd);
    db->fd = open(db->name, rw ? O_RDWR : O_RDONLY);
    if (db->fd == -1) {
        fprintf(stderr, "open fail '%s'\n", db->name);
        return -1;
    }
    if (flock(db->fd, rw ? LOCK_EX : LOCK_SH) == -1) {
        perror("flock");
        close(db->fd);
        db->fd = -1;
        return -1;
    }
    return 0;
}

int db_close(struct dbase *db)
{
    fsync(db->fd);
    close(db->fd);
    db->fd = -1;
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

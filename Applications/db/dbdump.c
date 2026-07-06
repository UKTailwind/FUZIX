/*
 *	Given a textual description of the database records output the
 *	matching header file. This avoids having 10-20 layers of recursive
 *	defines as we did before, and which gives cpp on a 64K RAM machine
 *	some headaches
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "db_common_layout.h"

struct dbfield {
    const char *name;
    struct dbfield *next;
    unsigned offset;
    unsigned size;
    /* Type info would be nice */
};

struct dbdef {
    struct dbfield *field;
    unsigned reclen;
};

static void oom(void)
{
    fprintf(stderr, "out of memory.\n");
    exit(EXIT_FAILURE);
}

static void db_load_def(struct dbdef *db, const char *path)
{
    char buf[256];
    char *n, *s;
    int v;
    unsigned total = 0;
    struct dbfield *df;
    struct dbfield *last = NULL;

    FILE *fp = fopen(path, "r");

    if (fp == NULL) {
        perror(path);
        exit(1);
    }

    while(fgets(buf, 256, fp)) {
        /* Format is NAME SIZE */
        if (*buf == '#')
            continue;
        n = strtok(buf, " \t");
        /* Line was just spaces */
        if (n == NULL)
            continue;
        s = strtok(NULL, " \t\n");
        if (s == NULL) {
            fprintf(stderr, "Field '%s' needs a size.\n", n);
            exit(EXIT_FAILURE);
        }
        v = atoi(s);
        if (v < 1) {
            fprintf(stderr, "Field '%s', size '%s' is not valid.\n", n, s);
            exit(EXIT_FAILURE);
        }
        df = malloc(sizeof(struct dbfield));
        if (df == NULL)
            oom();
        df->next = NULL;
        df->name = strdup(n);
        if (df->name == NULL)
            oom();
        df->size = v;
        df->offset = total;
        total += v + FIELD_SEP_LEN; 
        if (last)
            last->next = df;
        else
            db->field = df;
        last = df;
    }
    db->reclen = total;
    fclose(fp);
}
    
int main(int argc, char *argv[])
{
    FILE *dbf;
    char *buf;
    struct dbdef db;
    if (argc != 3) {
        fprintf(stderr, "%s dbase layout\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    
    db_load_def(&db, argv[2]);

    dbf = fopen(argv[1], "r");
    if (dbf == NULL) {
        perror(argv[1]);
        exit(EXIT_FAILURE);
    }

    buf = malloc(db.reclen);
    if (buf == NULL)
        oom();

    while(fread(buf, db.reclen, 1, dbf) == 1) {
        struct dbfield *df = db.field;
        while(df) {
            printf("%s: %.*s (%d)\n", df->name, df->size, buf + df->offset, df->size);
            df = df->next;
        }
        printf("---\n");
    }

    fclose(dbf);
    return 0;
}

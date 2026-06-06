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

int main(int argc, char *argv[])
{
    char buf[256];
    char *n, *s;
    int v;
    unsigned total = 0;
    if (argc != 2) {
        fprintf(stderr, "%s layout-name\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    printf("#include \"db_common_layout.h\"\n\n");
    while(fgets(buf, 256, stdin)) {
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
        printf("#define %s_%s_BASE_LEN %d\n", argv[1], n, v);
        printf("#define %s_%s_OFF %d\n", argv[1], n, total);
        printf("#define %s_%s_LEN %d\n", argv[1], n, v);
        total += v + FIELD_SEP_LEN;
    }
    printf("#define %s_RECORD_LEN %u\n", argv[1], total);
    printf("#define %s_DISK_LEN %u\n", argv[1], total + 1);
    return 0;
}

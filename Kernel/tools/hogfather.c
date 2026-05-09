#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct sym {
    struct sym *next;
    char name[16];
    unsigned addr;
    unsigned size;
};

struct symlist {
    struct sym *head;
    struct sym *tail;
};

struct symlist symtab[10];

/*
 *	Find memory hogs in a sorted symbol list
 */

static unsigned ignore_sym(const char *p)
{
    if(strcmp(p, "__code") == 0)
        return 1;
    if(strcmp(p, "__code_size") == 0)
        return 1;
    if(strcmp(p, "__data") == 0)
        return 1;
    if(strcmp(p, "__data_size") == 0)
        return 1;
    if(strcmp(p, "__bss") == 0)
        return 1;
    if(strcmp(p, "__bss_size") == 0)
        return 1;
    if(strcmp(p, "__discard") == 0)
        return 1;
    if(strcmp(p, "__discard_size") == 0)
        return 1;
    if(strcmp(p, "__literal") == 0)
        return 1;
    if(strcmp(p, "__literal_size") == 0)
        return 1;
    if(strcmp(p, "__buffers") == 0)
        return 1;
    if(strcmp(p, "__buffers_size") == 0)
        return 1;
    if(strcmp(p, "__common") == 0)
        return 1;
    if(strcmp(p, "__common_size") == 0)
        return 1;
    if(strcmp(p, "__commondata") == 0)
        return 1;
    if(strcmp(p, "__commondata_siz") == 0)
        return 1;
    if(strcmp(p, "__zp") == 0)
        return 1;
    if(strcmp(p, "__zp_size") == 0)
        return 1;
    return 0;
}

static unsigned symnum(char c)
{
    static char *p = "ACDBZXSLsb";
    char *x = strchr(p, c);
    if (x == NULL)
        return -1;
    return x - p;
}

static void insert(char c, struct sym *s)
{
    int l = symnum(c);
    if (l == -1 ) {
        fprintf(stderr, "Unknown segment '%c'.\n", c);
        exit(1);
    }
    s->next = NULL;
    if (symtab[l].head)
        symtab[l].tail->next = s;
    else
        symtab[l].head = s;
    symtab[l].tail = s;
}

int load_symbols(FILE *f)
{
    struct sym *s;
    char buf[128];
    char c;

    while(fgets(buf, 128, f)) {
        s = malloc(sizeof(struct sym));
        if (s == NULL) {
            fprintf(stderr, "Out of  memory.\n");
            exit(1);
        }
        if (sscanf(buf, "%X %c %15s\n", &s->addr, &c, s->name) != 3) {
            fprintf(stderr, "invalid map data: %s\n", buf);
            exit(1);
        }
        if (ignore_sym(s->name)) {
            printf("Ignoring %s\n", s->name);
            free(s);
            continue;
        }
        insert(c, s);
    }
}

static void size_symbols(void)
{
    unsigned i;
    struct sym *s;
    for (i = 0; i < 10; i++) {
        s = symtab[i].head;
        while(s && s->next) {
            s->size = s->next->addr - s->addr;
            s = s->next;
        }
    }
}   

static void dump_sizes(void)
{
    unsigned i;
    struct sym *s;
    for (i = 0; i < 10; i++) {
        s = symtab[i].head;
        while(s) {
            printf("%d %c %s\n", s->size, "ACDBZXSLsb"[i], s->name);
            s = s->next;
        }
    }
}

int main(int argc, char *argv[])
{
    FILE *fp;
    if (argc != 2) {
        fprintf(stderr, "%s mapfile.\n", argv[0]);
        exit(1);
    }
    fp = fopen(argv[1], "r");
    if (fp == NULL) {
        perror(argv[1]);
        exit(1);
    }
    load_symbols(fp);
    fclose(fp);
    size_symbols();
    dump_sizes();
}

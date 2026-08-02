/* mmbc_out.c - the output side: global_decls, report and write.
 * Mirrors mmb2c.py 3233-3392.  Everywhere output depends on iterating
 * a table the Python sorts the names first; the two unsorted walks
 * (implied, skipped, data) are lists = append order. */

#include "mmbc.h"

static int cmpstr(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/* Sorted copy of the global names (scratch). */
static const char **global_names_sorted(int *np)
{
    const char **names = salloc(sizeof(char *)
                                * (size_t)(cv.nglobals ? cv.nglobals : 1));
    int k;

    for (k = 0; k < cv.nglobals; k++)
        names[k] = cv.globals[k]->name;
    qsort(names, (size_t)cv.nglobals, sizeof(char *), cmpstr);
    *np = cv.nglobals;
    return names;
}

static const char **routine_names_sorted(int *np)
{
    const char **names = salloc(sizeof(char *)
                                * (size_t)(cv.nroutines ? cv.nroutines : 1));
    int k;

    for (k = 0; k < cv.nroutines; k++)
        names[k] = cv.routines[k]->name;
    qsort(names, (size_t)cv.nroutines, sizeof(char *), cmpstr);
    *np = cv.nroutines;
    return names;
}

static struct sym *globals_get(const char *canon)
{
    int k;
    for (k = 0; k < cv.nglobals; k++)
        if (strcmp(cv.globals[k]->name, canon) == 0)
            return cv.globals[k];
    return NULL;
}

static void ob_add(struct outbuf *o, const char *line)
{
    out_append(o, pstr(line));
}

/* Returns the declaration lines for the non-const globals (scratch
 * outbuf, persistent lines), sorted by name. */
static void global_decls(struct outbuf *o)
{
    int n, k, d;
    const char **names = global_names_sorted(&n);

    for (k = 0; k < n; k++) {
        struct sym *s = globals_get(names[k]);
        const char *note = "";
        char *dims;

        if (s->is_const)
            continue;
        if (s->implied)
            note = sfmt("   /* implied, first seen line %d */", s->where);
        if (s->is_array) {
            dims = sstr("");
            for (d = 0; d < s->ndims; d++)
                dims = sfmt("%s[%s]", dims, s->dims[d]);
            if (s->ty == TY_S)
                ob_add(o, sfmt("char %s%s[MM_STRSZ];%s", s->acc, dims,
                               note));
            else
                ob_add(o, sfmt("%s %s%s;%s", ctype_of(s->ty), s->acc,
                               dims, note));
        } else if (s->ty == TY_S) {
            ob_add(o, sfmt("char %s[MM_STRSZ];%s", s->acc, note));
        } else {
            ob_add(o, sfmt("%s %s;%s", ctype_of(s->ty), s->acc, note));
        }
    }
}

/* The --report text; also the top-of-file comment block. */
void report_build(struct outbuf *o)
{
    int k, n, j;

    ob_add(o, "Implied global variables (created by first use, never");
    ob_add(o, "declared with DIM):");
    if (cv.nimplied == 0) {
        ob_add(o, "    (none - every variable was declared)");
    } else {
        for (k = 0; k < cv.nimplied; k++) {
            struct implied_rec *r = &cv.implied[k];
            struct routine *rt;
            struct sym *g;
            const char *rd, *where;
            int seen = 0;
            for (j = 0; j < k; j++)
                if (strcmp(cv.implied[j].name, r->name) == 0) {
                    seen = 1;
                    break;
                }
            if (seen)
                continue;
            rt = routine_get(r->routine);
            rd = rt != NULL ? rt->disp : r->routine;
            where = r->routine[0] ? sfmt("in %s", rd)
                                  : "in the main program";
            g = globals_get(r->name);
            ob_add(o, sfmt("    %-20s %-8s first used line %-5d %s",
                           g != NULL ? g->disp : r->name,
                           tyname_of(r->ty), r->line, where));
        }
    }
    if (cv.nskipped > 0) {
        ob_add(o, "");
        ob_add(o, "Lines that could not be translated.  Each is left in");
        ob_add(o, "the C as a comment; nothing was emitted for it, so");
        ob_add(o, "check the surrounding logic still makes sense:");
        for (k = 0; k < cv.nskipped; k++) {
            ob_add(o, sfmt("    line %-5d %.60s", cv.skipped[k].line,
                           cv.skipped[k].text));
            ob_add(o, sfmt("              -> %.64s", cv.skipped[k].why));
        }
    }
    /* every global reached from inside a routine is worth a look */
    {
        const char **rn = routine_names_sorted(&n);
        int any_touch = 0;
        struct outbuf body;

        memset(&body, 0, sizeof(body));
        for (k = 0; k < n; k++) {
            struct routine *r = routine_get(rn[k]);
            const char **keys;

            if (r->ngtouch == 0)
                continue;
            any_touch = 1;
            ob_add(&body, sfmt("    %s %s (line %d):",
                               r->is_func ? "FUNCTION" : "SUB     ",
                               r->disp, r->line));
            keys = salloc(sizeof(char *) * (size_t)r->ngtouch);
            for (j = 0; j < r->ngtouch; j++)
                keys[j] = r->gtouch[j].name;
            qsort(keys, (size_t)r->ngtouch, sizeof(char *), cmpstr);
            for (j = 0; j < r->ngtouch; j++) {
                struct sym *g = globals_get(keys[j]);
                const char *tag = (g != NULL && g->implied) ? "implied"
                                                            : "DIMmed";
                int line = 0;
                int m;
                for (m = 0; m < r->ngtouch; m++)
                    if (strcmp(r->gtouch[m].name, keys[j]) == 0) {
                        line = r->gtouch[m].line;
                        break;
                    }
                ob_add(&body, sfmt("        %-20s %-8s %-8s used line %d",
                                   g != NULL ? g->disp : keys[j],
                                   g != NULL ? tyname_of(g->ty) : "?",
                                   tag, line));
            }
        }
        if (any_touch) {
            ob_add(o, "");
            ob_add(o, "Globals reached from inside a SUB or FUNCTION.");
            ob_add(o, "Anything marked \"implied\" was never DIMmed and is");
            ob_add(o, "shared with the whole program - check that a LOCAL");
            ob_add(o, "was not what you meant:");
            for (k = 0; k < body.n; k++)
                ob_add(o, body.lines[k]);
        }
    }
}

static int cmpbnd(const void *a, const void *b)
{
    const struct bnd *x = a, *y = b;
    int c = strcmp(x->name, y->name);
    if (c)
        return c;
    return strcmp(x->body, y->body);
}

void conv_write(FILE *f)
{
    int k, n;
    const char **names;
    struct outbuf rep, gd;

    memset(&rep, 0, sizeof(rep));
    memset(&gd, 0, sizeof(gd));

    fprintf(f, "/* Generated by mmb2c.py v%s from %s */\n", VERSION,
            cv.srcname);
    fprintf(f, "/*\n");
    report_build(&rep);
    for (k = 0; k < rep.n; k++)
        fprintf(f, " * %s\n", rep.lines[k]);
    for (k = 0; k < cv.nwarnings; k++)
        fprintf(f, " * warning: %s\n", cv.warnings[k]);
    fprintf(f, " */\n\n");
    fprintf(f, "#include \"mmb_runtime.h\"\n");
    /* The geometry primitives are static functions in a header, so they
       land in the program rather than in bcrun - and only the ones it
       calls, because cc1 drops a static nothing names.  One flag for
       the whole header: it is included when the program uses any of
       them, and the compiler sorts out which. */
    if (cv.uses_gfx)
        fprintf(f, "#include \"mmb_gfx.h\"\n");
    fprintf(f, "#include <math.h>\n");
    fprintf(f, "#include <string.h>\n");
    fprintf(f, "#include <stdlib.h>\n\n");
    fprintf(f, "/* ---- constants ---- */\n");
    names = global_names_sorted(&n);
    for (k = 0; k < n; k++) {
        struct sym *s = globals_get(names[k]);
        if (s->is_const)
            fprintf(f, "#define %s %s\n", cvar(names[k]), s->acc);
    }
    fprintf(f, "\n/* ---- global variables ---- */\n");
    global_decls(&gd);
    for (k = 0; k < gd.n; k++)
        fprintf(f, "%s\n", gd.lines[k]);
    if (cv.nbnds > 0) {
        struct bnd *tabs = salloc(sizeof(struct bnd) * (size_t)cv.nbnds);
        fprintf(f, "\n/* ---- array bounds tables (FCC has no compound"
                   " literals) ---- */\n");
        memcpy(tabs, cv.bnds, sizeof(struct bnd) * (size_t)cv.nbnds);
        qsort(tabs, (size_t)cv.nbnds, sizeof(struct bnd), cmpbnd);
        for (k = 0; k < cv.nbnds; k++)
            fprintf(f, "static const MMINTEGER %s[] = { %s };\n",
                    tabs[k].name, tabs[k].body);
    }
    if (cv.ndata > 0) {
        fprintf(f, "\n/* ---- DATA items: parallel primitive arrays, "
                   "so no\n");
        fprintf(f, " * struct layout crosses the bcrun VM boundary "
                   "---- */\n");
        fprintf(f, "static const int __mmb_data_kind[] = {\n");
        for (k = 0; k < cv.ndata; k++)
            fprintf(f, "    %d,\n", cv.data[k].kind);
        fprintf(f, "};\n");
        fprintf(f, "static const MMFLOAT __mmb_data_f[] = {\n");
        for (k = 0; k < cv.ndata; k++)
            fprintf(f, "    %s,\n", cv.data[k].f);
        fprintf(f, "};\n");
        fprintf(f, "static const MMINTEGER __mmb_data_i[] = {\n");
        for (k = 0; k < cv.ndata; k++)
            fprintf(f, "    %s,\n", cv.data[k].i);
        fprintf(f, "};\n");
        fprintf(f, "static const char *__mmb_data_s[] = {\n");
        for (k = 0; k < cv.ndata; k++)
            fprintf(f, "    %s,\n", cv.data[k].sv);
        fprintf(f, "};\n");
    }
    fprintf(f, "\n/* ---- forward declarations ---- */\n");
    if (cv.uses_clear)
        fprintf(f, "static void __mmb_clear(void);\n");
    names = routine_names_sorted(&n);
    for (k = 0; k < n; k++)
        fprintf(f, "%s;\n", signature(routine_get(names[k])));
    if (cv.uses_clear) {
        fprintf(f, "\nstatic void __mmb_clear(void)\n{\n");
        names = global_names_sorted(&n);
        for (k = 0; k < n; k++) {
            struct sym *s = globals_get(names[k]);
            if (s->is_const)
                continue;
            fprintf(f, "    %s\n", zero_of(s));
        }
        fprintf(f, "}\n");
    }
    fprintf(f, "\n/* ---- subroutines and functions ---- */\n");
    for (k = 0; k < cv.out_body.n; k++)
        fprintf(f, "%s\n", cv.out_body.lines[k]);
    fprintf(f, "\n/* ---- main program ---- */\n");
    fprintf(f, "int main(void)\n{\n");
    fprintf(f, "    unsigned __mark = mm_mark(); (void)__mark;\n");
    if (cv.ndata > 0)
        fprintf(f, "    mm_data_init4(__mmb_data_kind, __mmb_data_f, "
                   "__mmb_data_i, __mmb_data_s, %d);\n", cv.ndata);
    for (k = 0; k < cv.out_main.n; k++)
        fprintf(f, "%s\n", cv.out_main.lines[k]);
    fprintf(f, "    return 0;\n}\n");
}

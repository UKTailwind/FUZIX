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
 * outbuf, persistent lines), sorted by name.
 *
 * Scalars in the process image, arrays and strings on the heap.  This
 * is how an interpreted BASIC has always done it - a fixed area for
 * simple variables, everything bulky in the heap - and on this machine
 * it is also what the memory wants.  Scalars are hot: loop counters
 * touched every iteration, and SRAM is 3.7x faster than PSRAM (44MB/s
 * against 12, measured with psbench).  Arrays and strings are bulk,
 * walked sequentially, and they are what was filling bcrun's 48K of VM
 * address space - a 38,400 byte array does not fit in it at all.
 *
 * One struct, one allocation.  So one free at exit with nothing to tidy
 * up, no fragmentation, and sizeof does the sizing - there is no
 * hand-computed maximum to drift out of step with the declarations as
 * they change.
 *
 * Every array bound here is a compile-time constant (mmbc rejects a
 * runtime bound by name), so sizeof covers everything that compiles.
 * When variable bounds do arrive they want a growable tail after the
 * fixed members, extended by realloc - which works precisely because
 * access goes through H and the block may move.
 *
 * The member names come from cvar(), not s->acc: heap_fixup has already
 * rewritten acc to "H->name" for exactly these symbols.
 */
static void global_decls(struct outbuf *o)
{
    int n, k, d;
    const char **names = global_names_sorted(&n);
    struct outbuf heap;

    memset(&heap, 0, sizeof(heap));
    for (k = 0; k < n; k++) {
        struct sym *s = globals_get(names[k]);
        const char *note = "";
        const char *cn;
        char *dims;

        if (s->is_const)
            continue;
        cn = cvar(s->name);
        if (s->implied)
            note = sfmt("   /* implied, first seen line %d */", s->where);
        if (s->stype != NULL) {
            dims = sstr("");
            if (s->is_array)
                for (d = 0; d < s->ndims; d++)
                    dims = sfmt("%s[%s]", dims, s->dims[d]);
            ob_add(&heap, sfmt("struct t_%s %s%s;%s", s->stype, cn,
                               dims, note));
        } else if (s->is_array) {
            dims = sstr("");
            for (d = 0; d < s->ndims; d++)
                dims = sfmt("%s[%s]", dims, s->dims[d]);
            if (s->ty == TY_S)
                ob_add(&heap, sfmt("char %s%s[MM_STRSZ];%s", cn, dims,
                                   note));
            else
                ob_add(&heap, sfmt("%s %s%s;%s", ctype_of(s->ty), cn,
                                   dims, note));
        } else if (s->ty == TY_S) {
            ob_add(&heap, sfmt("char %s[MM_STRSZ];%s", cn, note));
        } else {
            ob_add(o, sfmt("%s %s;%s", ctype_of(s->ty), cn, note));
        }
    }
    cv.heap_used = heap.n > 0;
    if (heap.n > 0) {
        ob_add(o, "");
        ob_add(o, "/* Arrays and strings: one block, allocated once"
                  " from the PSRAM heap. */");
        ob_add(o, "struct mm_vars {");
        for (k = 0; k < heap.n; k++)
            ob_add(o, sfmt("    %s", heap.lines[k]));
        ob_add(o, "};");
        ob_add(o, "static struct mm_vars *H;");
    }
}

/* One struct per routine that has LOCAL arrays or strings.
 *
 * At file scope rather than inside the function, because the FCC view
 * is C89 and a type declared in a block would be a different type in
 * every translation the compiler sees.  Routines come out in sorted
 * order for the same reason the globals do: the C must not depend on
 * the host Python's dictionary ordering. */
static void local_structs(struct outbuf *o)
{
    int n, k, j, d;
    const char **names = routine_names_sorted(&n);

    for (k = 0; k < n; k++) {
        struct routine *r = routine_get(names[k]);
        if (r == NULL || !r->heap_locals)
            continue;
        ob_add(o, "");
        ob_add(o, sfmt("/* LOCAL arrays and strings of %s: one block per"
                       " invocation. */", r->disp));
        ob_add(o, sfmt("struct mm_l_%s {", r->cname));
        for (j = 0; j < r->nlocal_order; j++) {
            struct sym *s = NULL;
            const char *cn;
            char *dims;
            int q;
            for (q = 0; q < r->nlocals; q++)
                if (strcmp(r->locals[q]->name, r->local_order[j]) == 0) {
                    s = r->locals[q];
                    break;
                }
            if (s == NULL || s->is_param || s->is_static)
                continue;
            if (!(s->is_array || s->ty == TY_S || s->stype != NULL))
                continue;
            cn = cvar(s->name);
            if (s->stype != NULL) {
                dims = sstr("");
                if (s->is_array)
                    for (d = 0; d < s->ndims; d++)
                        dims = sfmt("%s[%s]", dims, s->dims[d]);
                ob_add(o, sfmt("    struct t_%s %s%s;", s->stype, cn,
                               dims));
            } else if (s->is_array) {
                dims = sstr("");
                for (d = 0; d < s->ndims; d++)
                    dims = sfmt("%s[%s]", dims, s->dims[d]);
                if (s->ty == TY_S)
                    ob_add(o, sfmt("    char %s%s[MM_STRSZ];", cn, dims));
                else
                    ob_add(o, sfmt("    %s %s%s;", ctype_of(s->ty), cn,
                                   dims));
            } else {
                ob_add(o, sfmt("    char %s[MM_STRSZ];", cn));
            }
        }
        ob_add(o, "};");
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
    /* The geometry primitives are static functions in headers, so they
       land in the program rather than in bcrun - one header per
       primitive, one flag per header, because cc1's dead-static rule
       counts names rather than reachability and cannot drop a
       recursive primitive an included header carries.  The include IS
       the granularity, so it must be exact. */
    if (cv.uses_circle)
        fprintf(f, "#include \"mmb_gfx_circle.h\"\n");
    if (cv.uses_box)
        fprintf(f, "#include \"mmb_gfx_box.h\"\n");
    if (cv.uses_rbox)
        fprintf(f, "#include \"mmb_gfx_rbox.h\"\n");
    if (cv.uses_triangle)
        fprintf(f, "#include \"mmb_gfx_triangle.h\"\n");
    if (cv.uses_arc)
        fprintf(f, "#include \"mmb_gfx_arc.h\"\n");
    if (cv.uses_text)
        fprintf(f, "#include \"mmb_gfx_text.h\"\n");
    if (cv.uses_mappal)
        fprintf(f, "#include \"mmb_gfx_map.h\"\n");
    if (cv.uses_gpio)
        fprintf(f, "#include \"mmb_gpio.h\"\n");
    /* After mmb_gpio.h, which it uses to read the pins.  Only a program
       that arms an interrupt carries any of it. */
    if (cv.uses_interrupts)
        fprintf(f, "#include \"mmb_int.h\"\n");
    if (cv.uses_pwm)
        fprintf(f, "#include \"mmb_pwm.h\"\n");
    if (cv.uses_i2c)
        fprintf(f, "#include \"mmb_i2c.h\"\n");
    if (cv.uses_spi)
        fprintf(f, "#include \"mmb_spi.h\"\n");
    fprintf(f, "#include <math.h>\n");
    fprintf(f, "#include <string.h>\n");
    fprintf(f, "#include <stdlib.h>\n\n");
    /* PLAY VOLUME sets this and every later PLAY passes it on, which is
       what makes the volume stick across statements the way MMBasic's
       does.  Emitted only when the program plays something, so nothing
       else carries it - the same bargain as the two headers above. */
    if (cv.uses_play)
        fprintf(f, "static int mm_play_volume = 80;\n\n");
    if (cv.ntypes > 0) {
        int j;
        fprintf(f, "/* ---- TYPE definitions: the firmware layout,"
                   " byte for byte (TYPE-SPEC.md).\n");
        fprintf(f, " * Numeric members start 8-aligned, strings are"
                   " packed, a nested member\n");
        fprintf(f, " * always starts 8-aligned - the explicit pads"
                   " carry the difference\n");
        fprintf(f, " * where C alignment alone would not. ---- */\n");
        for (k = 0; k < cv.ntypes; k++) {
            struct typedef_rec *td = cv.types[k];
            long long pos = 0;
            int padn = 0;
            fprintf(f, "struct t_%s {\n", td->name);
            for (j = 0; j < td->nmembers; j++) {
                struct typemember *m = td->members[j];
                const char *decl;
                if (m->offset > pos) {
                    fprintf(f, "    unsigned char __p%d[%ld];\n",
                            padn, (long)(m->offset - pos));
                    padn++;
                }
                if (m->stype != NULL) {
                    decl = sfmt("struct t_%s m_%s", m->stype, m->name);
                    if (m->has_dims)
                        decl = sfmt("%s[%ld]", decl, (long)m->count);
                } else if (m->ty == TY_S) {
                    decl = sfmt("char m_%s[%ld]", m->name,
                                (long)(m->esize * m->count));
                } else {
                    decl = sfmt("%s m_%s", ctype_of(m->ty), m->name);
                    if (m->has_dims)
                        decl = sfmt("%s[%ld]", decl, (long)m->count);
                }
                fprintf(f, "    %s;\n", decl);
                pos = m->offset + m->esize * m->count;
            }
            fprintf(f, "};    /* %ld bytes */\n", (long)td->total);
        }
        fprintf(f, "\n");
    }
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
    {
        struct outbuf ls;
        memset(&ls, 0, sizeof(ls));
        local_structs(&ls);
        for (k = 0; k < ls.n; k++)
            fprintf(f, "%s\n", ls.lines[k]);
    }
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
    if (cv.uses_onerror) {
        fprintf(f, "\n/* ---- ON ERROR state, read by the guards below ---- *\n");
        fprintf(f, " * [0] is the poison: an error has been recorded and the\n");
        fprintf(f, " * rest of this statement is skipped.  [1] is the skip\n");
        fprintf(f, " * count, MMBasic's OptionErrorSkip: 0 abort, -1 ignore.\n");
        fprintf(f, " * It lives here rather than in the runtime so a guard is\n");
        fprintf(f, " * a load and a branch instead of a library call. */\n");
        fprintf(f, "static int __mm_e[2];\n");
    }
    /* OUTSIDE the ON ERROR block - it was written inside it, so an I2C2
       program that never mentions ON ERROR got no declaration and the
       generated C would not compile.  Nothing caught it because every
       I2C2 program in the corpus used ON ERROR SKIP to scan the bus. */
    if (cv.uses_i2c)
        /* SETPIN puts the pins here and OPEN reads them: MMBasic
           allows the two to be far apart in a program. */
        fprintf(f, "static int __mmi2c_sda, __mmi2c_scl;\n");
    if (cv.uses_spi)
        /* the same for SPI's three, in whatever order they were
           written - mmb_spi.h works out which pin is which signal */
        fprintf(f, "static int __mmspi_a, __mmspi_b, __mmspi_c;\n");
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
    if (cv.uses_cmdline)
        fprintf(f, "int main(int argc, char **argv)\n{\n");
    else
        fprintf(f, "int main(void)\n{\n");
    fprintf(f, "    unsigned __mark = mm_mark(); (void)__mark;\n");
    if (cv.uses_cmdline)
        fprintf(f, "    mm_argv_bind(argc, argv);\n");
    if (cv.uses_onerror)
        fprintf(f, "    mm_err_bind(__mm_e);\n");
    if (cv.heap_used)
        fprintf(f, "    H = mm_heap(sizeof *H);   "
                   "/* arrays and strings */\n");
    if (cv.ndata > 0)
        fprintf(f, "    mm_data_init4(__mmb_data_kind, __mmb_data_f, "
                   "__mmb_data_i, __mmb_data_s, %d);\n", cv.ndata);
    for (k = 0; k < cv.out_main.n; k++)
        fprintf(f, "%s\n", cv.out_main.lines[k]);
    fprintf(f, "    return 0;\n}\n");
}

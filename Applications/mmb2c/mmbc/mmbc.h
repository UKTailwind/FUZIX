/*
 * mmbc.h - mmbc, the mmb2c translator in C.
 *
 * A function-for-function mirror of mmb2c.py; the Python is the
 * reference implementation and byte-identical output over the test
 * suite (mmbc/mmbctests.sh) is the definition of correct.  Where this
 * file or its .c files do something odd, the answer is usually "that
 * is what the Python does" - check mmb2c.py before "fixing" it.
 *
 * Memory model: two bump pools.  The scratch pool holds token texts
 * and expression fragments and is reset at the top of tokenize() -
 * one line at a time, so usage is bounded on a 256K Fuzix process.
 * Anything that outlives the current line (symbol names, block-stack
 * texts, output lines) must be copied out with pstr() at the point of
 * store.  The persistent pool is never freed, like the Python.
 */

#ifndef MMBC_H
#define MMBC_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <setjmp.h>

#define VERSION "0.1"

/* ---- types (mmb2c.py TY_F / TY_I / TY_S) ---- */

#define TY_F 'f'                /* MMFLOAT   (double)  - MMBasic default */
#define TY_I 'i'                /* MMINTEGER (int64_t) */
#define TY_S 's'                /* string, MMBasic layout [len][data][NUL] */
#define TY_NONE 0               /* Python None */
/* OPTION ANGLE DEGREES: MMBasic's RADCONV (Functions.h:38), to its own
 * digits.  SIN/COS/TAN divide by it, ATN/ATAN2/ASIN/ACOS multiply by it
 * - the same operations in the same order as the reference, so a
 * side-by-side agrees to the last bit. */
#define RADCONV "57.2957795130823229"

/* MM_STRLEN in mmb_runtime.h: the characters a string can hold, and the
 * LENGTH an array element gets when the program does not say. */
#define MM_STRLEN 255
/* The Python types a struct value as a TUPLE ('T'|'TM', tyname); every
 * isinstance(ty, tuple) test becomes ty == TY_T here, with the tuple's
 * members carried in struct val's stype ('T'/'TM' second half) and tm
 * ('TM' = reached via a member) fields. */
#define TY_T 'T'

const char *ctype_of(int ty);   /* CTYPE[]  */
const char *tyname_of(int ty);  /* TYNAME[] */

/* ---- tokens ---- */

#define T_ID 1
#define T_NUM 2
#define T_STR 3
#define T_OP 4

struct tok {
    int kind;
    const char *text;           /* scratch lifetime - pstr() before storing */
    const char *up;             /* T_ID: upper-cased; T_NUM: "F"/"I"/"H";
                                   T_STR: ""; T_OP: canonical op text */
};

/* Python builds unbounded lists; a fixed cap with a hard error is the
 * honest C translation - only reachable on lines no real program has. */
#define MAXTOKS 512

int tokenize(const char *line, int lineno, struct tok *out);
int tokenize_frag(const char *line, int lineno, struct tok *out);

/* ---- string helpers (mmbc_lex.c) ---- */

int is_alpha(int c);
int is_digit_c(int c);          /* is_digit clashes with some libcs */
int is_idchar(int c);
int is_hexd(int c);
char *cblock_safe(const char *text);
int nonzero_literal(const char *code);
const char *float_form_of_int_literal(const char *code);
int boolean_expr(const char *code);
char *c_string_literal(const char *s);
char *split_suffix(const char *word, int *ty);  /* ty out: TY_* or TY_NONE */
char *cvar(const char *name);
char *cconst(const char *name);   /* a global CONST: #define, own prefix */
struct sym;
/* run-time arrays (mmbc_decl.c) */
int redimmed_in(const char *canon);
const char *dyn_decl(struct sym *s, const char *cn);
const char *strsz_of(struct sym *s);
const char *sread_of(struct sym *s, const char *code);
const char *swrite_of(struct sym *s, const char *target, const char *val);
const char *swrite_cap(int cap, const char *target, const char *val);
void no_length_array(struct sym *s);
void emit_dim_alloc(struct sym *s, const char **dims, int ndims,
                    int preserve);
void do_redim(void);
const char *varaddr(void);      /* PEEK(VARADDR v) */
int gp_pin(const char *word, const struct sym *known);  /* "GP8" -> 8 */
char *clabel(const char *name);
int const_c_expr(const char *text);
int const_or_literal_expr(const char *text);
char *upper(const char *s);
char *lower(const char *s);

/* ---- pools + formatting (mmbc_util.c) ---- */

void *palloc(size_t n);
char *pstr(const char *s);
void *salloc(size_t n);
char *sstr(const char *s);
void scratch_reset(void);
char *sfmt(const char *fmt, ...);   /* scratch sprintf - the workhorse
                                       standing in for Python % */

/* ---- MMError (mmbc_util.c) ----
 *
 * mm_error() formats err_msg and longjmps to the current catch frame,
 * mirroring `raise MMError`.  Catch sites push a frame:
 *
 *     jmp_buf jb, *saved = err_jmp;
 *     err_jmp = &jb;
 *     if (setjmp(jb) == 0) { ...protected...; err_jmp = saved; }
 *     else { err_jmp = saved; ...handler, message in err_msg...; }
 *
 * With no frame in place mm_error prints and exits, standing in for an
 * uncaught Python traceback.
 */

extern jmp_buf *err_jmp;
extern char err_msg[512];
void mm_error(const char *fmt, ...);

/* ---- source lines (mmbc_main.c) ---- */

extern char **src_lines;        /* with trailing \n kept, like readline() */
extern int src_nlines;
extern int escape_line;         /* OPTION ESCAPE's positional gate: */
extern int escape_col;          /* set once per source by scan_escape() */
void scan_escape(void);

/* ---- static word tables (mmbc_tab.c) ---- */

struct builtin { const char *name; int minargs, maxargs; };

int kw_in(const char *up);                  /* up in KEYWORDS */
int routine_name_known(const char *canon);   /* a SUB/FUNCTION of that name */
const struct builtin *builtin_get(const char *up);   /* BUILTINS[up] */
int rawarg_in(const char *up);
int strfunc_in(const char *up);
int bintype_index(const char *up);          /* BINTYPES.index, -1 absent */
long rgbname_get(const char *up);           /* RGBNAMES[up], -1 absent */
int mathfunc_get(const char *up);           /* MATHFUNCS[up] argc, 0 absent */
int matharray_in(const char *up);

/* ---- values: what every expression routine returns ---- */

/* stype/tm are only meaningful when ty == TY_T (the Python's tuple
 * type); everywhere else they are left zeroed by mkval and never
 * read. */
struct val { const char *code; int ty; const char *stype; int tm; };

/* ---- symbols ---- */

struct sym {
    const char *name;           /* canonical: lower case, no suffix */
    const char *disp;           /* as the programmer spelled it */
    int ty;
    const char *stype;          /* canonical TYPE name when a struct */
    const char *acc;            /* C text used to read/write it */
    const char **dims;          /* C size expressions */
    int ndims;
    int is_const, is_array, is_param, byref, is_static;
    /* A global CONST whose expression is not a compile-time constant:
       a hidden global assigned once where the CONST statement stands,
       never a #define (see do_const) */
    int const_runtime;
    /* An array whose bounds are only known at run time.  Held exactly
       as an array PARAMETER is - a flat pointer plus a bounds table -
       so index_of(), array_flat() and BOUND() take the same branch for
       both.  bacc is the C text of that table. */
    int dynamic;
    const char *bacc;
    /* DIM s$(n) LENGTH m on an ARRAY: the element stride, which is
       m + 1 and is part of the PROGRAM'S VIEW OF MEMORY, not just a
       saving - findvar returns val.s + nbr * (size + 1)
       (MMBasic.c:4924), so a program walking the array with
       PEEK(VARADDR a$()) is entitled to that spacing.  0 = the default
       MM_STRSZ element, which carries a trailing NUL. */
    int alen;
    int where;                  /* source line first seen */
    int implied;
    int has_init;
    const char *declared_in;    /* "" = main line, else routine name */
};

/* ---- TYPE ... END TYPE (mmbc_type.c) ----
 *
 * One member of a TYPE.  esize is the element size in bytes and count
 * the number of elements (1 unless the member is an array), so
 * offset + esize * count is where the next member starts from.
 * Python `m.dims is None` <-> has_dims == 0.  The counts are long
 * long: Python ints are unbounded and the emission formats print the
 * arithmetic results. */
#define MAXMDIMS 16     /* Python list is unbounded; hard error past this */

struct typemember {
    const char *name;
    const char *disp;
    int ty;                     /* TY_* for plain members, TY_NONE (None)
                                 * for struct */
    const char *stype;          /* canonical type name for struct members */
    long long slen;             /* STRING members: LENGTH */
    long long dims[MAXMDIMS];   /* list of int bounds */
    int ndims;
    int has_dims;
    long long count;
    long long offset;
    long long esize;
};

/* A TYPE ... END TYPE definition, laid out exactly as the firmware
 * lays it out (ParseStructMember + GetStructAlignment): numeric and
 * struct members start 8-aligned, strings are unaligned, and the
 * total is rounded to 8 only when something numeric is inside.  See
 * TYPE-SPEC.md for the full contract.
 *
 * members doubles as the Python's byname dict (linear lookup); 16 is
 * the firmware's limit, enforced in type_member before add. */
#define MAXMEMBERS 16

struct typedef_rec {
    const char *name;
    const char *disp;
    struct typemember *members[MAXMEMBERS];
    int nmembers;
    long long total;
    int numeric;                /* anything numeric anywhere inside */
    int where;
};

/* member_path result: the Python's
 *   ('num', code, ty) | ('str', ptrcode, slen) |
 *   ('struct', code, tyname, via_member) */
#define MP_NUM 0
#define MP_STR 1
#define MP_STRUCT 2

struct mpres {
    int kind;
    const char *code;
    int ty;                     /* MP_NUM */
    long long slen;             /* MP_STR */
    const char *tyname;         /* MP_STRUCT */
    int via;                    /* MP_STRUCT */
};

/* struct_head result: the Python's (sym, parts, sfx) */
struct shead {
    struct sym *s;
    const char **parts; int nparts;
    int sfx;                    /* TY_* or TY_NONE (None) */
};

/* struct_operand result: the Python's (kind, code, sym), kind
 * 'one' -> all == 0, 'all' -> all == 1 */
struct sopnd { int all; const char *code; struct sym *s; };

struct gtouch { const char *name; int line; };

struct routine {
    const char *name;           /* canonical */
    const char *disp;
    const char *cname;          /* f_<name> */
    int is_func;
    int ty;
    struct sym **params; int nparams, cparams;
    struct sym **locals; int nlocals, clocals;   /* insertion-ordered map */
    const char **local_order; int nlocal_order, clocal_order;
    struct sym **statics; int nstatics, cstatics;
    int line;
    struct gtouch *gtouch; int ngtouch, cgtouch; /* global -> first line */
    int heap_locals;            /* has LOCAL arrays or strings, so its
                                 * invocations carry a heap block */
};

/* One record per label name; the Python keeps six dicts keyed the same
 * way (labels, data_at, label_routine, label_depth, goto_depth,
 * labels_used), so each field carries its own "key present" flag to
 * keep dict-membership semantics exact. */
struct label {
    const char *name;
    int placed, lineno;         /* labels{} */
    int has_data_at, data_at;   /* data_at{} */
    const char *routine;        /* label_routine{}, NULL = absent */
    int has_depth, depth;       /* label_depth{} */
    int has_goto, goto_depth;   /* goto_depth{} */
    int used;                   /* labels_used{} */
};

struct implied_rec { const char *name; int ty; int line;
                     const char *routine; };
struct data_item { int kind; const char *f, *i, *sv; };
struct skip_rec { int line; const char *text; const char *why; };
struct bnd { const char *key; const char *name; const char *body; };

struct outbuf { const char **lines; int n, cap; };

/* gosub_sites: routine name ('' = main) -> site numbers, scan order */
struct gsub { const char *routine; int *sites; int n, cap; };

/* The Python block stack holds tuples of per-kind shape; tuple[0] is
 * the kind ('if', 'for', 'do', 'while', 'select', 'routine', ...) and
 * the LAST element is always the start line (walk's unterminated-block
 * error reads blk[0] and blk[-1]).  The middle members become a..d;
 * each push site documents its own mapping. */
/* Shapes: ['if',line] ['while',line] ['routine',line]
 * ['for',canon,line]->a  ['do','head'|'tail',line]->a
 * ['select',name,ty,line]->a,ty.  Strings pushed here must be pstr()d
 * (newtmp returns scratch). */
struct block { const char *kind; const char *a, *b, *c, *d;
               int ty; int line; };

/* ---- the guts: Python's Conv instance as one global ---- */

#define M_SCAN 0
#define M_DECL 1
#define M_EMIT 2
#define M_TYPES 3               /* the Python's mode 'types' */

struct conv {
    const char *srcname;
    struct sym **globals; int nglobals, cglobals;      /* insertion order */
    struct routine **routines; int nroutines, croutines;
    /* CALL by name: one dispatcher per distinct argument shape,
       emitted after the routine bodies (see call_dispatch) */
    struct calldisp *calld; int ncalld, ccalld;
    const char **lit_names; int nlit_names, clit_names;
    int lit_names_built;        /* see lit_routine_names() */
    const char **routine_names; int nroutine_names, croutine_names;
    struct label *labels; int nlabels, clabels;
    const char **errors; int nerrors, cerrors;
    const char **warnings; int nwarnings, cwarnings;
    struct implied_rec *implied; int nimplied, cimplied;
    struct data_item *data; int ndata, cdata;
    /* DefineFont blocks, collected by pass_fonts before anything else
     * runs.  Kept in font-number order (there are at most seven), so
     * the emitter walks the array as the Python walks sorted(). */
    struct fontdef { int num; unsigned char *data; int len; }
        *fonts; int nfonts, cfonts;
    int lenient, fcc;
    struct bnd *bnds; int nbnds, cbnds;                /* bnd_tables */
    struct skip_rec *skipped; int nskipped, cskipped;
    struct gsub *gsubs; int ngsubs, cgsubs;            /* gosub_sites */
    int gosub_n;
    int opt_default;            /* TY_* or TY_NONE (OPTION DEFAULT NONE) */
    int opt_explicit, opt_base;
    /* OPTION ANGLE: MMBasic's `optionangle`, a plain multiplier
     * (MM_Misc.c:5064).  NULL is RADIANS and emits nothing; RADCONV is
     * DEGREES and folds into the trig call sites, so a program in
     * radians - which is every program that never says - pays nothing
     * at all for this. */
    const char *opt_angle;
    int opt_angle_seen, opt_angle_line;
    int mode;
    struct routine *cur;        /* NULL = main line */
    struct outbuf out_main, out_body;
    struct outbuf *out;
    int indent;
    struct block *blocks; int nblocks, cblocks;
    int tmpn;
    int lineno;
    struct tok toks[MAXTOKS]; int ntoks;
    int i;
    int tmp_used;
    /* TYPE ... END TYPE: the Python's self.types dict and
     * self.type_order list collapse into one array - names are only
     * entered at END TYPE, exactly when type_order is appended, so
     * registration order IS the dict's insertion order. */
    struct typedef_rec **types; int ntypes, ctypes;
    int in_type;                /* inside TYPE...END TYPE in this pass */
    /* depth of single-line IF bodies being emitted: END SUB means
     * "return now" in there, not "the routine ends here" */
    int inline_depth;
    int uses_clear;
    int uses_circle;
    int uses_box;
    int uses_gui;
    int uses_linew;
    int reads_string;
    const char **redimmed;      /* every array some REDIM names */
    int nredimmed, credimmed;
    int uses_rbox;
    int uses_triangle;
    int uses_polygon;
    int uses_bezier;
    int uses_fill;
    int uses_arc;
    int uses_text;
    int uses_fbsel;             /* FRAMEBUFFER buffer named at run time */
    int uses_mappal;
    int uses_gpio;
    int uses_pioout;            /* WS2812/BITSTREAM: mmb_pioout.h */
    int uses_port;              /* PORT: pulls in mmb_port.h */
    int uses_math;              /* MATH C_ADD etc: pulls in mmb_math.h */
    int uses_crc;               /* MATH(CRCn ...): pulls in mmb_crc.h */
    int uses_mt;                /* MATH RANDOMIZE / MATH(RAND) */
    int uses_sort;              /* SORT: pulls in mmb_sort.h */
    int uses_array;             /* whole-array ops/REDIM/MATH(): mmb_array.h */
    int uses_lstring;           /* LONGSTRING: pulls in mmb_lstring.h */
    int uses_datetime;          /* DATE$/TIME$/EPOCH etc: mmb_datetime.h */
    int uses_data;              /* DATA/READ/RESTORE: mmb_data.h */
    int uses_misc;              /* GOSUB/BIT/FLAG/BIN2STR etc: mmb_misc.h */
    int uses_pulse;             /* PULSE: pulls in mmb_pulse.h */
    int uses_onewire;           /* ONEWIRE/TEMPR: mmb_onewire.h */
    int uses_pulsin;            /* Pulsin(/Distance(: mmb_pulsin.h */
    int uses_comms;             /* I2C/SPI data forms: mmb_comms.h */
    int uses_net;               /* the socket floor: mmb_net.h */
    int uses_udp;               /* WEB UDP: mmb_udp.h */
    int uses_webclient;         /* WEB TCP/TLS client: mmb_webc.h */
    int uses_webserver;         /* WEB TCP server: mmb_webs.h */
    int uses_json;              /* JSON$: mmb_json.h */
    /* one entry per TRANSMIT PAGE call site: the normalised
       expression texts, emitted as __mmwebsub_N at file scope */
#define MAXWEBSUB 16
#define MAXWEBKEYS 128
    struct websubtab {
        const char *keys[MAXWEBKEYS];
        int n;
    } websubs[MAXWEBSUB];
    int nwebsubs;
    int uses_wait;              /* a serviced PAUSE: pulls in mmb_wait.h */
    int uses_play;              /* PLAY: emit the volume it remembers */
    int uses_blit;              /* BLIT family: mmb_blit.h */
    int uses_flash;             /* pseudo flash slots: mmb_flash.h */
    int uses_sprite;            /* SPRITE family: mmb_sprite.h */
    int uses_playd;             /* SOUND/TONE/MOD daemons: mmb_play.h */
    /* FRAMEBUFFER LAYER with a transparent colour: the colour is
       run-time state (the firmware's transparentlow/high), kept in an
       emitted global that MERGE reads when it names no colour */
    int uses_fbt;
    /* set in the scan pass: any ON ERROR at all pulls in the __mm_e
       state, the routine prologues and mm_err_bind */
    int uses_onerror;
    /* ON ERROR IGNORE (or a SKIP whose count is not a bare literal)
       arms trapping for an unbounded stretch of the program, so every
       statement pays the checked forms - set in the scan pass,
       statements BEFORE the line included, because the armed window is
       a run-time thing.  A literal ON ERROR SKIP n arms exactly the ON
       ERROR statement plus the next n: the checked forms and the
       per-statement bookkeeping are emitted for that window alone
       (err_window, counted down in statement()). */
    int onerror_global;
    int err_window;
    int err_window_pending;     /* -1 = none */
    /* likewise: an interrupt armed at line 100 has to be polled by the
       statements before it, so the poll sites are emitted for the whole
       program or none of it */
    int uses_interrupts;
    int uses_pwm;
    int uses_i2c;
    int uses_i2c0;
    int uses_spi;
    int uses_peek;             /* PEEK(): pulls in mmb_peek.h */
    int uses_cmdline;          /* MM.CMDLINE$: main takes argv */
    /* set by global_decls when the program has any array or string:
     * main() then has to allocate the block they live in */
    int heap_used;
};

extern struct conv cv;

/* ---- growable arrays ---- */

void *xrealloc(void *p, size_t n);
#define GROW(arr, n, cap) do { \
        if ((n) == (cap)) { \
            (cap) = (cap) ? (cap) * 2 : 16; \
            (arr) = xrealloc((arr), sizeof(*(arr)) * (size_t)(cap)); \
        } \
    } while (0)

/* ---- conv plumbing (mmbc_sym.c) ---- */

void cv_err(const char *fmt, ...);          /* self.err - raises */
void cv_note(const char *fmt, ...);         /* self.note - records, no raise */
void cv_warn(const char *fmt, ...);         /* self.warn - dedupes */
struct tok *peek(int k);
int at_end(void);
struct tok *nxt(void);
int is_op(const char *s, int k);
int checks_on(void);
int is_kw(const char *s, int k);
int accept_op(const char *s);
void expect_op(const char *s);
int accept_kw(const char *s);
/* A letter, a quoted letter or a string worked out as the program
   runs - MMBasic takes all three for a FRAMEBUFFER buffer and for PLAY
   SOUND's channel and type.  Returns a C expression: a constant when
   the letter is written down, a call to `rt' when it is not. */
struct kwval { const char *nm; int val; };
const char *kw_or_str(const struct kwval *table, const char *rt,
                      const char *what);
/* TEXT's justification: the bare word MMBasic tries before it
   evaluates anything, or an ordinary string expression. */
const char *just_arg(void);
const char *fb_buf(void);                   /* self.fb_buf - N=0, F=1 */
int stmt_end(void);
void emit(const char *text);
int last_line(void);                        /* index of the line emit() wrote */
void raw(const char *text);
char *newtmp(const char *pfx);
void out_append(struct outbuf *o, const char *persistent_line);
void out_insert(struct outbuf *o, int where, const char *persistent_line);
struct label *label_rec(const char *canon);  /* find-or-create */
struct sym *sym_lookup(const char *canon);   /* self.lookup */
struct sym *sym_new(const char *canon, int ty, const char *acc);
struct sym *declare(const char *canon, int ty, const char *scope,
                    const char **arr_dims, int ndims, int is_static);
struct sym *reference(const char *word, int as_array);
void note_touch(const char *canon, struct sym *s);
const char *as_int(struct val v);
const char *as_flt(struct val v);
const char *as_str(struct val v);
struct val need_num(struct val v);
void errors_add(const char *msg);            /* append, no dedupe */
void errors_add_dedup(const char *msg);      /* the statement-site form */

/* ---- expression grammar (mmbc_expr.c / mmbc_builtin.c) ----
 * Full surface (e_* chain, call machinery, builtins) in mmbc_expr.h;
 * only the entry point is shared here. */

struct val expr(void);
struct val input_target(int *cap);  /* mmbc_stmt.c; MATH(BASE64) too */

/* ---- declaration passes (mmbc_decl.c) ---- */

void pass_routine_names(void);
void pass_declarations(void);
/* DefineFont ... End DefineFont, taken at the LINE level before any
 * other pass (the body is hex, not BASIC) and blanked out afterwards.
 * Fonts bind at program LOAD in MMBasic, so a block at the bottom of a
 * file is in force at the top of it. */
void pass_fonts(void);
void place_label(const char *canon);
void strip_line_number(void);
void skip_statement(void);
void decl_statement(void);
void collect_data(void);
char *source_text(int a, int b);
void decl_routine(int is_func);
void decl_param(struct routine *r);
void do_option(void);
void do_declare(const char *kw);
void do_const(void);

/* ---- TYPE / structures (mmbc_type.c) ---- */

struct typedef_rec *types_get(const char *canon);   /* self.types.get */
void pass_types(void);
int skip_type_block(const char *up);
struct mpres member_path(const char *base, const char *tyname,
                         const char **parts, int nparts, int sfx);
int struct_head(const char *word, struct shead *out); /* 1 = struct */
const char *struct_base(struct sym *s);
struct val member_value(struct mpres res);
struct val struct_fn(void);
void do_struct(void);
void assign_member(struct mpres res);
void assign_struct(const char *target, const char *tyname);
void struct_initialiser(struct sym *s);

/* ---- statement walk (mmbc_walk.c / mmbc_stmt.c) ---- */

void walk(int mode);
void statement(void);
void statement_inner(void);
char *loop_cond(const char *c);
int is_literal_number(struct val v);
struct routine *routine_get(const char *canon);

/* CALL by name (mmbc_expr.c): a dispatcher per argument shape */
struct arglist;
#define MAXCALLC 64
struct calldisp {
    int is_func;
    int rty;                    /* result TY_*, TY_NONE for subs */
    int nparams;
    int pty[64];
    int pbyref[64];
    const char *name;           /* __mm_calld_N */
    struct routine *rep;
    struct routine *cands[MAXCALLC];
    int ncands;
};
struct calldisp *call_dispatch(int is_func, struct arglist *args);
struct val emit_call_byname(struct calldisp *d, const char *nmexpr,
                            struct arglist *args);
char *calld_head(struct calldisp *d);
int type_word(void);
char *zero_of(struct sym *s);
char *signature(struct routine *r);

/* ---- output side (mmbc_out.c) ---- */

void report_build(struct outbuf *o);
void conv_write(FILE *f);

#endif

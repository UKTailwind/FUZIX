#ifndef MMB_DATA_H
#define MMB_DATA_H
/*
 *	DATA / READ / RESTORE - the cursor over the tables the translator
 *	compiled into this program.
 *
 *	WHY A HEADER rather than the runtime - the mmb_math.h bargain:
 *	the tables are the program's own data, the cursor is the
 *	program's own state, and nothing here touches the outside world
 *	(mm_val and mm_toint stay runtime calls, already paid for).
 *	Moved out of mmb_runtime.c on 2026-08-21, byte for byte - and
 *	SIMPLER for the move: the hosted build read the string table
 *	through a VM-offset shim (mm_vm_base/mm_vm_rd32) because the
 *	runtime lived on the far side of the bcrun boundary; here a
 *	string table entry is just a pointer.
 *
 *	mm_data_init5 is the form the translator emits: any column it
 *	can prove dead is NULL, and a NULL kind column means every item
 *	is `ukind` - a DATA item costs 24 bytes with every column
 *	present and the data segment is bounded at 64K, so this is the
 *	difference between a large table fitting and not.  init4 and the
 *	struct form stay for hand-written driver code.
 */

#ifndef MMG_FN
#if defined(MM_FCC) || defined(MM_PC3)
#define MMG_FN static
#else
#define MMG_FN static __inline__ __attribute__((unused))
#endif
#endif

static const MMDataItem *mm_dtbl;
static int  mm_dn, mm_dptr;
static int  mm_dstack[8];
static int  mm_dsp;

static int              mm_d4_on;
static const int       *mm_d4_kind;
static int              mm_d4_ukind;
static const MMFLOAT   *mm_d4_f;
static const MMINTEGER *mm_d4_i;
static const char     **mm_d4_s;

MMG_FN const char *mm_d4_str(int j)
{
    if (mm_d4_s == NULL)
        return "\000";
    return mm_d4_s[j];
}

/* The kind of item j: from the column, or the one value they all share
   when the translator proved there was only one. */
MMG_FN int mm_d4_k(int j)
{
    return mm_d4_kind ? mm_d4_kind[j] : mm_d4_ukind;
}

MMG_FN void mm_data_init(const MMDataItem *tbl, int n)
{
    mm_dtbl = tbl; mm_d4_on = 0; mm_d4_kind = NULL;
    mm_dn = n; mm_dptr = 0; mm_dsp = 0;
}

/* The general form.  `k` may be NULL, and then every item is `ukind`;
   f, i and s may each be NULL when no item of that kind exists. */
MMG_FN void mm_data_init5(const int *k, int ukind, const MMFLOAT *f,
                          const MMINTEGER *i, const char **s, int n)
{
    mm_d4_on = 1;
    mm_d4_kind = k; mm_d4_ukind = ukind;
    mm_d4_f = f; mm_d4_i = i; mm_d4_s = s;
    mm_dtbl = NULL;
    mm_dn = n; mm_dptr = 0; mm_dsp = 0;
}

/* The four-column form every program built before the columns became
   optional still calls.  A kind array is always present there. */
MMG_FN void mm_data_init4(const int *k, const MMFLOAT *f, const MMINTEGER *i,
                          const char **s, int n)
{
    mm_data_init5(k, MM_D_FLT, f, i, s, n);
}

MMG_FN void mm_restore(int index)
{
    if (index < 0) index = 0;
    mm_dptr = index;
}

MMG_FN int mm_next_idx(void)
{
    if (mm_dptr >= mm_dn) mm_error("No more DATA to READ");
    return mm_dptr++;
}

MMG_FN MMFLOAT mm_read_f(void)
{
    int j = mm_next_idx();
    int kind = mm_d4_on ? mm_d4_k(j) : mm_dtbl[j].kind;
    if (kind == MM_D_STR)
        return mm_val(mm_d4_on ? mm_d4_str(j) : mm_dtbl[j].s);
    if (kind == MM_D_INT)
        return (MMFLOAT)(mm_d4_on ? mm_d4_i[j] : mm_dtbl[j].i);
    return mm_d4_on ? mm_d4_f[j] : mm_dtbl[j].f;
}

MMG_FN MMINTEGER mm_read_i(void)
{
    int j = mm_next_idx();
    int kind = mm_d4_on ? mm_d4_k(j) : mm_dtbl[j].kind;
    if (kind == MM_D_STR)
        return (MMINTEGER)mm_val(mm_d4_on ? mm_d4_str(j) : mm_dtbl[j].s);
    if (kind == MM_D_FLT)
        return mm_toint(mm_d4_on ? mm_d4_f[j] : mm_dtbl[j].f);
    return mm_d4_on ? mm_d4_i[j] : mm_dtbl[j].i;
}

MMG_FN char *mm_read_s(void)
{
    int j = mm_next_idx();
    char *t = mm_tmp();
    mm_sset(t, mm_d4_on ? mm_d4_str(j) : mm_dtbl[j].s);
    return t;
}

MMG_FN void mm_read_save(void)
{
    if (mm_dsp >= (int)(sizeof mm_dstack / sizeof mm_dstack[0]))
        MM_RAISE("Too many nested READ SAVE");
    mm_dstack[mm_dsp++] = mm_dptr;
}

MMG_FN void mm_read_unsave(void)
{
    if (mm_dsp <= 0) MM_RAISE("READ RESTORE without READ SAVE");
    mm_dptr = mm_dstack[--mm_dsp];
}

#endif /* MMB_DATA_H */

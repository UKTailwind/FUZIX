/*
 *	The mm_* BASIC runtime, native inside bcrun.
 *
 *	Included from the end of bcrun.c so everything stays one
 *	translation unit: the wrappers need arg()/argd()/dput()/mem and
 *	the interpreter needs mm_wrap_lookup()/mmrt_reserve(), and none
 *	of it wants external linkage.
 *
 *	mmb_runtime.c and mmb_runtime.h are verbatim copies of the master
 *	files in the mmb2c repo - edit them THERE and re-run
 *	fcc/sync-runtime.sh, or the reference build and this one drift.
 *
 *	The scheme: the runtime's own code runs on native pointers,
 *	untouched.  Every entry point a translated program can call gets
 *	a small wrapper converting VM offsets to native pointers on the
 *	way in (mm_ptr) and back on the way out (mm_off).  The only state
 *	a program can hold a pointer to - the scratch-string pool and the
 *	by-ref pool - is carved out of VM memory by mmrt_reserve() and
 *	bound into the runtime, so those pointers are ordinary VM
 *	addresses.  Everything else (FILE channels, DATA cursors, GOSUB
 *	stack, INPUT line buffer) is native state the program only ever
 *	names, never addresses.
 */

/* Everything the runtime includes must be read BEFORE the strtoll
   macros below, or a first-read declaration of strtoll would be
   macro-expanded into garbage.  All are include-guarded no-ops where
   bcrun.c already pulled them in. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <time.h>

/*
 *	Microseconds since start, same sources as the time_us libcall:
 *	the PC3 kernel's clock through /dev/sys on the board, wall clock
 *	on the development machine.  Defined before the runtime is
 *	included so its MM_HOSTED branches pick it up.
 *
 *	All 64 bits: TIMER is a float in MMBasic, so the runtime keeps its
 *	base in microseconds, and the 31-bit ADVAL(-9) those used to come
 *	from wraps every 36 minutes - which would send TIMER backwards in
 *	the middle of a program.
 */
static long long time_us64(void)
{
	return lib_us64();
}

/*
 *	Fuzix libc's strtoll stops at 32 bits; bc_strtoll above is the
 *	one 64-bit parser every host shares, so the runtime's VAL() and
 *	friends parse identically on the development machine and the
 *	board.
 */
#define strtoll(s, e, b)  bc_strtoll((s), (e), (b), 0)
#define strtoull(s, e, b) ((unsigned long long)bc_strtoll((s), (e), (b), 1))

/*
 *	The runtime's own maths goes through the shared table too.  The
 *	kernel exports one libm from flash (mfns_share fills mfns[] from
 *	it at startup, fatally if absent), but these four are also called
 *	DIRECTLY below - mm_pow, mm_atan3, the float formatter, the
 *	statistics reducers - which made the linker keep private copies
 *	of pow, atan2, log10 and sqrt plus their internals (__log1p,
 *	scalbn, atan): ~5K in every process, duplicating functions the
 *	kernel already runs from flash with the DCP.  Routing the direct
 *	calls through mfns[] drops them.  Index by position: the mfns
 *	order is the table ABI (sqrt 9, log10 12, pow 16, atan2 17).
 *	On the hosts mfns[] holds the local functions, so this is an
 *	identity there.
 */
#define sqrt(x)      ((mfns[9].f1)(x))
#define log10(x)     ((mfns[12].f1)(x))
#define pow(x, y)    ((mfns[16].f2)((x), (y)))
#define atan2(y, x)  ((mfns[17].f2)((y), (x)))

/* The runtime's hosted islands read the DATA string table through
   these (it holds 32-bit VM pointers whatever the host width). */
/* NULL, not mem: the DATA string table holds machine addresses like
   everything else, so mm_d4_str's "base + entry" must add nothing. */
unsigned char *mm_vm_base(void) { return NULL; }
unsigned long mm_vm_rd32(unsigned long a) { return rd32(a); }

#define MM_HOSTED 1
#include "mmb_runtime.c"

/* ---- offset <-> pointer -------------------------------------------- */

/*
 *	need is how much must exist above the offset: MM_STRSZ for a
 *	string argument, 0 for arrays and buffers whose extent is the
 *	runtime's own business (exactly as fread's was).  NULL passes
 *	through so optional index arrays keep working.
 */
/*
 *	Both identities now: a program address is a machine address (see
 *	vptr in bcrun.c).  Kept as functions because the ~135 wrappers
 *	below are written in terms of them, and because NULL still has to
 *	survive the round trip in both directions.
 */
static char *mm_ptr(unsigned long off, unsigned long need)
{
	(void)need;
	return (char *)(uintptr_t)off;
}

static long mm_off(const char *p)
{
	return (long)(unsigned long)(uintptr_t)p;
}

/* Argument fetchers by type; a double or an MMINTEGER takes two slots. */
#define Ps(n)	mm_ptr((unsigned long)(uint32_t)arg(n), MM_STRSZ)
#define Pa(n)	mm_ptr((unsigned long)(uint32_t)arg(n), 0)
#define PI(n)	((MMINTEGER *)(void *)Pa(n))
#define PF(n)	((MMFLOAT *)(void *)Pa(n))
#define PSA(n)	((char (*)[MM_STRSZ])(void *)Pa(n))
#define I(n)	((int)arg(n))
#define LL(n)	((MMINTEGER)argll(n))
#define D(n)	((MMFLOAT)argd(n))

/* ---- the wrappers --------------------------------------------------- */

/* scratch and by-ref */
static void w_tmp(void)      { A = mm_off(mm_tmp()); }
static void w_mark(void)     { A = S32(mm_mark()); }
static void w_release(void)  { mm_release((unsigned)arg(0)); A = 0; }
static void w_byref_f(void)  { A = mm_off((char *)mm_byref_f(D(0))); }
static void w_byref_i(void)  { A = mm_off((char *)mm_byref_i(LL(0))); }

/* core string ops */
static void w_sset(void)     { mm_sset(Ps(0), Ps(1)); A = 0; }
static void w_ssetm(void)    { mm_ssetm(Ps(0), I(1), Ps(2)); A = 0; }
static void w_ssetc(void)    { mm_ssetc(Ps(0), Pa(1)); A = 0; }
static void w_ssetn(void)    { mm_ssetn(Ps(0), Pa(1), I(2)); A = 0; }
static void w_scat(void)     { A = mm_off(mm_scat(Ps(0), Ps(1))); }
static void w_scmp(void)     { A = mm_scmp(Ps(0), Ps(1)); }
static void w_scopy(void)    { A = mm_off(mm_scopy(Ps(0))); }

/* console */
static void w_putc(void)     { mm_putc(I(0)); A = 0; }
static void w_pr_s(void)     { mm_pr_s(Ps(0)); A = 0; }
static void w_pr_i(void)     { mm_pr_i(LL(0)); A = 0; }
static void w_pr_f(void)     { mm_pr_f(D(0)); A = 0; }
static void w_pr_nl(void)    { mm_pr_nl(); A = 0; }
static void w_pr_se(void)    { mm_pr_se(Ps(0)); A = 0; }
static void w_pr_ie(void)    { mm_pr_ie(LL(0)); A = 0; }
static void w_pr_fe(void)    { mm_pr_fe(D(0)); A = 0; }
static void w_pr_tabe(void)  { mm_pr_tabe(); A = 0; }
static void w_pr_tab(void)   { mm_pr_tab(); A = 0; }
static void w_col(void)      { A = mm_col(); }
static void w_tab(void)      { A = mm_off(mm_tab(LL(0))); }

/* number -> string */
static void w_int_to_str(void)     { mm_int_to_str(Pa(0), argll(1), I(3)); A = 0; }
static void w_int_to_str_pad(void) { mm_int_to_str_pad(Pa(0), argll(1),
					(signed char)arg(3), I(4), I(5)); A = 0; }
static void w_float_to_str(void)   { mm_float_to_str(Pa(0), D(1), I(3), I(4),
					(unsigned char)arg(5)); A = 0; }

/* numeric helpers */
static void w_toint(void)    { A = mm_toint(D(0)); }
static void w_idiv(void)     { A = mm_idiv(LL(0), LL(2)); }
static void w_mod(void)      { A = mm_mod(LL(0), LL(2)); }
static void w_fdiv(void)     { A = dput(mm_fdiv(D(0), D(2))); }
static void w_pow(void)      { A = dput(mm_pow(D(0), D(2))); }
static void w_sqr(void)      { A = dput(mm_sqr(D(0))); }
static void w_log(void)      { A = dput(mm_log(D(0))); }
static void w_asin(void)     { A = dput(mm_asin(D(0))); }
static void w_acos(void)     { A = dput(mm_acos(D(0))); }
static void w_atan3(void)    { A = dput(mm_atan3(D(0), D(2))); }
static void w_rnd(void)      { A = dput(mm_rnd()); }
static void w_randomize(void){ mm_randomize(LL(0)); A = 0; }
static void w_sgn(void)      { A = mm_sgn(D(0)); }
static void w_int(void)      { A = dput(mm_int(D(0))); }
static void w_fix(void)      { A = dput(mm_fix(D(0))); }

/* string functions */
static void w_asc(void)      { A = mm_asc(Ps(0)); }
static void w_instr(void)    { A = mm_instr(LL(0), Ps(2), Ps(3)); }
static void w_val(void)      { A = dput(mm_val(Ps(0))); }
static void w_chr(void)      { A = mm_off(mm_chr(LL(0))); }
static void w_left(void)     { A = mm_off(mm_left(Ps(0), LL(1))); }
static void w_right(void)    { A = mm_off(mm_right(Ps(0), LL(1))); }
static void w_mid(void)      { A = mm_off(mm_mid(Ps(0), LL(1), LL(3))); }
static void w_ucase(void)    { A = mm_off(mm_ucase(Ps(0))); }
static void w_lcase(void)    { A = mm_off(mm_lcase(Ps(0))); }
static void w_ltrim(void)    { A = mm_off(mm_ltrim(Ps(0))); }
static void w_rtrim(void)    { A = mm_off(mm_rtrim(Ps(0))); }
static void w_space(void)    { A = mm_off(mm_space(LL(0))); }
static void w_strrep(void)   { A = mm_off(mm_strrep(LL(0), LL(2))); }
static void w_str_f(void)    { A = mm_off(mm_str_f(D(0), LL(2), LL(4), Ps(6))); }
static void w_str_i(void)    { A = mm_off(mm_str_i(LL(0), LL(2), LL(4), Ps(6))); }
static void w_hex(void)      { A = mm_off(mm_hex(LL(0), LL(2))); }
static void w_oct(void)      { A = mm_off(mm_oct(LL(0), LL(2))); }
static void w_bin(void)      { A = mm_off(mm_bin(LL(0), LL(2))); }
static void w_byte(void)     { A = mm_byte(Ps(0), LL(1)); }
static void w_trim(void)     { A = mm_off(mm_trim(Ps(0), Ps(1), I(2))); }
static void w_field(void)    { A = mm_off(mm_field(Ps(0), LL(1), Ps(3), Ps(4))); }
static void w_format(void)   { A = mm_off(mm_format(D(0), Ps(2))); }
static void w_mid_assign(void) { mm_mid_assign(Ps(0), LL(1), LL(3), Ps(5)); A = 0; }

/* date and time */
static void w_epoch_now(void){ A = mm_epoch_now(); }
static void w_epoch_str(void){ A = mm_epoch_str(Ps(0)); }
static void w_datetime(void) { A = mm_off(mm_datetime(LL(0))); }
static void w_time_str(void) { A = mm_off(mm_time_str()); }
static void w_date_str(void) { A = mm_off(mm_date_str()); }
static void w_day(void)      { A = mm_off(mm_day(LL(0))); }

/* BIN2STR$ / STR2BIN */
static void w_bin2str(void)  { A = mm_off(mm_bin2str(I(0), D(1), LL(3), I(5))); }
static void w_str2bin_f(void){ A = dput(mm_str2bin_f(I(0), Ps(1), I(2))); }
static void w_str2bin_i(void){ A = mm_str2bin_i(I(0), Ps(1), I(2)); }

/* files */
static void w_open(void)     { mm_open(Ps(0), I(1), LL(2)); A = 0; }
static void w_close(void)    { mm_close(LL(0)); A = 0; }
static void w_close_all(void){ mm_close_all(); A = 0; }
static void w_fpr_s(void)    { mm_fpr_s(LL(0), Ps(2)); A = 0; }
static void w_fpr_i(void)    { mm_fpr_i(LL(0), LL(2)); A = 0; }
static void w_fpr_f(void)    { mm_fpr_f(LL(0), D(2)); A = 0; }
static void w_fpr_nl(void)   { mm_fpr_nl(LL(0)); A = 0; }
static void w_fpr_tab(void)  { mm_fpr_tab(LL(0)); A = 0; }
static void w_eof(void)      { A = mm_eof(LL(0)); }
static void w_loc(void)      { A = mm_loc(LL(0)); }
static void w_lof(void)      { A = mm_lof(LL(0)); }
static void w_seek(void)     { mm_seek(LL(0), LL(2)); A = 0; }
static void w_getline(void)  { A = mm_off(mm_getline(LL(0))); }
static void w_input_str(void){ A = mm_off(mm_input_str(LL(0), LL(2))); }
static void w_input_line(void){ mm_input_line(LL(0)); A = 0; }
static void w_input_next(void){ A = mm_off(mm_input_next()); }
static void w_atoi(void)     { A = mm_atoi(Ps(0)); }
static void w_atof(void)     { A = dput(mm_atof(Ps(0))); }

/* file management */
static void w_kill(void)     { mm_kill(Ps(0)); A = 0; }
static void w_rename(void)   { mm_rename(Ps(0), Ps(1)); A = 0; }
static void w_fcopy(void)    { mm_copy(Ps(0), Ps(1)); A = 0; }
static void w_mkdir(void)    { mm_mkdir(Ps(0)); A = 0; }
static void w_rmdir(void)    { mm_rmdir(Ps(0)); A = 0; }
static void w_chdir(void)    { mm_chdir(Ps(0)); A = 0; }
static void w_cwd(void)      { A = mm_off(mm_cwd()); }
static void w_inkey(void)    { A = mm_off(mm_inkey()); }
/* PRINT @(x,y[,mode]) - returns the empty string, like MMBasic's */
static void w_at(void)       { A = mm_off(mm_at(LL(0), LL(2), LL(4))); }
static void w_dir(void)      { A = mm_off(mm_dir(Ps(0), I(1), I(2))); }
static void w_files(void)    { mm_files(Ps(0)); A = 0; }

/* DATA / READ / RESTORE */
static void w_data_init4(void)
{
	/* the string table stays a VM offset: its elements are 32-bit
	   VM pointers, unreadable through a host pointer on a 64-bit
	   machine */
	mm_data_init4((const int *)(void *)Pa(0),
		      (const MMFLOAT *)(void *)Pa(1),
		      (const MMINTEGER *)(void *)Pa(2),
		      (unsigned long)(uint32_t)arg(3), I(4));
	A = 0;
}
static void w_restore(void)  { mm_restore(I(0)); A = 0; }
static void w_read_f(void)   { A = dput(mm_read_f()); }
static void w_read_i(void)   { A = mm_read_i(); }
static void w_read_s(void)   { A = mm_off(mm_read_s()); }
static void w_read_save(void){ mm_read_save(); A = 0; }
static void w_read_unsave(void){ mm_read_unsave(); A = 0; }

/* SORT */
static void w_sort_i(void)   { mm_sort_i(PI(0), PI(1), I(2), I(3), I(4), I(5)); A = 0; }
static void w_sort_f(void)   { mm_sort_f(PF(0), PI(1), I(2), I(3), I(4), I(5)); A = 0; }
static void w_sort_s(void)   { mm_sort_s(PSA(0), PI(1), I(2), I(3), I(4), I(5)); A = 0; }

/* whole array operations */
static void w_arr_count(void){ A = mm_arr_count(PI(0)); }
static void w_arr_set_i(void){ mm_arr_set_i(PI(0), I(1), LL(2)); A = 0; }
static void w_arr_set_f(void){ mm_arr_set_f(PF(0), I(1), D(2)); A = 0; }
static void w_arr_set_s(void){ mm_arr_set_s(PSA(0), I(1), Ps(2)); A = 0; }
static void w_arr_add_i(void){ mm_arr_add_i(PI(0), I(1), LL(2), PI(4)); A = 0; }
static void w_arr_add_f(void){ mm_arr_add_f(PF(0), I(1), D(2), PF(4)); A = 0; }
static void w_arr_add_s(void){ mm_arr_add_s(PSA(0), I(1), Ps(2), PSA(3)); A = 0; }
static void w_arr_scale_i(void){ mm_arr_scale_i(PI(0), I(1), LL(2), PI(4)); A = 0; }
static void w_arr_scale_f(void){ mm_arr_scale_f(PF(0), I(1), D(2), PF(4)); A = 0; }

/* MATH() array reductions */
static void w_st_sum_i(void) { A = dput(mm_st_sum_i(PI(0), I(1))); }
static void w_st_sum_f(void) { A = dput(mm_st_sum_f(PF(0), I(1))); }
static void w_st_mean_i(void){ A = dput(mm_st_mean_i(PI(0), I(1))); }
static void w_st_mean_f(void){ A = dput(mm_st_mean_f(PF(0), I(1))); }
static void w_st_sd_i(void)  { A = dput(mm_st_sd_i(PI(0), I(1))); }
static void w_st_sd_f(void)  { A = dput(mm_st_sd_f(PF(0), I(1))); }
static void w_st_max_i(void) { A = dput(mm_st_max_i(PI(0), I(1), PI(2))); }
static void w_st_max_f(void) { A = dput(mm_st_max_f(PF(0), I(1), PI(2))); }
static void w_st_min_i(void) { A = dput(mm_st_min_i(PI(0), I(1), PI(2))); }
static void w_st_min_f(void) { A = dput(mm_st_min_f(PF(0), I(1), PI(2))); }
static void w_st_med_i(void) { A = dput(mm_st_med_i(PI(0), I(1))); }
static void w_st_med_f(void) { A = dput(mm_st_med_f(PF(0), I(1))); }

/* misc Tier A */
static void w_pause(void)    { mm_pause(D(0)); A = 0; }
static void w_error_s(void)  { mm_error_s(Ps(0)); A = 0; }
/* ON ERROR: the state pair lives in the PROGRAM's memory so a guard in
   generated code is a load, not a call through here.  Pa(0) translates
   the program address the way every by-reference argument is. */
static void w_err_bind(void) { mm_err_bind((int *)Pa(0)); A = 0; }
static void w_on_error(void) { mm_on_error(I(0), LL(1)); A = 0; }
static void w_errno(void)    { A = mm_errno(); }
/* through a scratch temp: MM.ERRMSG$ lives in bcrun's own memory, and a
   program can only be handed a pointer inside the VM's address space */
static void w_errmsg(void)   { A = mm_off(mm_scopy(mm_errmsg())); }
static void w_ver(void)      { A = dput(mm_ver()); }
static void w_device(void)   { A = mm_off(mm_scopy(mm_device())); }
static void w_cmdline(void)  { A = mm_off(mm_scopy(mm_cmdline())); }
/* The generated main passes its own argc/argv, which are meaningless
   here - the entry is dispatched without them.  bcrun's are the real
   ones, so they are what gets bound, and the generated C stays the same
   in both worlds. */
static void w_argv_bind(void) { mm_argv_bind(prog_argc, prog_argv); A = 0; }
static void w_timer_set(void){ mm_timer_set(D(0)); A = 0; }
static void w_run_begin(void) { mm_run_begin(); A = 0; }
static void w_run_arg(void)   { mm_run_arg(Ps(0)); A = 0; }
static void w_run_arg_i(void) { mm_run_arg_i(LL(0)); A = 0; }
static void w_run_arg_f(void) { mm_run_arg_f(D(0)); A = 0; }
static void w_run_exec(void)  { A = mm_run_exec(); }
static void w_run_bg(void)    { A = mm_run_bg(); }
static void w_play_start(void){ A = mm_play_start(); }
static void w_play_stop(void) { A = mm_play_stop(); }
static void w_set_date(void) { mm_set_date(Ps(0)); A = 0; }
static void w_set_time(void) { mm_set_time(Ps(0)); A = 0; }

/* graphics - every argument and result is RGB888, as in MMBasic */
static void w_pixel(void)    { mm_pixel(LL(0), LL(2), LL(4)); A = 0; }
static void w_pixel_get(void){ A = mm_pixel_get(LL(0), LL(2)); }
static void w_cls(void)      { mm_cls(LL(0)); A = 0; }
static void w_line(void)     { mm_line(LL(0), LL(2), LL(4), LL(6), LL(8));
                               A = 0; }
static void w_hres(void)     { A = mm_hres(); }
static void w_vres(void)     { A = mm_vres(); }
static void w_mode(void)     { mm_mode(LL(0)); A = 0; }
static void w_colour(void)   { mm_colour(LL(0), LL(2)); A = 0; }
static void w_fg(void)       { A = mm_fg(); }
static void w_bg(void)       { A = mm_bg(); }
/* The arrays belong to the program, so they arrive as VM offsets and
   the kernel is handed the real address inside mem. */
static void w_plot(void)     { mm_plot((const short *)Pa(0), LL(1), LL(3));
                               A = 0; }
static void w_fill(void)     { mm_fill((const short *)Pa(0), LL(1), LL(3));
                               A = 0; }
/* PIXEL xa(), ya() [, c | ca()] - six array pointers, of which one of
   each pair is null, then the scalar colour and the count. */
static void w_pixels(void)   { mm_pixels(PF(0), PI(1), PF(2), PI(3),
                                         PF(4), PI(5), LL(6), LL(8));
                               A = 0; }
/* TEXT and FONT.  mm_fontinfo hands back the cell through two by-ref
   integers, which is why it takes pointers where everything around it
   takes values - the caller is mmg_text in mmb_gfx.h, not BASIC. */
static void w_fontinfo(void) { A = mm_fontinfo(LL(0), PI(2), PI(3)); }
static void w_font(void)     { mm_font(LL(0), LL(2)); A = 0; }
static void w_gtext(void)    { mm_gtext(LL(0), LL(2), LL(4), LL(6),
                                        LL(8), LL(10), Ps(12), LL(13));
                               A = 0; }
/* GPIO - one crossing for all of SETPIN and PIN; the statements
   themselves are static functions in mmb_gpio.h, so a program that
   touches no pins carries none of them. */
static void w_gpio(void)     { A = mm_gpio(LL(0), LL(2), LL(4)); }
/* MAP - the palette.  mm_map collects an entry, mm_map_set applies the
   lot during blanking, mm_map_get answers what a number stands for. */
static void w_map(void)      { mm_map(LL(0), LL(2)); A = 0; }
static void w_map_set(void)  { mm_map_set(); A = 0; }
static void w_map_reset(void){ mm_map_reset(); A = 0; }
static void w_map_get(void)  { A = mm_map_get(LL(0)); }
/* FRAMEBUFFER - 0 is the screen, 1 the off-screen buffer */
static void w_fb_create(void){ mm_fb_create(); A = 0; }
static void w_fb_close(void) { mm_fb_close(); A = 0; }
static void w_fb_write(void) { mm_fb_write(LL(0)); A = 0; }
static void w_fb_copy(void)  { mm_fb_copy(LL(0), LL(2), LL(4)); A = 0; }
static void w_fb_wait(void)  { mm_fb_wait(); A = 0; }

/* LONGSTRING */
static void w_ls_len(void)   { A = mm_ls_len(PI(0)); }
static void w_ls_clear(void) { mm_ls_clear(PI(0), I(1)); A = 0; }
static void w_ls_append(void){ mm_ls_append(PI(0), I(1), Ps(2)); A = 0; }
static void w_ls_load(void)  { mm_ls_load(PI(0), I(1), LL(2), Ps(4)); A = 0; }
static void w_ls_copy(void)  { mm_ls_copy(PI(0), I(1), PI(2)); A = 0; }
static void w_ls_concat(void){ mm_ls_concat(PI(0), I(1), PI(2)); A = 0; }
static void w_ls_left(void)  { mm_ls_left(PI(0), I(1), PI(2), LL(3)); A = 0; }
static void w_ls_right(void) { mm_ls_right(PI(0), I(1), PI(2), LL(3)); A = 0; }
static void w_ls_mid(void)   { mm_ls_mid(PI(0), I(1), PI(2), LL(3), LL(5)); A = 0; }
static void w_ls_replace(void){ mm_ls_replace(PI(0), I(1), Ps(2), LL(3)); A = 0; }
static void w_ls_resize(void){ mm_ls_resize(PI(0), I(1), LL(2)); A = 0; }
static void w_ls_setbyte(void){ mm_ls_setbyte(PI(0), I(1), LL(2), LL(4)); A = 0; }
static void w_ls_trim(void)  { mm_ls_trim(PI(0), I(1), LL(2)); A = 0; }
static void w_ls_ucase(void) { mm_ls_ucase(PI(0)); A = 0; }
static void w_ls_lcase(void) { mm_ls_lcase(PI(0)); A = 0; }
static void w_ls_print(void) { mm_ls_print(LL(0), PI(2), I(3)); A = 0; }
static void w_ls_getstr(void){ A = mm_off(mm_ls_getstr(PI(0), LL(1), LL(3))); }
static void w_ls_getbyte(void){ A = mm_ls_getbyte(PI(0), LL(1), I(3)); }
static void w_ls_instr(void) { A = mm_ls_instr(PI(0), Ps(1), LL(2)); }
static void w_ls_compare(void){ A = mm_ls_compare(PI(0), PI(1)); }
static void w_ls_input(void) { A = mm_ls_input(PI(0), I(1), LL(2), LL(4)); }

/* GOSUB / RETURN */
static void w_gosub_push(void){ mm_gosub_push(I(0)); A = 0; }
static void w_gosub_pop(void){ A = mm_gosub_pop(); }

/* misc */
static void w_error(void)    { mm_error(Pa(0)); }
static void w_end(void)      { mm_end(); }
static void w_timer(void)    { A = dput(mm_timer()); }

/*
 *	One block for every array and string in the program, from the
 *	VM heap - which is itself PSRAM on the board (see heap_init in
 *	bcrun.c), so this is megabytes and costs no syscall.  It used to
 *	go straight to the kernel here; that was one ioctl pair per call
 *	and only tolerable because a program allocates once.
 */
static void w_heap(void)
{
	unsigned long n = (unsigned long)(uint32_t)arg(0);

	A = lib_malloc(n);
	if (A)
		memset(vptr(A), 0, n);	/* mm_heap zeroes; so must this */
	else
		mm_error("out of memory for arrays and strings");
}

/*
 *	The same heap, per invocation, for a routine's LOCAL arrays and
 *	strings.  Separate from w_heap only so the two can be tuned apart
 *	later: this one runs on every call and w_heap runs once.
 */
static void w_lheap(void)
{
	unsigned long n = (unsigned long)(uint32_t)arg(0);

	A = lib_malloc(n);
	if (A)
		memset(vptr(A), 0, n);
	else
		mm_error("out of memory for LOCAL arrays and strings");
}

static void w_lfree(void)
{
	lib_free((unsigned long)(uint32_t)arg(0));
	A = 0;
}

/* ---- name table ----------------------------------------------------- */

/*
 *	Looked up once per symbol: libcall() caches the result in
 *	libbind[], so the cost of the scan is paid at most once per name
 *	per run, not per call.
 */
static const struct mmwrap {
	const char *name;
	void (*fn)(void);
} mmwtab[] = {
	{ "mm_tmp",		w_tmp },
	{ "mm_mark",		w_mark },
	{ "mm_release",		w_release },
	{ "mm_byref_f",		w_byref_f },
	{ "mm_byref_i",		w_byref_i },
	{ "mm_sset",		w_sset },
	{ "mm_ssetm",		w_ssetm },
	{ "mm_ssetc",		w_ssetc },
	{ "mm_ssetn",		w_ssetn },
	{ "mm_scat",		w_scat },
	{ "mm_scmp",		w_scmp },
	{ "mm_scopy",		w_scopy },
	{ "mm_putc",		w_putc },
	{ "mm_pr_s",		w_pr_s },
	{ "mm_pr_i",		w_pr_i },
	{ "mm_pr_f",		w_pr_f },
	{ "mm_pr_nl",		w_pr_nl },
	{ "mm_pr_se",		w_pr_se },
	{ "mm_pr_ie",		w_pr_ie },
	{ "mm_pr_fe",		w_pr_fe },
	{ "mm_pr_tabe",	w_pr_tabe },
	{ "mm_pr_tab",		w_pr_tab },
	{ "mm_col",		w_col },
	{ "mm_tab",		w_tab },
	{ "mm_int_to_str",	w_int_to_str },
	{ "mm_int_to_str_pad",	w_int_to_str_pad },
	{ "mm_float_to_str",	w_float_to_str },
	{ "mm_toint",		w_toint },
	{ "mm_idiv",		w_idiv },
	{ "mm_mod",		w_mod },
	{ "mm_fdiv",		w_fdiv },
	{ "mm_pow",		w_pow },
	{ "mm_sqr",		w_sqr },
	{ "mm_log",		w_log },
	{ "mm_asin",		w_asin },
	{ "mm_acos",		w_acos },
	{ "mm_atan3",		w_atan3 },
	{ "mm_rnd",		w_rnd },
	{ "mm_randomize",	w_randomize },
	{ "mm_sgn",		w_sgn },
	{ "mm_int",		w_int },
	{ "mm_fix",		w_fix },
	{ "mm_asc",		w_asc },
	{ "mm_instr",		w_instr },
	{ "mm_val",		w_val },
	{ "mm_chr",		w_chr },
	{ "mm_left",		w_left },
	{ "mm_right",		w_right },
	{ "mm_mid",		w_mid },
	{ "mm_ucase",		w_ucase },
	{ "mm_lcase",		w_lcase },
	{ "mm_ltrim",		w_ltrim },
	{ "mm_rtrim",		w_rtrim },
	{ "mm_space",		w_space },
	{ "mm_strrep",		w_strrep },
	{ "mm_str_f",		w_str_f },
	{ "mm_str_i",		w_str_i },
	{ "mm_hex",		w_hex },
	{ "mm_oct",		w_oct },
	{ "mm_bin",		w_bin },
	{ "mm_byte",		w_byte },
	{ "mm_trim",		w_trim },
	{ "mm_field",		w_field },
	{ "mm_format",		w_format },
	{ "mm_mid_assign",	w_mid_assign },
	{ "mm_epoch_now",	w_epoch_now },
	{ "mm_epoch_str",	w_epoch_str },
	{ "mm_datetime",	w_datetime },
	{ "mm_time_str",	w_time_str },
	{ "mm_date_str",	w_date_str },
	{ "mm_day",		w_day },
	{ "mm_bin2str",		w_bin2str },
	{ "mm_str2bin_f",	w_str2bin_f },
	{ "mm_str2bin_i",	w_str2bin_i },
	{ "mm_open",		w_open },
	{ "mm_close",		w_close },
	{ "mm_close_all",	w_close_all },
	{ "mm_fpr_s",		w_fpr_s },
	{ "mm_fpr_i",		w_fpr_i },
	{ "mm_fpr_f",		w_fpr_f },
	{ "mm_fpr_nl",		w_fpr_nl },
	{ "mm_fpr_tab",		w_fpr_tab },
	{ "mm_eof",		w_eof },
	{ "mm_loc",		w_loc },
	{ "mm_lof",		w_lof },
	{ "mm_seek",		w_seek },
	{ "mm_getline",		w_getline },
	{ "mm_input_str",	w_input_str },
	{ "mm_input_line",	w_input_line },
	{ "mm_input_next",	w_input_next },
	{ "mm_atoi",		w_atoi },
	{ "mm_atof",		w_atof },
	{ "mm_kill",		w_kill },
	{ "mm_rename",		w_rename },
	{ "mm_copy",		w_fcopy },
	{ "mm_mkdir",		w_mkdir },
	{ "mm_rmdir",		w_rmdir },
	{ "mm_chdir",		w_chdir },
	{ "mm_cwd",		w_cwd },
	{ "mm_inkey",		w_inkey },
	{ "mm_at",		w_at },
	{ "mm_dir",		w_dir },
	{ "mm_files",		w_files },
	{ "mm_data_init4",	w_data_init4 },
	{ "mm_restore",		w_restore },
	{ "mm_read_f",		w_read_f },
	{ "mm_read_i",		w_read_i },
	{ "mm_read_s",		w_read_s },
	{ "mm_read_save",	w_read_save },
	{ "mm_read_unsave",	w_read_unsave },
	{ "mm_sort_i",		w_sort_i },
	{ "mm_sort_f",		w_sort_f },
	{ "mm_sort_s",		w_sort_s },
	{ "mm_arr_count",	w_arr_count },
	{ "mm_arr_set_i",	w_arr_set_i },
	{ "mm_arr_set_f",	w_arr_set_f },
	{ "mm_arr_set_s",	w_arr_set_s },
	{ "mm_arr_add_i",	w_arr_add_i },
	{ "mm_arr_add_f",	w_arr_add_f },
	{ "mm_arr_add_s",	w_arr_add_s },
	{ "mm_arr_scale_i",	w_arr_scale_i },
	{ "mm_arr_scale_f",	w_arr_scale_f },
	{ "mm_st_sum_i",	w_st_sum_i },
	{ "mm_st_sum_f",	w_st_sum_f },
	{ "mm_st_mean_i",	w_st_mean_i },
	{ "mm_st_mean_f",	w_st_mean_f },
	{ "mm_st_sd_i",		w_st_sd_i },
	{ "mm_st_sd_f",		w_st_sd_f },
	{ "mm_st_max_i",	w_st_max_i },
	{ "mm_st_max_f",	w_st_max_f },
	{ "mm_st_min_i",	w_st_min_i },
	{ "mm_st_min_f",	w_st_min_f },
	{ "mm_st_med_i",	w_st_med_i },
	{ "mm_st_med_f",	w_st_med_f },
	{ "mm_pause",		w_pause },
	{ "mm_error_s",		w_error_s },
	{ "mm_err_bind",	w_err_bind },
	{ "mm_on_error",	w_on_error },
	{ "mm_errno",		w_errno },
	{ "mm_errmsg",		w_errmsg },
	{ "mm_ver",		w_ver },
	{ "mm_device",		w_device },
	{ "mm_cmdline",		w_cmdline },
	{ "mm_argv_bind",	w_argv_bind },
	{ "mm_timer_set",	w_timer_set },
	{ "mm_run_begin",	w_run_begin },
	{ "mm_run_arg",		w_run_arg },
	{ "mm_run_arg_i",	w_run_arg_i },
	{ "mm_run_arg_f",	w_run_arg_f },
	{ "mm_run_exec",	w_run_exec },
	{ "mm_run_bg",		w_run_bg },
	{ "mm_play_start",	w_play_start },
	{ "mm_play_stop",	w_play_stop },
	{ "mm_set_date",	w_set_date },
	{ "mm_set_time",	w_set_time },
	{ "mm_pixel",		w_pixel },
	{ "mm_pixel_get",	w_pixel_get },
	{ "mm_cls",		w_cls },
	{ "mm_line",		w_line },
	{ "mm_hres",		w_hres },
	{ "mm_vres",		w_vres },
	{ "mm_mode",		w_mode },
	{ "mm_colour",		w_colour },
	{ "mm_fg",		w_fg },
	{ "mm_bg",		w_bg },
	{ "mm_plot",		w_plot },
	{ "mm_fill",		w_fill },
	{ "mm_pixels",		w_pixels },
	{ "mm_gpio",		w_gpio },
	{ "mm_map",		w_map },
	{ "mm_map_set",		w_map_set },
	{ "mm_map_reset",	w_map_reset },
	{ "mm_map_get",		w_map_get },
	{ "mm_fontinfo",	w_fontinfo },
	{ "mm_font",		w_font },
	{ "mm_gtext",		w_gtext },
	{ "mm_fb_create",	w_fb_create },
	{ "mm_fb_close",	w_fb_close },
	{ "mm_fb_write",	w_fb_write },
	{ "mm_fb_copy",		w_fb_copy },
	{ "mm_fb_wait",		w_fb_wait },
	{ "mm_ls_len",		w_ls_len },
	{ "mm_ls_clear",	w_ls_clear },
	{ "mm_ls_append",	w_ls_append },
	{ "mm_ls_load",		w_ls_load },
	{ "mm_ls_copy",		w_ls_copy },
	{ "mm_ls_concat",	w_ls_concat },
	{ "mm_ls_left",		w_ls_left },
	{ "mm_ls_right",	w_ls_right },
	{ "mm_ls_mid",		w_ls_mid },
	{ "mm_ls_replace",	w_ls_replace },
	{ "mm_ls_resize",	w_ls_resize },
	{ "mm_ls_setbyte",	w_ls_setbyte },
	{ "mm_ls_trim",		w_ls_trim },
	{ "mm_ls_ucase",	w_ls_ucase },
	{ "mm_ls_lcase",	w_ls_lcase },
	{ "mm_ls_print",	w_ls_print },
	{ "mm_ls_getstr",	w_ls_getstr },
	{ "mm_ls_getbyte",	w_ls_getbyte },
	{ "mm_ls_instr",	w_ls_instr },
	{ "mm_ls_compare",	w_ls_compare },
	{ "mm_ls_input",	w_ls_input },
	{ "mm_gosub_push",	w_gosub_push },
	{ "mm_gosub_pop",	w_gosub_pop },
	{ "mm_error",		w_error },
	{ "mm_end",		w_end },
	{ "mm_timer",		w_timer },
	{ "mm_heap",		w_heap },
	{ "mm_lheap",		w_lheap },
	{ "mm_lfree",		w_lfree },
	{ NULL,			NULL }
};

static void (*mm_wrap_lookup(const char *name))(void)
{
	const struct mmwrap *w;
	for (w = mmwtab; w->name; w++)
		if (strcmp(w->name, name) == 0)
			return w->fn;
	return NULL;
}

/* ---- VM space for the pools ----------------------------------------- */

#define MMRT_BYREFSZ	((unsigned long)MM_BYREFN * sizeof(union mm_byref_u))
#define MMRT_POOLSZ	((unsigned long)MM_TMPN * MM_STRSZ)

/*
 *	Called from load() once the symbol and string tables are in: if
 *	the program imports any mm_* name, carve the scratch pool and the
 *	by-ref pool out of VM memory between bss and the heap (by-ref
 *	first: it holds doubles and the base is rounded to 8).  Returns
 *	the first address the heap may use.
 */
/*
 *	What mmrt_reserve will want, for whoever has to size mem[] before
 *	this file is reached.  Always counted rather than only when the
 *	program imports an mm_* name: it is four kilobytes, every
 *	translated BASIC program imports one, and a sizing rule that
 *	depends on the symbol table is a rule that will be wrong once.
 */
static unsigned long mmrt_bytes(void)
{
	return MMRT_BYREFSZ + MMRT_POOLSZ;
}

static unsigned long mmrt_reserve(unsigned long base)
{
	unsigned long i;

	for (i = 0; i < h.h_nsym; i++)
		if (sym[i].s_type == BC_SYM_LIB &&
		    strncmp(strtab + sym[i].s_name, "mm_", 3) == 0)
			break;
	if (i == h.h_nsym)
		return base;

	base = (base + 7) & ~7UL;
	if (base + MMRT_BYREFSZ + MMRT_POOLSZ + STACKROOM > MEMTOP) {
		fprintf(stderr, "bcrun: no room for the mm runtime pool\n");
		exit(1);
	}
	mm_hosted_bind((char *)vptr(base + MMRT_BYREFSZ), vptr(base));
	return base + MMRT_BYREFSZ + MMRT_POOLSZ;
}

/*
 *	The BASIC runtime's CMM2 platform (mmbrun only): included three
 *	times from Applications/mmb2c/mmb_runtime.c, MMB_CMM2_PART saying
 *	which, in place of the PC3's code at the same three places.
 *
 *	  1  the helpers a translated program runs (LOAD IMAGE's loadimage,
 *	     PLAY's players) and PLAY's control: the PC3 forks and execs
 *	     programs for them, which the CMM2 cannot; its firmware has them
 *	     as commands, so the helper and its arguments become the
 *	     firmware's own statement (the services' basic_cmd)
 *	  2  the display: drawing by the firmware's routines (the services'
 *	     version 10 drawing), MODE, MAP and the text cursor as the
 *	     firmware's statements, and a framebuffer's bytes moved straight
 *	     in and out of the page drawn on (gfx_target)
 *	  3  FRAMEBUFFER's five hooks, the firmware's FRAMEBUFFER statements
 *
 *	PRINT is the firmware's console's (stdout is the services' con_putc),
 *	which draws in any mode, at the text cursor, through FRAMEBUFFER
 *	WRITE's page, and scrolls: so mm_gon stays 0 and the PC3's glyph
 *	engine is not needed.  MODE n is the PicoMite's: 1 640x480 in one
 *	bit, 2 320x240 and 3 640x480 in RGB121 (the firmware's MODE 8,1, 7,4
 *	and 8,4: docs/PicoMite_CMM2_Mode_Map.md).  A framebuffer's raw bytes
 *	are the PC3's (4 bits a pixel the high nibble on the left, 1 bit the
 *	high bit), the firmware's pages the PicoMite's (the low ones on the
 *	left), so each byte is turned round on its way.  (Peter, 4 Nov 2026:
 *	the firmware's code, not copies of it, for petscii.)
 */
#if MMB_CMM2_PART == 1			/* (the first of the three: what all of them use) */

#include "cmm2.h"			/* tools/sdk's: the services table */
#include "mmb_playctl.h"		/* PLAY's records (MM_PLAY_MODSAMP...) */

/* a statement for the firmware (basic_cmd), and its error as the program's */
static int mm_cmm2_cmd(const char *line)
{
	int r = cmm2->basic_cmd(line);

	if (r < 0)
		mm_error(r == -4 ? cmm2->io_error() : "Statement too long");
	return r;
}

/* A translated program, at its load (mmrt_reserve).  Its output is
   MMBasic's, a PRINT's CR LF its own and a CHR$(10) a bare LF, so the
   console's newline rule, for C programs, is off.  (Peter, 5 Nov 2026:
   the output the interpreter's, byte for byte.) */
static void mm_cmm2_start(void)
{
	struct termios t;

	if (tcgetattr(1, &t) == 0) {
		t.c_oflag &= ~(unsigned)ONLCR;
		tcsetattr(1, TCSANOW, &t);
	}
}

/* The helper programs the translator asks for, and the firmware's statement
   for each: its arguments after the first quoted as a file name, the rest as
   they are (numbers), up to max of them */
static const struct {
	const char *prog, *stmt;
	int max;
} mm_cmm2_helpers[] = {
	{ "loadimage", "LOAD IMAGE", 3 }, { "loadjpg", "LOAD JPG", 8 },
	{ "loadpng", "LOAD PNG", 5 }, { "saveimage", "SAVE IMAGE", 5 },
	{ "playmod", "PLAY MODFILE", 1 }, { "playmp3", "PLAY MP3", 1 },
	{ "playwav", "PLAY WAV", 1 }, { "playflac", "PLAY FLAC", 1 },
	{ NULL, NULL, 0 }
};

static int mm_cmm2_playing;		/* a PLAY of ours is on (PLAY STOP ends it) */
static int mm_cmm2_effect;		/* a PLAY EFFECT's end is awaited */

static MMINTEGER mm_cmm2_run(void)
{
	char line[320];
	const char *p;
	int i, k, n;

	if (mm_run_nargs < 1)
		return -1;
	p = strrchr(mm_run_argv[0], '/');
	p = p ? p + 1 : mm_run_argv[0];
	for (k = 0; mm_cmm2_helpers[k].prog; k++)
		if (!strcmp(p, mm_cmm2_helpers[k].prog))
			break;
	if (!mm_cmm2_helpers[k].prog || mm_run_nargs < 2 || !strcmp(mm_run_argv[1], "-s")) {
		mm_error("Not available on the CMM2");
		return -1;
	}
	n = snprintf(line, sizeof(line), "%s \"%s\"", mm_cmm2_helpers[k].stmt, mm_run_argv[1]);
	for (i = 2; i < mm_run_nargs && i <= mm_cmm2_helpers[k].max && n < (int)sizeof(line); i++)
		n += snprintf(line + n, sizeof(line) - n, ",%s", mm_run_argv[i]);
	if (mm_cmm2_cmd(line) < 0)
		return -1;
	if (!strncmp(mm_cmm2_helpers[k].stmt, "PLAY", 4))
		mm_cmm2_playing = 1;
	return 0;
}

MMINTEGER mm_run_exec(void) { return mm_cmm2_run(); }
MMINTEGER mm_run_bg(void) { return mm_cmm2_run() < 0 ? -1 : 1; }

int mm_run_pipe(void)
{
	mm_error("Not available on the CMM2");
	return -1;
}

MMINTEGER mm_run_pipe_close(int fd) { (void)fd; return -1; }
MMINTEGER mm_run_pipe_read(int fd, void *buf, int n) { (void)fd; (void)buf; (void)n; return -1; }

void mm_restart(void)
{
	mm_error("CPU RESTART is not available in a compiled program");
}

/* The player is the firmware: "owned" while a PLAY of ours is on, or an
   effect over it.  mmb_int.h's watch for a compiled interrupt asks this */
MMINTEGER mm_play_owner(void)
{
	int s = cmm2->sound_state();

	if (mm_cmm2_effect) {
		if (!(s & 0x100) && (s & 0xFF))
			return 1;
		mm_cmm2_effect = 0;
		return 0;
	}
	return mm_cmm2_playing && (s & 0xFF);
}

/* PLAY's records to the player: MODSAMPLE and VOLUME, as the statements */
MMINTEGER mm_play_send(MMINTEGER op, MMINTEGER a, MMINTEGER b,
		       MMINTEGER p1, MMINTEGER p2, MMINTEGER p3)
{
	char line[64];

	(void)p3;
	if (op == MM_PLAY_MODSAMP)
		snprintf(line, sizeof(line), "PLAY MODSAMPLE %d,%d,%d", (int)a, (int)b, (int)p1);
	else if (op == MM_PLAY_VOLUME)
		snprintf(line, sizeof(line), "PLAY VOLUME %d,%d", (int)p1, (int)p2);
	else
		return -1;
	return cmm2->basic_cmd(line) < 0 ? -1 : 0;
}

MMINTEGER mm_snd_cmd(MMINTEGER op, MMINTEGER a, MMINTEGER b,
		     MMINTEGER p1, MMINTEGER p2, MMINTEGER p3)
{
	(void)op; (void)a; (void)b; (void)p1; (void)p2; (void)p3;
	return -1;
}

MMINTEGER mm_snd_stop(void) { return 0; }

MMINTEGER mm_bbc_sound(MMINTEGER chan, MMINTEGER amp, MMINTEGER pitch, MMINTEGER dur)
{
	(void)chan; (void)amp; (void)pitch; (void)dur;
	return -1;
}

MMINTEGER mm_bbc_env(MMINTEGER lo, MMINTEGER hi) { (void)lo; (void)hi; return -1; }
void mm_bbc_quiet(void) { }

MMINTEGER mm_play_start(void)
{
	if (mm_play_owner()) {
		mm_error("sound output in use");
		return -1;
	}
	return mm_run_bg();
}

MMINTEGER mm_play_stop(void)
{
	if (mm_cmm2_playing) {
		cmm2->basic_cmd("PLAY STOP");
		mm_cmm2_playing = mm_cmm2_effect = 0;
	}
	return 0;
}

#elif MMB_CMM2_PART == 2

/* Nothing is queued here (each primitive is one service call) */
static void mm_pix_drain(void) {}

/* /dev/sys, which the PC3's I2C, SPI and RTC clients above still open
   (cmm2_sys.c answers them: nothing there yet) */
static int mm_gfx_open(void)
{
	return mm_sys_open();
}

/* the colour a primitive draws in: MM_CUR is COLOUR's */
#define MM_CMM2_COL(c)	((int)((c) == MM_CUR ? mm_gfx_fg : ((c) & MM_COLBITS)))

/* ---- pins: the PC3's /dev/sys clients have nothing to talk to, and
   the version 7 services are not wired in yet: the host build's answers */
MMINTEGER mm_gpio(MMINTEGER op, MMINTEGER pin, MMINTEGER val)
{
	(void)op; (void)pin; (void)val;
	return 0;
}

MMINTEGER mm_pinowner(MMINTEGER pin) { return mm_pin_board(pin) ? 2 : 0; }

MMINTEGER mm_pinct(MMINTEGER op, MMINTEGER pin, MMINTEGER val)
{
	static MMINTEGER cnt[4];

	if (pin < 4 || pin > 7)
		return op == MM_PINCT_READ ? 0 : -1;
	switch (op) {
	case MM_PINCT_FIN:
	case MM_PINCT_CIN:
	case MM_PINCT_PER:
	case MM_PINCT_OFF:
		cnt[pin - 4] = 0;
		return 0;
	case MM_PINCT_READ:
		return cnt[pin - 4];
	case MM_PINCT_SET:
		cnt[pin - 4] = val;
		return 0;
	}
	return -1;
}

MMINTEGER mm_pincap(MMINTEGER op, MMINTEGER pin, MMINTEGER arg)
{
	(void)pin; (void)arg;
	return (op == MM_PINCAP_ARM || op == MM_PINCAP_OFF || op == MM_PINCAP_POLL) ? 0 : -1;
}

MMINTEGER mm_pobuf(void) { return 0; }

/* CLS: the firmware's, on the page drawn on, which is the console's too,
   and the text cursor home (mm_cls: the PC3's console clear is ANSI) */
static void mm_cmm2_cls(MMINTEGER rgb)
{
	fflush(stdout);
	cmm2->gfx_cls((int)((rgb == MM_CUR ? mm_bg() : rgb) & MM_COLBITS));
	mm_gn = 0;
	mm_gx = mm_gy = 0;
}

/* ---- text: the firmware's console draws PRINT; @() moves its cursor */
static void mm_gflush(void) {}
static int mm_gputc(int c) { (void)c; return 0; }
static void mm_curs_show(int on) { (void)on; }

char *mm_at(MMINTEGER x, MMINTEGER y, MMINTEGER mode)
{
	char line[48];

	mm_gx = (int)x;
	mm_gy = (int)y;
	fflush(stdout);			/* what came before, where it was */
	if (mode > 0 && mode <= 7)
		snprintf(line, sizeof(line), "PRINT @(%d,%d,%d);", (int)x, (int)y, (int)mode);
	else
		snprintf(line, sizeof(line), "PRINT @(%d,%d);", (int)x, (int)y);
	mm_cmm2_cmd(line);
	return mm_tmp();		/* "" */
}

/* ---- fonts: the firmware's, and a program's own as DefineFont makes them */
static int mm_gfont = 1, mm_gscale = 1, mm_gcw = 8, mm_gch = 12;

MMINTEGER mm_fontinfo(MMINTEGER font, MMINTEGER *w, MMINTEGER *h)
{
	int fw = 0, fh = 0;

	if (w) *w = 0;
	if (h) *h = 0;
	if (cmm2->font_size((int)font, &fw, &fh) < 0)
		return 0;			/* no such font */
	if (w) *w = fw;
	if (h) *h = fh;
	return 1;
}

MMINTEGER mm_fontaddr(MMINTEGER font) { (void)font; return 0; }

MMINTEGER mm_fontdef(MMINTEGER font, MMINTEGER addr, MMINTEGER bytes)
{
	(void)bytes;
	return cmm2->font_define((int)font, (const uint8_t *)(uintptr_t)addr) < 0 ? -1 : 0;
}

void mm_font(MMINTEGER font, MMINTEGER scale)
{
	int w = 0, h = 0;

	if (scale < 1)
		scale = 1;
	if (scale > 15)
		scale = 15;
	if (font >= 1 && cmm2->gfx_font((int)font, (int)scale, &w, &h) == 0 && w && h) {
		mm_gfont = (int)font;
		mm_gscale = (int)scale;
		mm_gcw = w / (int)scale;
		mm_gch = h / (int)scale;
	}
}

MMINTEGER mm_fontwidth(void)  { return (MMINTEGER)mm_gcw * mm_gscale; }
MMINTEGER mm_fontheight(void) { return (MMINTEGER)mm_gch * mm_gscale; }

void mm_font_cur(MMINTEGER *font, MMINTEGER *scale)
{
	if (font)
		*font = mm_gfont;
	if (scale)
		*scale = mm_gscale;
}

/* OPTION CONSOLE's screen half: the services' con_screen */
static void mm_con_mirror(int on)
{
	cmm2->con_screen(on ? 1 : 0);
}

/* ---- MAP: the firmware's statements (a 4-bit mode's palette) */
void mm_map(MMINTEGER index, MMINTEGER rgb)
{
	char line[48];

	if (index < 0 || index > 15) {
		mm_error("MAP index must be 0 to 15");
		return;
	}
	snprintf(line, sizeof(line), "MAP(%d)=%ld", (int)index, (long)(rgb & 0xFFFFFF));
	mm_cmm2_cmd(line);
}

void mm_map_set(void)   { mm_cmm2_cmd("MAP SET"); }
void mm_map_reset(void) { mm_cmm2_cmd("MAP RESET"); }

/* ---- drawing: the firmware's routines (version 10) */
void mm_gtext(MMINTEGER x, MMINTEGER y, MMINTEGER font, MMINTEGER scale,
	      MMINTEGER fg, MMINTEGER bg, const char *s, MMINTEGER len,
	      MMINTEGER orient)
{
	char buf[256], just[4] = { 'L', 'T', 'N', 0 };

	if (len <= 0)
		return;
	if (len > (MMINTEGER)sizeof(buf) - 1)
		len = sizeof(buf) - 1;
	memcpy(buf, s, (size_t)len);
	buf[len] = 0;
	if (orient > 0 && orient <= 4)
		just[2] = "NVIUD"[orient];
	if (scale < 1)
		scale = mm_gscale;
	if (font < 1)
		font = mm_gfont;
	cmm2->gfx_text((int)x, (int)y, buf, just, (int)font, (int)scale, MM_CMM2_COL(fg),
		       bg < 0 ? -1 : (int)(bg & 0xFFFFFF));
	mm_gx = (int)x;
	mm_gy = (int)y;
}

void mm_line(MMINTEGER x1, MMINTEGER y1, MMINTEGER x2, MMINTEGER y2, MMINTEGER rgb)
{
	cmm2->gfx_line((int)x1, (int)y1, (int)x2, (int)y2, 1, MM_CMM2_COL(rgb));
}

/* after the firmware's MODE */
static void mm_cmm2_moded(void)
{
	int w = 0, h = 0;

	mm_fb_forget();
	mm_gx = mm_gy = 0;
	mm_gfont = mm_gscale = 1;
	if (cmm2->gfx_font(0, 0, &w, &h) == 0 && w && h) {
		mm_gcw = w;
		mm_gch = h;
	}
}

/* MODE n, one number (MODE r, bits goes to the firmware as written, by
   cmm2_stmt): run by the firmware too, as the interpreter runs it - the
   Colour Maximite 2's mode n, or under OPTION PICOMITE the PicoMite's (the
   firmware maps 1-3 itself: 8,1, 7,4 and 8,4, the table this had).  It was
   always the PicoMite's here: MODE 9 was refused and MODE 1 gave 640 x 480
   in one bit (a Back Shed user's Life program, 10 Nov 2026).  RESOLUTION's
   mm_mode(1) gets the firmware's MODE 1 the same way. */
void mm_mode(MMINTEGER n)
{
	char line[24];

	snprintf(line, sizeof(line), "MODE %d", (int)n);
	fflush(stdout);
	if (mm_cmm2_cmd(line) < 0)
		return;
	mm_cmm2_moded();
}

/* A program written for the CMM2 (mmb2c --cmm2): the statements only the
   firmware has run by the firmware, on the values the compiled expressions
   give them.  Each value is pushed as it is worked out, and \001 in the
   text takes them in order.  A stack, as an argument may call a FUNCTION
   that runs a statement of its own: each mm_fw takes only its own */
static int mm_cmm2_target(cmm2_target *t);
#define MM_FW_MAX 32
static struct {
	char kind;			/* i, f or s */
	union {
		MMINTEGER i;
		MMFLOAT f;
		const char *s;
	} v;
} mm_fw_arg[MM_FW_MAX];
static int mm_fw_n;

/* what the runtime keeps of a statement the firmware has run */
static void mm_fw_after(const char *line)
{
	if (line[0] == 'M' && !strncmp(line, "MODE", 4))
		mm_cmm2_moded();
	else if (line[0] == 'P' && !strncmp(line, "PLAY ", 5)) {
		mm_cmm2_playing = 1;
		if (!strncmp(line + 5, "EFFECT", 6))
			mm_cmm2_effect = 1;
	}
}

void mm_fwi(MMINTEGER v)
{
	if (mm_fw_n < MM_FW_MAX) {
		mm_fw_arg[mm_fw_n].kind = 'i';
		mm_fw_arg[mm_fw_n++].v.i = v;
	}
}

void mm_fwf(MMFLOAT v)
{
	if (mm_fw_n < MM_FW_MAX) {
		mm_fw_arg[mm_fw_n].kind = 'f';
		mm_fw_arg[mm_fw_n++].v.f = v;
	}
}

void mm_fws(const char *s)
{
	if (mm_fw_n < MM_FW_MAX) {
		mm_fw_arg[mm_fw_n].kind = 's';
		mm_fw_arg[mm_fw_n++].v.s = s;
	}
}

/* Each statement in the program, tokenised at its first use and run from
   its tokens after that (basic_prep): text is the translator's constant, so
   its address says which statement this is.  Tokenising took 80-190us a
   statement, more than most of them take to run.  tok NULL: by text */
#define MM_FW_SITES 1024		/* (a power of 2) */
static struct mm_fw_site {
	const char *text;
	char *tok;
	int len;
	int *arg;			/* IMAGE RESIZE_FAST's ten, or NULL */
} *mm_fw_tab;
static int mm_fw_used;

/* IMAGE RESIZE_FAST goes straight to the firmware's work, with no parsing:
   it is the statement a raycaster runs for every column of a wall.  Each of
   its ten arguments is known here or a value to come, MM_FW_VAL; the
   firmware's -1 is the word FRAMEBUFFER and -2 none given */
#define MM_FW_VAL (-3)

static void mm_fw_resize(struct mm_fw_site *s, const char *p)
{
	int a[10], n = 0, i;

	if (strncmp(p, "IMAGE RESIZE_FAST ", 18))
		return;
	p += 18;
	for (i = 0; i < 10; i++)
		a[i] = -2;
	for (;;) {
		if (*p == 1) {
			a[n] = MM_FW_VAL;
			p++;
		} else if (!strncmp(p, "FRAMEBUFFER", 11)) {
			a[n] = -1;
			p += 11;
		} else if (*p >= '0' && *p <= '9') {
			a[n] = 0;
			while (*p >= '0' && *p <= '9')
				a[n] = a[n] * 10 + *p++ - '0';
		} else if (*p != ',' && *p != 0) {
			return;		/* (a negative number: the firmware's say) */
		}
		n++;
		if (*p == 0)
			break;
		if (*p != ',' || n == 10)
			return;
		p++;
	}
	if (n >= 8 && (s->arg = malloc(sizeof(a))) != NULL)
		memcpy(s->arg, a, sizeof(a));
}

static struct mm_fw_site *mm_fw_site(const char *text, const char *line)
{
	char tok[320];
	unsigned h = ((unsigned)(unsigned long)text * 2654435761u) >> 22;
	struct mm_fw_site *s;
	int n;

	if (mm_fw_tab == NULL
	    && (mm_fw_tab = calloc(MM_FW_SITES, sizeof(*mm_fw_tab))) == NULL)
		return NULL;
	for (;;) {
		s = &mm_fw_tab[h & (MM_FW_SITES - 1)];
		if (s->text == text)
			return s;
		if (s->text == NULL)
			break;
		h++;
	}
	if (mm_fw_used >= MM_FW_SITES / 2)
		return NULL;
	mm_fw_used++;
	s->text = text;
	n = cmm2->basic_prep(line, tok, sizeof(tok));
	if (n > 0 && (s->tok = malloc(n)) != NULL) {
		memcpy(s->tok, tok, n);
		s->len = n;
		mm_fw_resize(s, line);
	}
	return s;
}

/* the digits of v at p, and where they end */
static char *mm_fw_int(char *p, MMINTEGER v)
{
	char d[24];
	int n = 0;
	unsigned long long u = v < 0 ? 0 - (unsigned long long)v : (unsigned long long)v;

	if (v < 0)
		*p++ = '-';
	while (u > 0xFFFFFFFFULL) {	/* (a 64-bit divide only for what needs one) */
		d[n++] = '0' + (char)(u % 10);
		u /= 10;
	}
	{
		unsigned w = (unsigned)u;

		do {
			d[n++] = '0' + (char)(w % 10);
			w /= 10;
		} while (w);
	}
	while (n)
		*p++ = d[--n];
	return p;
}

/* the values written into a statement's tokens; 0 for one a token must
   stand in, a minus sign, or one that will not fit: by text then */
static int mm_fw_fill(const struct mm_fw_site *s, char *o, int size, int k)
{
	const char *t = s->tok, *e = t + s->len;
	char *end = o + size - 40;

	while (t < e) {
		if (o >= end)
			return 0;
		if (*t != 1) {
			*o++ = *t++;
			continue;
		}
		t++;
		if (mm_fw_arg[k].kind == 'i') {
			if (mm_fw_arg[k].v.i < 0)
				return 0;
			o = mm_fw_int(o, mm_fw_arg[k].v.i);
		} else if (mm_fw_arg[k].kind == 'f') {
			if (mm_fw_arg[k].v.f < 0)
				return 0;
			mm_float_to_str(o, mm_fw_arg[k].v.f, 0, MM_AUTO_PRECISION, ' ');
			o += strlen(o);
		} else {
			const char *v = mm_fw_arg[k].v.s;
			int m = (unsigned char)v[0];

			if (o + m + 2 >= end)
				return 0;
			*o++ = '"';
			memcpy(o, v + 1, m);
			o += m;
			*o++ = '"';
		}
		k++;
	}
	return 1;
}

/* a statement or expression as text, its values written in and taken off
   the stack; 0 if it will not fit */
static int mm_fw_expand(const char *text, char *line, int size)
{
	char *p = line, *end = line + size - 40;
	int len = (unsigned char)text[0], n = 0, i, k;

	for (i = 1; i <= len; i++)
		n += text[i] == 1;
	k = mm_fw_n - n;
	for (i = 1; i <= len && p < end; i++) {
		if (text[i] != 1) {
			*p++ = text[i];
		} else if (mm_fw_arg[k].kind == 'i') {
			p = mm_fw_int(p, mm_fw_arg[k++].v.i);
		} else if (mm_fw_arg[k].kind == 'f') {
			mm_float_to_str(p, mm_fw_arg[k++].v.f, 0, MM_AUTO_PRECISION, ' ');
			p += strlen(p);
		} else {
			const char *s = mm_fw_arg[k++].v.s;
			int m = (unsigned char)s[0];

			if (p + m + 2 >= end)
				p = end;
			else {
				*p++ = '"';
				memcpy(p, s + 1, m);
				p += m;
				*p++ = '"';
			}
		}
	}
	*p = 0;
	mm_fw_n -= n;
	return p < end;
}

void mm_fw(const char *text)
{
	char line[320];
	int len = (unsigned char)text[0], n = 0, i, k;
	struct mm_fw_site *s = NULL;

	if (mm_fw_tab != NULL) {	/* (seen before: the usual way here) */
		unsigned h = ((unsigned)(unsigned long)text * 2654435761u) >> 22;

		for (;;) {
			struct mm_fw_site *q = &mm_fw_tab[h & (MM_FW_SITES - 1)];

			if (q->text == text)
				s = q;
			if (q->text == text || q->text == NULL)
				break;
			h++;
		}
	}
	for (i = 1; i <= len; i++)
		n += text[i] == 1;
	k = mm_fw_n - n;
	if (s != NULL && s->arg != NULL) {
		long long a[10];

		for (i = 0; i < 10; i++)
			a[i] = s->arg[i] == MM_FW_VAL ? mm_fw_arg[k++].v.i : s->arg[i];
		if (a[8] >= -2 && a[9] >= -2 && !(s->arg[8] == MM_FW_VAL && a[8] < 0)
		    && !(s->arg[9] == MM_FW_VAL && a[9] < 0)) {
			mm_fw_n -= n;
			if (cmm2->image_resize(a) < 0)
				mm_error(cmm2->io_error());
			return;
		}
		k = mm_fw_n - n;	/* (a page of -1 in a variable: the firmware's error) */
	}
	if (s != NULL && s->tok != NULL && mm_fw_fill(s, line, sizeof(line), k)) {
		mm_fw_n -= n;
		if (cmm2->basic_exec(line) < 0)
			mm_error(cmm2->io_error());
		else
			mm_fw_after(text + 1);
		return;
	}
	if (s == NULL) {		/* its first time: the template, \001s and all */
		memcpy(line, text + 1, len);
		line[len] = 0;
		mm_fw_site(text, line);
	}
	if (!mm_fw_expand(text, line, sizeof(line))) {
		mm_error("Statement too long");
		return;
	}
	if (mm_cmm2_cmd(line) < 0)
		return;
	mm_fw_after(line);
}

char *mm_fw_str(const char *expr)
{
	char in[320], out[256];
	char *r = mm_tmp();

	if (!mm_fw_expand(expr, in, sizeof(in)))
		mm_error("Statement too long");
	else if (cmm2->basic_eval(in, out) < 0)
		mm_error(cmm2->io_error());
	else
		mm_ssetc(r, out);
	return r;
}

/* RGB(r, g, b [, t]), as fun_rgb: t < 0 is its three arguments' opaque; a
   32-bit mode's t is 0-255, of which it keeps the top four bits */
MMINTEGER mm_rgbt(MMINTEGER r, MMINTEGER g, MMINTEGER b, MMINTEGER t)
{
	cmm2_target x;

	if ((r | g | b) & ~(MMINTEGER)0xFF) {
		if (!mm_range(r, 0, 255) && !mm_range(g, 0, 255))
			mm_range(b, 0, 255);
		return 0;
	}
	if (t < 0)
		t = 15;
	else if (mm_cmm2_target(&x) == 0 && x.bpp == 32) {
		if (mm_range(t, 0, 255))
			return 0;
		t >>= 4;
	} else if (mm_range(t, 0, 15))
		return 0;
	return (t << 24) | (r << 16) | (g << 8) | b;
}

static int mm_cmm2_target(cmm2_target *t)
{
	return cmm2->gfx_target(t);
}

MMINTEGER mm_hres(void) { cmm2_target t; return mm_cmm2_target(&t) < 0 ? 0 : (MMINTEGER)t.width; }
MMINTEGER mm_vres(void) { cmm2_target t; return mm_cmm2_target(&t) < 0 ? 0 : (MMINTEGER)t.height; }

/* the PC3's raw framebuffer: the PicoMite's 4 and 1-bit pages only */
MMINTEGER mm_fb_geom(void)
{
	cmm2_target t;

	if (mm_cmm2_target(&t) < 0 || (t.bpp != 4 && t.bpp != 1))
		return -1;
	return ((MMINTEGER)t.pitch << 8) | (MMINTEGER)t.bpp;
}

int mm_board_no(void) { return 3; }
MMINTEGER mm_psram_size(void) { return 0; }

char *mm_lcdpanel(void)
{
	static char buf[MM_STRSZ];

	mm_ssetc(buf, "");
	return buf;
}

MMINTEGER mm_rand32(void) { return 0; }
char *mm_device(void) { return mm_dev_name(0); }

/* KEYDOWN: the services' keys_held (0 the count, 1-6 the keys, 7 the
   modifiers, 8 the locks, which it does not give) */
MMINTEGER mm_keydown(MMINTEGER n)
{
	int k[7] = { 0 }, c;

	if (n < 0 || n > 8)
		MM_RAISEV("KEYDOWN takes 0 to 8", 0);
	c = cmm2->keys_held(k);
	if (n == 0)
		return c;
	if (n == 8)
		return 0;
	return k[n - 1];
}

void mm_plot(const short *xy, MMINTEGER n, MMINTEGER rgb)
{
	MMINTEGER i;
	int c = MM_CMM2_COL(rgb);

	for (i = 0; i < n; i++)
		cmm2->gfx_pixel(xy[2 * i], xy[2 * i + 1], c);
}

void mm_fill(const short *xyxy, MMINTEGER n, MMINTEGER rgb)
{
	MMINTEGER i;
	int c = MM_CMM2_COL(rgb);

	for (i = 0; i < n; i++) {
		const short *r = xyxy + 4 * i;
		int w = r[2] - r[0], h = r[3] - r[1];

		cmm2->gfx_box(r[0], r[1], w + (w < 0 ? -1 : 1), h + (h < 0 ? -1 : 1), 0, c, c);
	}
}

void mm_pixels(const MMFLOAT *xf, const MMINTEGER *xi,
	       const MMFLOAT *yf, const MMINTEGER *yi,
	       const MMFLOAT *cf, const MMINTEGER *ci,
	       MMINTEGER rgb, MMINTEGER count)
{
	MMINTEGER i;

	for (i = 0; i < count; i++)
		cmm2->gfx_pixel((int)(xi ? xi[i] : (MMINTEGER)xf[i]), (int)(yi ? yi[i] : (MMINTEGER)yf[i]),
				(cf || ci) ? (int)((ci ? ci[i] : (MMINTEGER)cf[i]) & MM_COLBITS) : MM_CMM2_COL(rgb));
}

void mm_pixel(MMINTEGER x, MMINTEGER y, MMINTEGER rgb)
{
	cmm2->gfx_pixel((int)x, (int)y, MM_CMM2_COL(rgb));
}

MMINTEGER mm_pixel_get(MMINTEGER x, MMINTEGER y)
{
	uint8_t bgr[3];

	if (cmm2->gfx_read((int)x, (int)y, 1, 1, bgr) < 0)
		return 0;
	return ((MMINTEGER)bgr[2] << 16) | ((MMINTEGER)bgr[1] << 8) | bgr[0];
}

/* A byte as the other packing has it: 4 bits a pixel the nibbles swapped,
   1 bit a pixel the bits reversed (each is its own inverse) */
static unsigned char mm_cmm2_turn(unsigned char b, int bpp)
{
	if (bpp == 4)
		return (unsigned char)((b << 4) | (b >> 4));
	b = (unsigned char)((b & 0xF0) >> 4 | (b & 0x0F) << 4);
	b = (unsigned char)((b & 0xCC) >> 2 | (b & 0x33) << 2);
	return (unsigned char)((b & 0xAA) >> 1 | (b & 0x55) << 1);
}

/* rows of len bytes, stride apart in the page from offset, to or from buf
   (contiguous): 0, or -1 if there is no 4 or 1-bit page or it is outside */
static MMINTEGER mm_cmm2_rows(MMINTEGER offset, MMINTEGER len, MMINTEGER rows,
			      MMINTEGER stride, unsigned char *buf, int put)
{
	cmm2_target t;
	MMINTEGER r, i;

	if (mm_cmm2_target(&t) < 0 || (t.bpp != 4 && t.bpp != 1) || offset < 0 || len < 0 || rows < 0)
		return -1;
	if (rows && offset + (rows - 1) * stride + len > (MMINTEGER)(t.pitch * t.height))
		return -1;
	for (r = 0; r < rows; r++) {
		unsigned char *p = t.fb + offset + r * stride, *q = buf + r * len;

		for (i = 0; i < len; i++) {
			if (put)
				p[i] = mm_cmm2_turn(q[i], (int)t.bpp);
			else
				q[i] = mm_cmm2_turn(p[i], (int)t.bpp);
		}
	}
	return 0;
}

MMINTEGER mm_fb_read(MMINTEGER offset, MMINTEGER len, void *buf)
{
	return mm_cmm2_rows(offset, len, 1, len, buf, 0);
}

MMINTEGER mm_fb_put(MMINTEGER offset, MMINTEGER len, const void *buf)
{
	return mm_cmm2_rows(offset, len, 1, len, (unsigned char *)buf, 1);
}

MMINTEGER mm_fb_readr(MMINTEGER offset, MMINTEGER len, MMINTEGER rows,
		      MMINTEGER stride, void *buf)
{
	return mm_cmm2_rows(offset, len, rows, stride, buf, 0);
}

MMINTEGER mm_fb_putr(MMINTEGER offset, MMINTEGER len, MMINTEGER rows,
		     MMINTEGER stride, const void *buf)
{
	return mm_cmm2_rows(offset, len, rows, stride, (unsigned char *)buf, 1);
}

MMINTEGER mm_fb_scroll2(MMINTEGER dx, MMINTEGER dy, MMINTEGER fill)
{
	(void)dx; (void)dy; (void)fill;
	return -1;
}

/* the index a colour is in the page: RGB121's in 4 bits, set or not in 1 */
MMINTEGER mm_colour_index(MMINTEGER rgb)
{
	cmm2_target t;

	if (mm_cmm2_target(&t) < 0)
		return -1;
	if (t.bpp == 4)
		return ((rgb & 0x800000) >> 20) | ((rgb & 0xC000) >> 13) | ((rgb & 0x80) >> 7);
	if (t.bpp == 1)
		return (rgb & 0xFFFFFF) != 0;
	return -1;
}

#elif MMB_CMM2_PART == 3

/* FRAMEBUFFER's hooks: the firmware's statements (the PicoMite's, in a
   4 or 1-bit mode: Rgb121.c).  N, F and L are 0, 1 and 2 here.  The
   PC3's layer is the LCD PicoMite's, shown only by MERGE (L over F onto
   the screen, a colour transparent): that is the firmware's framebuffer
   2, which its FRAMEBUFFER MERGE c puts over F into N so, where its own
   L is a VGA PicoMite's, laid over the screen every frame */
static const char mm_cmm2_fbl[] = "NF2";

static int mm_fb_open_hw(int claim, int which)
{
	char line[40];

	if (claim)
		snprintf(line, sizeof(line), "FRAMEBUFFER CREATE%s", which == MM_FB_L ? " 2" : "");
	else
		snprintf(line, sizeof(line), "FRAMEBUFFER CLOSE %c", mm_cmm2_fbl[which]);
	return cmm2->basic_cmd(line) < 0 ? -1 : 0;
}

static void mm_fb_sel_hw(int which)
{
	char line[32];

	snprintf(line, sizeof(line), "FRAMEBUFFER WRITE %c", mm_cmm2_fbl[which]);
	mm_cmm2_cmd(line);
}

static void mm_fb_copy_hw(int src, int dst)
{
	char line[32];

	snprintf(line, sizeof(line), "FRAMEBUFFER COPY %c,%c", mm_cmm2_fbl[src], mm_cmm2_fbl[dst]);
	mm_cmm2_cmd(line);
}

static int mm_fb_merge_hw(int colour)
{
	char line[40];

	snprintf(line, sizeof(line), "FRAMEBUFFER MERGE %d", colour);
	return mm_cmm2_cmd(line) < 0 ? -1 : 0;		/* (the firmware's message) */
}

static void mm_fb_wait_hw(void)
{
	mm_cmm2_cmd("FRAMEBUFFER WAIT");
}

/* CREATE hands back blank buffers already (Rgb121.c) */
static void mm_fb_clear_hw(int which) { (void)which; }

/* the page drawn on, flooded: the firmware's CLS */
static void mm_fb_paint_hw(MMINTEGER rgb)
{
	cmm2->gfx_cls((int)(rgb & MM_COLBITS));
}

#endif

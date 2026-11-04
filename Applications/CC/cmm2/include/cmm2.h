#ifndef _CMM2_H
#define _CMM2_H

/*
 * The Colour Maximite 2's services, for a program compiled on it.
 *
 * A loadable program built on a PC calls the firmware through its
 * services table (the Loadable Programs manual, The Services); a
 * compiled program calls the same services by the same names, as
 * functions: cmm2->pin_write(5, 1) there is pin_write(5, 1) here.
 * bcrun binds each by its name (cmm2/bcrun_svc.c).  A program's
 * addresses are the machine's, so its buffers, and the framebuffer
 * video_mode gives it, are reached by ordinary pointers.
 *
 * Prototyped, so that an argument of another type is converted.
 *
 * Not here: audio_start and frame_hook, whose callbacks the firmware
 * runs in an interrupt, and the OPL chip; music_play starts the sound
 * output itself.
 */

#include <stddef.h>

/* ---- colours: MMBasic's, RGB888 with its transparency nibble ---- */

#define RGB(r, g, b)	(0x0F000000L | ((long)(r) << 16) | ((long)(g) << 8) | (long)(b))
#define BLACK		0x00000000L
#define BLUE		RGB(0, 0, 255)
#define GREEN		RGB(0, 255, 0)
#define CYAN		RGB(0, 255, 255)
#define RED		RGB(255, 0, 0)
#define MAGENTA		RGB(255, 0, 192)
#define YELLOW		RGB(255, 255, 0)
#define BROWN		RGB(0xA5, 0x2A, 0x2A)
#define GRAY		RGB(64, 64, 64)
#define LITEGRAY	RGB(128, 128, 128)
#define WHITE		RGB(255, 255, 255)
#define ORANGE		RGB(0xFF, 0xA5, 0)
#define PINK		RGB(0xFF, 0xA0, 0xAB)
#define GOLD		RGB(0xFF, 0xD7, 0)
#define SALMON		RGB(0xFA, 0x80, 0x72)
#define BEIGE		RGB(0xF5, 0xF5, 0xDC)
#define NOFILL		(-1L)		/* a fill or background of none */

/* ---- the console and the clock ---------------------------------- */

void con_putc(int c);
void con_puts(const char *s);
int con_getc(void);			/* the next key, or -1: it never waits */
void con_screen(int on);		/* 0: console output to the serial port only */
unsigned long long micros(void);	/* microseconds since start-up */
void delay_us(unsigned long us);
void datetime(int t[6]);		/* year, month, day, hour, minute, second */
unsigned long cpu_hz(void);
int poll(void);				/* USB seen to; 1 once Ctrl-C has been pressed */
int break_take(void);			/* 1 if Ctrl-C was pressed since the last call */

/* ---- the screen ---------------------------------------------------- */

typedef struct {			/* the display after video_mode: page 0 */
	unsigned char *fb;		/* its first pixel; a line is pitch bytes on */
	unsigned long width, height, pitch, bpp;
	unsigned long copies;		/* 2 in a mode whose lines are shown twice */
} cmm2_video;

int video_mode(int mode, int depth, cmm2_video *v);	/* MODE mode,depth: 0, or negative */
int video_scaled(int mode, int depth, int scale, cmm2_video *v);
int video_palette(int start, int count, const unsigned char *rgb);
void video_wait(void);			/* until the next frame starts */
int dma2d_copy(const void *src, unsigned long src_pitch, void *dst, unsigned long dst_pitch,
	       unsigned long width, unsigned long height);
int dma2d_busy(void);

/* drawing, by MMBasic's routines: colours as RGB(), NOFILL for no fill or background; 0, or -4 */
int gfx_cls(long colour);
int gfx_pixel(int x, int y, long colour);
int gfx_line(int x1, int y1, int x2, int y2, int width, long colour);
int gfx_box(int x, int y, int w, int h, int width, long colour, long fill);
int gfx_rbox(int x, int y, int w, int h, int radius, long colour, long fill);
int gfx_circle(int x, int y, int r, int width, double aspect, long colour, long fill);
int gfx_triangle(int x1, int y1, int x2, int y2, int x3, int y3, long colour, long fill);
int gfx_text(int x, int y, const char *s, const char *just, int font, int scale, long colour, long bg);
int gfx_font(int font, int scale, int *w, int *h);	/* FONT's, and its size; font 0: as it is */
int gfx_colour(long colour, long bg);			/* COLOUR's */
int gfx_read(int x, int y, int w, int h, unsigned char *bgr);	/* 3 bytes a pixel */
int gfx_write(int x, int y, int w, int h, const unsigned char *bgr);
int gfx_save(const char *name);		/* the screen as a PNG, or a BMP if the name ends in .BMP */

/* ---- the keyboard and the mouse ------------------------------------ */

int keys_held(int keys[7]);		/* KEYDOWN's: how many are held */
int keys_raw(unsigned char report[8]);	/* the USB keyboard's last report */
int mouse(int m[4]);			/* x, y, buttons, wheel: 1, or 0 with no mouse */
void mouse_set(int x, int y);

/* ---- music: a MIDI file in memory, on the firmware's OPL3 synth ---- */

int music_play(const void *midi, unsigned long size, int loop, const unsigned char *bank);
void music_stop(void);
void music_volume(int volume);		/* 0-127 */
void music_pause(int paused);
int music_playing(void);
void music_rate(int divisor);		/* 1-4 */

/* ---- files and directories, beyond stdio --------------------------- */

typedef struct {
	unsigned long size;		/* 0 for a directory */
	unsigned char is_dir;
	unsigned char month, day, hour, minute, second;
	unsigned short year;
} cmm2_stat;

typedef struct {
	char name[256];
	unsigned long size;
	unsigned char is_dir;
} cmm2_dirent;

int file_stat(const char *name, cmm2_stat *st);	/* 0, or minus FatFs's error */
int dir_make(const char *name);
int dir_open(const char *path);		/* a handle 0-3 */
int dir_read(int h, cmm2_dirent *e);	/* 1 an entry, 0 the end */
int dir_close(int h);

/* ---- I/O, by the PicoMite's GP numbers ----------------------------- */

#define CMM2_PIN_OFF	0
#define CMM2_PIN_IN	1
#define CMM2_PIN_OUT	2
#define CMM2_PIN_OD	3		/* open drain */
#define CMM2_PIN_ANALOG	4
#define CMM2_PULL_NONE	0
#define CMM2_PULL_UP	1
#define CMM2_PULL_DOWN	2

int pin_setup(int gp, int mode, int pull);
int pin_read(int gp);
void pin_write(int gp, int v);
int adc_read(int gp, unsigned long *microvolts);	/* 0-65535 */
int pwm_set(int gp, unsigned long freq, unsigned long duty);	/* GP2-5, duty 0-65535 */
int pwm_stop(int gp);
int i2c_open(int bus, int khz, int timeout_ms);
int i2c_close(int bus);
int i2c_xfer(int bus, int addr, const unsigned char *w, unsigned long wn, unsigned char *r,
	     unsigned long rn, int stop);
int spi_open(int bus, unsigned long hz, int mode);
int spi_close(int bus);
int spi_xfer(int bus, const unsigned char *tx, unsigned char *rx, unsigned long n);
int com_open(int com, unsigned long baud, int bits, int parity, int stop, unsigned long rxbuf);
int com_close(int com);
int com_write(int com, const unsigned char *buf, unsigned long n);
int com_read(int com, unsigned char *buf, unsigned long n);	/* the bytes waiting, up to n */
int com_avail(int com);
const char *io_error(void);		/* the firmware's message for the last -4 */

#endif

/*
 *	The CMM2's services as bcrun's library calls (CMM2 only).
 *
 *	Included at the end of bcrun.c, as bcrun_mm.c is on the PC3: the
 *	wrappers need arg(), argd() and A.  A compiled program calls the
 *	firmware's services (Inc/cmm2img.h in the CMM2's repository, the
 *	Loadable Programs manual's chapter The Services) by their names,
 *	declared in cmm2/include/cmm2.h: cmm2->pin_write(5, 1) in a loadable
 *	program is pin_write(5, 1) in a compiled one.  A program address is
 *	a machine address, so buffers and structures go straight through;
 *	the argument slots are the prototypes' (a double takes two).
 */
#include "cmm2.h"			/* tools/sdk's: the services table */

#define SI(n)	((int)arg(n))
#define SU(n)	((uint32_t)arg(n))
#define SP(n)	((void *)vptr((unsigned long)arg(n)))

/* music_play needs the sound output running, which a program starts
   with a fill of its own; a compiled one has none, so this silence */
static int sv_audio;

static void sv_silence(int16_t *buf, uint32_t frames)
{
	memset(buf, 0, frames * 4);
}

static void sv_con_putc(void) { cmm2->con_putc(SI(0)); A = 0; }
static void sv_con_puts(void) { cmm2->con_puts(SP(0)); A = 0; }
static void sv_con_getc(void) { A = cmm2->con_getc(); }
static void sv_con_screen(void) { cmm2->con_screen(SI(0)); A = 0; }
static void sv_micros(void) { A = (int64_t)cmm2->micros(); }
static void sv_delay_us(void) { cmm2->delay_us(SU(0)); A = 0; }
static void sv_datetime(void) { cmm2->datetime(SP(0)); A = 0; }
static void sv_cpu_hz(void) { A = cmm2->cpu_hz(); }
static void sv_poll(void) { A = cmm2->poll(); }
static void sv_break_take(void) { A = cmm2->break_take(); }

static void sv_video_mode(void) { A = cmm2->video_mode(SI(0), SI(1), SP(2)); }
static void sv_video_scaled(void) { A = cmm2->video_scaled(SI(0), SI(1), SI(2), SP(3)); }
static void sv_video_palette(void) { A = cmm2->video_palette(SI(0), SI(1), SP(2)); }
static void sv_video_wait(void) { cmm2->video_wait(); A = 0; }
static void sv_dma2d_copy(void)
{
	A = cmm2->dma2d_copy(SP(0), SU(1), SP(2), SU(3), SU(4), SU(5));
}
static void sv_dma2d_busy(void) { A = cmm2->dma2d_busy(); }

static void sv_gfx_cls(void) { A = cmm2->gfx_cls(SI(0)); }
static void sv_gfx_pixel(void) { A = cmm2->gfx_pixel(SI(0), SI(1), SI(2)); }
static void sv_gfx_line(void)
{
	A = cmm2->gfx_line(SI(0), SI(1), SI(2), SI(3), SI(4), SI(5));
}
static void sv_gfx_box(void)
{
	A = cmm2->gfx_box(SI(0), SI(1), SI(2), SI(3), SI(4), SI(5), SI(6));
}
static void sv_gfx_rbox(void)
{
	A = cmm2->gfx_rbox(SI(0), SI(1), SI(2), SI(3), SI(4), SI(5), SI(6));
}
static void sv_gfx_circle(void)
{
	A = cmm2->gfx_circle(SI(0), SI(1), SI(2), SI(3), (float)argd(4),
			     SI(6), SI(7));
}
static void sv_gfx_triangle(void)
{
	A = cmm2->gfx_triangle(SI(0), SI(1), SI(2), SI(3), SI(4), SI(5),
			       SI(6), SI(7));
}
static void sv_gfx_text(void)
{
	A = cmm2->gfx_text(SI(0), SI(1), SP(2), SP(3), SI(4), SI(5), SI(6),
			   SI(7));
}
static void sv_gfx_font(void) { A = cmm2->gfx_font(SI(0), SI(1), SP(2), SP(3)); }
static void sv_gfx_colour(void) { A = cmm2->gfx_colour(SI(0), SI(1)); }
static void sv_gfx_read(void) { A = cmm2->gfx_read(SI(0), SI(1), SI(2), SI(3), SP(4)); }
static void sv_gfx_write(void) { A = cmm2->gfx_write(SI(0), SI(1), SI(2), SI(3), SP(4)); }
static void sv_gfx_save(void) { A = cmm2->gfx_save(SP(0)); }

static void sv_keys_held(void) { A = cmm2->keys_held(SP(0)); }
static void sv_keys_raw(void) { A = cmm2->keys_raw(SP(0)); }
static void sv_mouse(void) { A = cmm2->mouse(SP(0)); }
static void sv_mouse_set(void) { cmm2->mouse_set(SI(0), SI(1)); A = 0; }

static void sv_music_play(void)
{
	if (!sv_audio && cmm2->audio_start(sv_silence) > 0)
		sv_audio = 1;
	A = cmm2->music_play(SP(0), SU(1), SI(2), SP(3));
}
static void sv_music_stop(void) { cmm2->music_stop(); A = 0; }
static void sv_music_volume(void) { cmm2->music_volume(SI(0)); A = 0; }
static void sv_music_pause(void) { cmm2->music_pause(SI(0)); A = 0; }
static void sv_music_playing(void) { A = cmm2->music_playing(); }
static void sv_music_rate(void) { cmm2->music_rate(SI(0)); A = 0; }

static void sv_file_stat(void) { A = cmm2->file_stat(SP(0), SP(1)); }
static void sv_dir_make(void) { A = cmm2->dir_make(SP(0)); }
static void sv_dir_open(void) { A = cmm2->dir_open(SP(0)); }
static void sv_dir_read(void) { A = cmm2->dir_read(SI(0), SP(1)); }
static void sv_dir_close(void) { A = cmm2->dir_close(SI(0)); }

static void sv_pin_setup(void) { A = cmm2->pin_setup(SI(0), SI(1), SI(2)); }
static void sv_pin_read(void) { A = cmm2->pin_read(SI(0)); }
static void sv_pin_write(void) { cmm2->pin_write(SI(0), SI(1)); A = 0; }
static void sv_adc_read(void) { A = cmm2->adc_read(SI(0), SP(1)); }
static void sv_pwm_set(void) { A = cmm2->pwm_set(SI(0), SU(1), SU(2)); }
static void sv_pwm_stop(void) { A = cmm2->pwm_stop(SI(0)); }
static void sv_i2c_open(void) { A = cmm2->i2c_open(SI(0), SI(1), SI(2)); }
static void sv_i2c_close(void) { A = cmm2->i2c_close(SI(0)); }
static void sv_i2c_xfer(void)
{
	A = cmm2->i2c_xfer(SI(0), SI(1), SP(2), SU(3), SP(4), SU(5), SI(6));
}
static void sv_spi_open(void) { A = cmm2->spi_open(SI(0), SU(1), SI(2)); }
static void sv_spi_close(void) { A = cmm2->spi_close(SI(0)); }
static void sv_spi_xfer(void) { A = cmm2->spi_xfer(SI(0), SP(1), SP(2), SU(3)); }
static void sv_com_open(void)
{
	A = cmm2->com_open(SI(0), SU(1), SI(2), SI(3), SI(4), SU(5));
}
static void sv_com_close(void) { A = cmm2->com_close(SI(0)); }
static void sv_com_write(void) { A = cmm2->com_write(SI(0), SP(1), SU(2)); }
static void sv_com_read(void) { A = cmm2->com_read(SI(0), SP(1), SU(2)); }
static void sv_com_avail(void) { A = cmm2->com_avail(SI(0)); }
static void sv_io_error(void) { A = (long)(uintptr_t)cmm2->io_error(); }

static const struct {
	const char *name;
	void (*fn)(void);
	unsigned version;		/* the services' version it needs */
} svtab[] = {
	{ "con_putc", sv_con_putc, 1 }, { "con_puts", sv_con_puts, 1 },
	{ "con_getc", sv_con_getc, 1 }, { "con_screen", sv_con_screen, 4 },
	{ "micros", sv_micros, 1 }, { "delay_us", sv_delay_us, 1 },
	{ "datetime", sv_datetime, 1 }, { "cpu_hz", sv_cpu_hz, 1 },
	{ "poll", sv_poll, 1 }, { "break_take", sv_break_take, 5 },
	{ "video_mode", sv_video_mode, 2 }, { "video_scaled", sv_video_scaled, 6 },
	{ "video_palette", sv_video_palette, 2 }, { "video_wait", sv_video_wait, 2 },
	{ "dma2d_copy", sv_dma2d_copy, 6 }, { "dma2d_busy", sv_dma2d_busy, 6 },
	{ "gfx_cls", sv_gfx_cls, 10 }, { "gfx_pixel", sv_gfx_pixel, 10 },
	{ "gfx_line", sv_gfx_line, 10 }, { "gfx_box", sv_gfx_box, 10 },
	{ "gfx_rbox", sv_gfx_rbox, 10 }, { "gfx_circle", sv_gfx_circle, 10 },
	{ "gfx_triangle", sv_gfx_triangle, 10 }, { "gfx_text", sv_gfx_text, 10 },
	{ "gfx_font", sv_gfx_font, 10 }, { "gfx_colour", sv_gfx_colour, 10 },
	{ "gfx_read", sv_gfx_read, 10 }, { "gfx_write", sv_gfx_write, 10 },
	{ "gfx_save", sv_gfx_save, 10 },
	{ "keys_held", sv_keys_held, 2 }, { "keys_raw", sv_keys_raw, 9 },
	{ "mouse", sv_mouse, 4 }, { "mouse_set", sv_mouse_set, 4 },
	{ "music_play", sv_music_play, 3 }, { "music_stop", sv_music_stop, 3 },
	{ "music_volume", sv_music_volume, 3 }, { "music_pause", sv_music_pause, 3 },
	{ "music_playing", sv_music_playing, 3 }, { "music_rate", sv_music_rate, 3 },
	{ "file_stat", sv_file_stat, 5 }, { "dir_make", sv_dir_make, 1 },
	{ "dir_open", sv_dir_open, 1 }, { "dir_read", sv_dir_read, 1 },
	{ "dir_close", sv_dir_close, 1 },
	{ "pin_setup", sv_pin_setup, 7 }, { "pin_read", sv_pin_read, 7 },
	{ "pin_write", sv_pin_write, 7 }, { "adc_read", sv_adc_read, 7 },
	{ "pwm_set", sv_pwm_set, 7 }, { "pwm_stop", sv_pwm_stop, 7 },
	{ "i2c_open", sv_i2c_open, 7 }, { "i2c_close", sv_i2c_close, 7 },
	{ "i2c_xfer", sv_i2c_xfer, 7 }, { "spi_open", sv_spi_open, 7 },
	{ "spi_close", sv_spi_close, 7 }, { "spi_xfer", sv_spi_xfer, 7 },
	{ "com_open", sv_com_open, 7 }, { "com_close", sv_com_close, 7 },
	{ "com_write", sv_com_write, 7 }, { "com_read", sv_com_read, 7 },
	{ "com_avail", sv_com_avail, 7 }, { "io_error", sv_io_error, 7 },
	{ NULL, NULL, 0 }
};

/*
 *	The wrapper for a service's name, or NULL.  One the firmware is too
 *	old for is not there either, so the program is refused at load,
 *	by the name of the service, as for any function bcrun lacks.
 */
static void (*cmm2_svc_lookup(const char *name))(void)
{
	unsigned i;

	for (i = 0; svtab[i].name; i++)
		if (strcmp(svtab[i].name, name) == 0)
			return cmm2->version >= svtab[i].version ? svtab[i].fn
								 : NULL;
	return NULL;
}

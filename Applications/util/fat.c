/* fat - read files from a FAT partition into Fuzix.
 *
 * The Pico Computer 3 SD card keeps a FAT partition (/dev/hda1) that
 * Windows/macOS/Linux mount directly; this reads it from the Fuzix
 * side so the card itself carries files between the two worlds.
 *
 *	fat ls [path]		list a directory (default: root)
 *	fat ls dir/4*		... or what matches a pattern in it
 *	fat get path [dest]	copy a FAT file into Fuzix
 *	fat info		filesystem details
 *	fat -d /dev/xxx ...	use another device
 *
 * FAT16 and FAT32, long file names, read only.  Devices up to 2GB
 * (off_t); the PC3 partition is 64MB.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>

#define SECSZ	512
#define LFNMAX	64

/* The SD card is hda.  It was hdb while the on-board flash filesystem
   existed and took hda; that device is gone (config.h), the card moved
   up, and this default did not - so "fat ls" answered "cannot open
   /dev/hdb1" on every machine until -d was given by hand. */
static const char *devname = "/dev/hda1";
static int dev;

static uint8_t sec[SECSZ];		/* directory/data sector */
static uint8_t fatsec[SECSZ];		/* FAT sector cache */
static long fatsec_lba = -1;

static int fat32;
static uint8_t spc;			/* sectors per cluster */
static uint32_t fat_lba;		/* first FAT */
static uint32_t root_lba;		/* FAT16 root directory */
static uint32_t root_secs;		/* FAT16 root size in sectors */
static uint32_t root_clus;		/* FAT32 root cluster */
static uint32_t data_lba;
static uint32_t nclus;

static uint16_t rd16(const uint8_t *p)
{
	return p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t *p)
{
	return rd16(p) | ((uint32_t)rd16(p + 2) << 16);
}

static void fail(const char *msg)
{
	fprintf(stderr, "fat: %s\n", msg);
	exit(1);
}

static void read_sec(uint32_t lba, uint8_t *buf)
{
	if (lseek(dev, (off_t)lba * SECSZ, 0) < 0 ||
	    read(dev, buf, SECSZ) != SECSZ)
		fail("device read error");
}

static void fat_mount(void)
{
	uint32_t rsvd, nfats, rootent, totsec, fatsz;

	dev = open(devname, O_RDONLY);
	if (dev < 0) {
		fprintf(stderr, "fat: cannot open %s\n", devname);
		exit(1);
	}
	read_sec(0, sec);
	if (rd16(sec + 11) != SECSZ)
		fail("not a FAT filesystem (or sector size not 512)");
	spc = sec[13];
	rsvd = rd16(sec + 14);
	nfats = sec[16];
	rootent = rd16(sec + 17);
	totsec = rd16(sec + 19);
	if (totsec == 0)
		totsec = rd32(sec + 32);
	fatsz = rd16(sec + 22);
	if (fatsz == 0)
		fatsz = rd32(sec + 36);
	if (spc == 0 || nfats == 0 || fatsz == 0 || totsec == 0)
		fail("not a FAT filesystem");

	fat_lba = rsvd;
	root_lba = rsvd + nfats * fatsz;
	root_secs = (rootent * 32 + SECSZ - 1) / SECSZ;
	data_lba = root_lba + root_secs;
	nclus = (totsec - data_lba) / spc;

	if (nclus < 4085)
		fail("FAT12 not supported: reformat as FAT16 or FAT32");
	fat32 = (nclus >= 65525);
	root_clus = fat32 ? rd32(sec + 44) : 0;
}

static uint32_t clus_lba(uint32_t cl)
{
	return data_lba + (cl - 2) * spc;
}

#define EOC 0x0FFFFFF8UL

static uint32_t fat_next(uint32_t cl)
{
	uint32_t off = fat32 ? cl * 4 : cl * 2;
	long lba = fat_lba + off / SECSZ;

	if (lba != fatsec_lba) {
		read_sec(lba, fatsec);
		fatsec_lba = lba;
	}
	if (fat32)
		return rd32(fatsec + (off % SECSZ)) & 0x0FFFFFFF;
	cl = rd16(fatsec + (off % SECSZ));
	return (cl >= 0xFFF8) ? EOC : cl;
}

/* --- directory iteration ------------------------------------------------- */

struct dirit {
	uint32_t clus;		/* current cluster (0 = FAT16 root) */
	uint32_t lba;		/* current sector */
	uint32_t limit;		/* sectors left in this run */
	int off;		/* byte offset of next entry */
};

static char lfn[LFNMAX + 1];
static char sname[13];

static void dit_open(struct dirit *it, uint32_t cl)
{
	it->clus = cl;
	if (cl == 0) {		/* FAT16 fixed root */
		it->lba = root_lba;
		it->limit = root_secs;
	} else {
		it->lba = clus_lba(cl);
		it->limit = spc;
	}
	it->off = SECSZ;	/* force first load */
	lfn[0] = 0;
}

/* Collect one long-name fragment (13 UCS-2 chars at fixed offsets). */
static void lfn_frag(const uint8_t *e)
{
	static const uint8_t at[13] = { 1,3,5,7,9,14,16,18,20,22,24,28,30 };
	int base = ((e[0] & 0x1F) - 1) * 13;
	int i;

	if (e[0] & 0x40)	/* first (=highest) fragment: start clean */
		memset(lfn, 0, sizeof(lfn));
	for (i = 0; i < 13; i++) {
		int pos = base + i;
		uint16_t u = rd16(e + at[i]);
		if (pos >= LFNMAX)
			continue;
		if (u == 0 || u == 0xFFFF)
			u = 0;
		else if (u > 0x7E || u < 0x20)
			u = '_';
		lfn[pos] = u;
	}
	lfn[LFNMAX] = 0;
}

/* Build "NAME.EXT" from an 8.3 entry. */
static void short_name(const uint8_t *e)
{
	int i, n = 0;

	for (i = 0; i < 8 && e[i] != ' '; i++)
		sname[n++] = e[i];
	if (e[8] != ' ') {
		sname[n++] = '.';
		for (i = 8; i < 11 && e[i] != ' '; i++)
			sname[n++] = e[i];
	}
	sname[n] = 0;
	if (sname[0] == 0x05)
		sname[0] = (char)0xE5;
}

/* Next real entry, or NULL at end of directory.  On return sname holds
 * the 8.3 name and lfn the long name ("" if none). */
static uint8_t *dit_next(struct dirit *it)
{
	for (;;) {
		uint8_t *e;

		if (it->off >= SECSZ) {
			if (it->limit == 0) {
				if (it->clus == 0)
					return NULL;
				it->clus = fat_next(it->clus);
				if (it->clus >= EOC || it->clus < 2)
					return NULL;
				it->lba = clus_lba(it->clus);
				it->limit = spc;
			}
			read_sec(it->lba++, sec);
			it->limit--;
			it->off = 0;
		}
		e = sec + it->off;
		it->off += 32;

		if (e[0] == 0x00)
			return NULL;		/* end of directory */
		if (e[0] == 0xE5) {
			lfn[0] = 0;		/* deleted */
			continue;
		}
		if ((e[11] & 0x3F) == 0x0F) {	/* long name fragment */
			lfn_frag(e);
			continue;
		}
		if (e[11] & 0x08) {
			lfn[0] = 0;		/* volume label */
			continue;
		}
		short_name(e);
		return e;
	}
}

/* --- lookup -------------------------------------------------------------- */

static uint32_t entry_clus(const uint8_t *e)
{
	uint32_t cl = rd16(e + 26);
	if (fat32)
		cl |= (uint32_t)rd16(e + 20) << 16;
	return cl;
}

/* Walk path from the root; returns the raw entry (in sec[]) or NULL.
 * A NULL path or "" means the root itself (returned as entry NULL with
 * *dirclus set). */
static uint8_t *lookup(const char *path, uint32_t *dirclus)
{
	static char comp[LFNMAX + 1];
	uint32_t cl = fat32 ? root_clus : 0;
	uint8_t *e = NULL;

	*dirclus = cl;
	while (path && *path) {
		const char *s;
		struct dirit it;
		int n = 0;

		while (*path == '/')
			path++;
		if (*path == 0)
			break;
		s = path;
		while (*s && *s != '/') {
			if (n <= LFNMAX)
				comp[n++] = *s;
			s++;
		}
		comp[n] = 0;
		path = s;

		dit_open(&it, cl);
		for (;;) {
			e = dit_next(&it);
			if (e == NULL)
				return NULL;
			if (!strcasecmp(comp, sname) ||
			    (lfn[0] && !strcasecmp(comp, lfn)))
				break;
			lfn[0] = 0;
		}
		if (*path) {		/* must descend */
			if (!(e[11] & 0x10))
				return NULL;
			cl = entry_clus(e);
			*dirclus = cl;
			e = NULL;
		}
	}
	return e;
}

/* --- commands ------------------------------------------------------------ */

/* '*' and '?', case-insensitively: FAT names are not case sensitive,
   and neither is the DOS convention these patterns come from.
   Backtracking on the last '*' rather than recursing, so a pattern like
   "a*b*c*d*" cannot cost exponential time on a long name. */
static int lc(int c)
{
	return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

static int globmatch(const char *pat, const char *s)
{
	const char *star = NULL, *ss = s;

	while (*s) {
		if (*pat == '?' || (*pat && lc(*pat) == lc(*s))) {
			pat++;
			s++;
		} else if (*pat == '*') {
			star = pat++;
			ss = s;
		} else if (star) {
			pat = star + 1;
			s = ++ss;
		} else {
			return 0;
		}
	}
	while (*pat == '*')
		pat++;
	return *pat == 0;
}

static int haswild(const char *s)
{
	for (; *s; s++)
		if (*s == '*' || *s == '?')
			return 1;
	return 0;
}

static int do_ls(const char *path)
{
	static char dirpart[LFNMAX + 1];
	struct dirit it;
	uint32_t dirclus;
	uint8_t *e;
	const char *pat = NULL;
	const char *name;
	int found = 0;

	while (path && *path == '/')
		path++;
	if (path && *path == 0)
		path = NULL;

	/*
	 * A wildcard in the LAST component makes this a search rather than
	 * a path: "physics/4*" lists what matches 4* inside physics, and
	 * "4*" does the same in the root.  Only the last component - a
	 * pattern in a directory name would mean walking several
	 * directories, which ls does not do anywhere else either.
	 */
	if (path && haswild(path)) {
		const char *slash = strrchr(path, '/');

		if (slash == NULL) {
			pat = path;
			path = NULL;
		} else {
			size_t n = (size_t)(slash - path);

			if (n > LFNMAX) {
				fprintf(stderr, "fat: %s: path too long\n",
					path);
				return 1;
			}
			memcpy(dirpart, path, n);
			dirpart[n] = 0;
			pat = slash + 1;
			path = dirpart;
			if (haswild(path)) {
				fprintf(stderr, "fat: %s: a wildcard is only "
					"allowed in the last part\n", pat - 1);
				return 1;
			}
		}
	}

	e = lookup(path, &dirclus);
	if (path && e == NULL) {
		fprintf(stderr, "fat: %s: not found\n", path);
		return 1;
	}
	if (e) {
		if (!(e[11] & 0x10)) {	/* a plain file: list it alone */
			if (pat) {
				fprintf(stderr, "fat: %s: not a directory\n",
					path);
				return 1;
			}
			printf("%8lu  %s\n", (unsigned long)rd32(e + 28),
			       lfn[0] ? lfn : sname);
			return 0;
		}
		dirclus = entry_clus(e);
	}
	dit_open(&it, dirclus);
	while ((e = dit_next(&it)) != NULL) {
		name = lfn[0] ? lfn : sname;
		if (pat == NULL || globmatch(pat, name)) {
			found++;
			if (e[11] & 0x10)
				printf("   <dir>  %s\n", name);
			else
				printf("%8lu  %s\n",
				       (unsigned long)rd32(e + 28), name);
		}
		lfn[0] = 0;
	}
	if (pat && !found) {
		fprintf(stderr, "fat: %s: no match\n", pat);
		return 1;
	}
	return 0;
}

static int do_get(const char *path, const char *dest)
{
	static char dbuf[LFNMAX + 1];
	uint32_t dirclus, cl, size;
	uint8_t *e;
	int fd;

	e = lookup(path, &dirclus);
	if (e == NULL) {
		fprintf(stderr, "fat: %s: not found\n", path);
		return 1;
	}
	if (e[11] & 0x10) {
		fprintf(stderr, "fat: %s: is a directory\n", path);
		return 1;
	}
	cl = entry_clus(e);
	size = rd32(e + 28);

	if (dest == NULL) {
		/* default to the name we matched, lowercased */
		const char *n = lfn[0] ? lfn : sname;
		int i;
		for (i = 0; n[i] && i < LFNMAX; i++)
			dbuf[i] = tolower((unsigned char)n[i]);
		dbuf[i] = 0;
		dest = dbuf;
	}

	fd = open(dest, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) {
		fprintf(stderr, "fat: cannot create %s\n", dest);
		return 1;
	}
	while (size && cl >= 2 && cl < EOC) {
		uint32_t lba = clus_lba(cl);
		int s;
		for (s = 0; s < spc && size; s++) {
			int n = (size < SECSZ) ? (int)size : SECSZ;
			read_sec(lba + s, sec);
			if (write(fd, sec, n) != n) {
				fprintf(stderr, "fat: write error on %s\n",
					dest);
				close(fd);
				return 1;
			}
			size -= n;
		}
		cl = fat_next(cl);
	}
	close(fd);
	if (size) {
		fprintf(stderr, "fat: %s: cluster chain short\n", path);
		return 1;
	}
	return 0;
}

static int do_info(void)
{
	printf("%s: FAT%d, %lu clusters of %u bytes (%lu MB)\n",
	       devname, fat32 ? 32 : 16, (unsigned long)nclus,
	       (unsigned)spc * SECSZ,
	       (unsigned long)(((nclus / 1024) * spc) / 2));
	return 0;
}

static void usage(void)
{
	fprintf(stderr,
	    "usage: fat [-d device] ls [path]      path may end in a\n"
	    "                                      pattern: dir/4*\n"
	    "       fat [-d device] get path [dest]\n"
	    "       fat [-d device] info\n");
	exit(1);
}

int main(int argc, char *argv[])
{
	int a = 1;

	if (a < argc && !strcmp(argv[a], "-d")) {
		if (a + 1 >= argc)
			usage();
		devname = argv[a + 1];
		a += 2;
	}
	if (a >= argc)
		usage();
	fat_mount();
	if (!strcmp(argv[a], "ls"))
		return do_ls(argv[a + 1]);
	if (!strcmp(argv[a], "get")) {
		if (a + 1 >= argc)
			usage();
		return do_get(argv[a + 1], argv[a + 2]);
	}
	if (!strcmp(argv[a], "info"))
		return do_info();
	usage();
	return 1;
}

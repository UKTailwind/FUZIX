/* fat - move files between a FAT partition and Fuzix.
 *
 * The Pico Computer 3 SD card keeps a FAT partition (/dev/hda1) that
 * Windows/macOS/Linux mount directly; this reads and writes it from the
 * Fuzix side so the card itself carries files between the two worlds.
 *
 *	fat ls [path]		list a directory (default: root)
 *	fat ls dir/4*		... or what matches a pattern in it
 *	fat get path [dest]	copy a FAT file into Fuzix
 *	fat put src [dest]	copy a Fuzix file into FAT
 *	fat info		filesystem details
 *	fat -d /dev/xxx ...	use another device
 *
 * FAT16 and FAT32, long file names.  Devices up to 2GB (off_t); the
 * PC3 partition is 128MB (mkcard.sh, FAT_MB).
 *
 * PUT AND POWER LOSS.  The card is meant to be pulled out and carried
 * to another machine, so a half-finished write must never leave it
 * looking like something it is not.  The order below is the whole
 * defence: the data and its cluster chain reach the card before the
 * directory entry that names them, and an overwritten file's old chain
 * is freed only after the entry points at the new one.  Lose power at
 * any point and the directory still describes a file that is entirely
 * there - either the old one or the new one.  What can be left behind
 * is clusters marked in use that no file claims: wasted space, which
 * any desktop chkdsk reclaims, and which no reader can mistake for
 * data.  Every FAT copy is written, because a chkdsk that finds the
 * copies disagreeing calls the card damaged.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

#define SECSZ	512
#define LFNMAX	64
/* Long name fragments hold 13 characters each. */
#define NLFN	((LFNMAX + 12) / 13)

static const char *devname = "/dev/hda1";
static int dev;

static uint8_t sec[SECSZ];		/* directory/data sector */
static uint8_t fatsec[SECSZ];		/* FAT sector cache */
static long fatsec_lba = -1;
static int fatsec_dirty;

static int fat32;
static uint8_t spc;			/* sectors per cluster */
static uint8_t nfats;			/* how many copies of the FAT */
static uint32_t fatsz;			/* sectors in one copy */
static uint32_t fat_lba;		/* first FAT */
static uint32_t root_lba;		/* FAT16 root directory */
static uint32_t root_secs;		/* FAT16 root size in sectors */
static uint32_t root_clus;		/* FAT32 root cluster */
static uint32_t data_lba;
static uint32_t nclus;
static uint32_t fsinfo_lba;		/* FAT32 only, 0 if none */

static uint16_t rd16(const uint8_t *p)
{
	return p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t *p)
{
	return rd16(p) | ((uint32_t)rd16(p + 2) << 16);
}

static void wr16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static void wr32(uint8_t *p, uint32_t v)
{
	wr16(p, (uint16_t)v);
	wr16(p + 2, (uint16_t)(v >> 16));
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

static void write_sec(uint32_t lba, uint8_t *buf)
{
	if (lseek(dev, (off_t)lba * SECSZ, 0) < 0 ||
	    write(dev, buf, SECSZ) != SECSZ)
		fail("device write error");
}

static void fat_mount(int rw)
{
	uint32_t rsvd, rootent, totsec, fatsz16;

	dev = open(devname, rw ? O_RDWR : O_RDONLY);
	if (dev < 0) {
		fprintf(stderr, "fat: cannot open %s%s\n", devname,
			rw ? " for writing" : "");
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
	fatsz16 = rd16(sec + 22);
	fatsz = fatsz16;
	if (fatsz == 0)
		fatsz = rd32(sec + 36);
	if (spc == 0 || nfats == 0 || fatsz == 0 || totsec == 0)
		fail("not a FAT filesystem");

	fat_lba = rsvd;
	root_lba = rsvd + nfats * fatsz;
	root_secs = (rootent * 32 + SECSZ - 1) / SECSZ;
	data_lba = root_lba + root_secs;
	nclus = (totsec - data_lba) / spc;

	/* The format defines the FAT width by the cluster count, and while
	   reading that only had to be right often enough to find the data,
	   writing has to be right always: guess FAT16 on a 32-bit FAT and
	   every entry written is half an entry - corruption, not a
	   refusal.  A FAT32 BPB has no 16-bit FAT size and no fixed root
	   directory, so where the count and the BPB disagree (a formatter
	   that made fewer clusters than the threshold) believe the BPB. */
	fat32 = (nclus >= 65525) || (rootent == 0 && fatsz16 == 0);
	if (!fat32 && nclus < 4085)
		fail("FAT12 not supported: reformat as FAT16 or FAT32");
	root_clus = fat32 ? rd32(sec + 44) : 0;
	fsinfo_lba = 0;
	if (fat32) {
		uint32_t f = rd16(sec + 48);
		if (f != 0 && f != 0xFFFF)
			fsinfo_lba = f;
	}
}

static uint32_t clus_lba(uint32_t cl)
{
	return data_lba + (cl - 2) * spc;
}

#define EOC 0x0FFFFFF8UL
/* What we write to end a chain.  Truncated to 0xFFFF on FAT16, which is
   that format's end marker, so one constant serves both. */
#define EOCW 0x0FFFFFFFUL

/* The cached FAT sector is written back lazily: a file of any size
   touches the same sector over and over (128 or 256 clusters live in
   each), and writing on every link would multiply the card traffic by
   about that factor. */
static void fat_flush(void)
{
	uint8_t i;

	if (!fatsec_dirty)
		return;
	for (i = 0; i < nfats; i++)
		write_sec((uint32_t)fatsec_lba + (uint32_t)i * fatsz, fatsec);
	fatsec_dirty = 0;
}

static void fat_load(uint32_t cl, uint32_t *off)
{
	long lba;

	*off = fat32 ? cl * 4 : cl * 2;
	lba = fat_lba + *off / SECSZ;
	if (lba != fatsec_lba) {
		fat_flush();
		read_sec(lba, fatsec);
		fatsec_lba = lba;
	}
	*off %= SECSZ;
}

/* The raw FAT entry, with FAT32's four reserved top bits masked off. */
static uint32_t fat_get(uint32_t cl)
{
	uint32_t off;

	fat_load(cl, &off);
	return fat32 ? (rd32(fatsec + off) & 0x0FFFFFFFUL) : rd16(fatsec + off);
}

static uint32_t fat_next(uint32_t cl)
{
	uint32_t v = fat_get(cl);

	if (!fat32 && v >= 0xFFF8)
		return EOC;
	return v;
}

static void fat_set(uint32_t cl, uint32_t val)
{
	uint32_t off;

	fat_load(cl, &off);
	if (fat32)
		/* the top four bits belong to the filesystem, not to us */
		wr32(fatsec + off, (rd32(fatsec + off) & 0xF0000000UL) |
				   (val & 0x0FFFFFFFUL));
	else
		wr16(fatsec + off, (uint16_t)val);
	fatsec_dirty = 1;
}

/* Where the last search for a free cluster stopped.  Restarting the
   scan at cluster 2 for every cluster of a file would make writing cost
   time proportional to the square of the size on a nearly full card. */
static uint32_t next_free = 2;
static uint32_t n_alloc, n_freed;	/* clusters that changed hands */

static uint32_t fat_alloc(void)
{
	uint32_t last = nclus + 1;	/* highest cluster number there is */
	uint32_t n, cl = next_free;

	for (n = 0; n < nclus; n++) {
		if (cl > last)
			cl = 2;
		if (fat_get(cl) == 0) {
			next_free = cl + 1;
			n_alloc++;
			return cl;
		}
		cl++;
	}
	return 0;			/* full */
}

static void free_chain(uint32_t cl)
{
	while (cl >= 2 && cl < EOC) {
		uint32_t nx = fat_next(cl);

		fat_set(cl, 0);
		n_freed++;
		cl = nx;
	}
}

/* FAT32 caches the free cluster count.  We know exactly how many
   clusters we took and gave back, so the count stays true without the
   full scan of the FAT that recomputing it would cost - and a desktop
   chkdsk does not report the card as needing repair. */
static void fsinfo_update(void)
{
	uint32_t free_cnt;

	if (!fat32 || fsinfo_lba == 0)
		return;
	read_sec(fsinfo_lba, sec);
	if (rd32(sec) != 0x41615252UL || rd32(sec + 484) != 0x61417272UL)
		return;
	free_cnt = rd32(sec + 488);
	if (free_cnt != 0xFFFFFFFFUL) {
		if (free_cnt >= n_alloc && free_cnt - n_alloc + n_freed <= nclus)
			free_cnt = free_cnt - n_alloc + n_freed;
		else
			free_cnt = 0xFFFFFFFFUL;   /* it was already wrong */
		wr32(sec + 488, free_cnt);
	}
	wr32(sec + 492, (next_free >= 2 && next_free <= nclus + 1) ?
			next_free : 0xFFFFFFFFUL);
	write_sec(fsinfo_lba, sec);
}

/* --- directory iteration ------------------------------------------------- */

struct dirit {
	uint32_t clus;		/* current cluster (0 = FAT16 root) */
	uint32_t lba;		/* current sector */
	uint32_t limit;		/* sectors left in this run */
	int off;		/* byte offset of next entry */
	uint32_t at_lba;	/* sector holding the entry just returned */
	int at_off;		/* and its offset within it */
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
static const uint8_t lfn_at[13] = { 1,3,5,7,9,14,16,18,20,22,24,28,30 };
static uint8_t lfn_ck;			/* the checksum the fragments claim */

/* The checksum that ties long name fragments to their 8.3 entry.  Get it
   wrong when writing and a desktop shows the short name, or chkdsk
   deletes the fragments as orphans; ignore it when reading and a stale
   fragment left behind by another tool puts someone else's name on this
   file. */
static uint8_t lfn_sum(const uint8_t *sn)
{
	uint8_t s = 0;
	int i;

	for (i = 0; i < 11; i++)
		s = (uint8_t)(((s & 1) << 7) + (s >> 1) + sn[i]);
	return s;
}

static void lfn_frag(const uint8_t *e)
{
	int base = ((e[0] & 0x1F) - 1) * 13;
	int i;

	if (e[0] & 0x40)	/* first (=highest) fragment: start clean */
		memset(lfn, 0, sizeof(lfn));
	lfn_ck = e[13];
	for (i = 0; i < 13; i++) {
		int pos = base + i;
		uint16_t u = rd16(e + lfn_at[i]);
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

/* Build "NAME.EXT" from an 8.3 entry.  Byte 12 carries the flags NT
   added for names that are simply lower case: honouring them is what
   makes "readme.txt" show here as it shows on the machine that wrote
   it, instead of shouting. */
static void short_name(const uint8_t *e)
{
	int i, n = 0;
	int lo = (e[12] & 0x08) != 0, elo = (e[12] & 0x10) != 0;

	for (i = 0; i < 8 && e[i] != ' '; i++)
		sname[n++] = lo ? tolower(e[i]) : e[i];
	if (e[8] != ' ') {
		sname[n++] = '.';
		for (i = 8; i < 11 && e[i] != ' '; i++)
			sname[n++] = elo ? tolower(e[i]) : e[i];
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
		it->at_lba = it->lba - 1;
		it->at_off = it->off;
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
		if (lfn[0] && lfn_sum(e) != lfn_ck)
			lfn[0] = 0;	/* those fragments are not this file's */
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

/* The same, for an entry we are about to walk into as a directory.  ".."
   in a first level subdirectory holds 0, meaning the root - and on FAT32
   the root is a real cluster, not 0.  Taken literally, 0 sends clus_lba()
   two clusters below the data area, into the FATs. */
static uint32_t dir_clus(const uint8_t *e)
{
	uint32_t cl = entry_clus(e);

	if (cl == 0)
		return fat32 ? root_clus : 0;
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
			cl = dir_clus(e);
			*dirclus = cl;
			e = NULL;
		}
	}
	return e;
}

/* Find one name in one directory, remembering where its entry sits so a
   put can write the entry back.  The 8.3 entry is the one located; any
   long name fragments in front of it describe a name we are not
   changing. */
static int dir_find(uint32_t dirclus, const char *name, uint32_t *elba,
		    int *eoff, uint8_t *isdir, uint32_t *clus)
{
	struct dirit it;
	uint8_t *e;

	dit_open(&it, dirclus);
	while ((e = dit_next(&it)) != NULL) {
		if (!strcasecmp(name, sname) ||
		    (lfn[0] && !strcasecmp(name, lfn))) {
			*elba = it.at_lba;
			*eoff = it.at_off;
			*isdir = (uint8_t)(e[11] & 0x10);
			*clus = entry_clus(e);
			return 1;
		}
		lfn[0] = 0;
	}
	return 0;
}

static int short_taken(uint32_t dirclus, const uint8_t *sn)
{
	struct dirit it;
	uint8_t *e;

	dit_open(&it, dirclus);
	while ((e = dit_next(&it)) != NULL) {
		if (!memcmp(e, sn, 11))
			return 1;
		lfn[0] = 0;
	}
	return 0;
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
		dirclus = dir_clus(e);
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

/* --- put ----------------------------------------------------------------- */

/* A slot in a directory, kept as numbers rather than a pointer into
   sec[] so that it survives the buffer being used for file data in
   between finding the slot and filling it in. */
struct dirpos {
	uint32_t clus;		/* cluster we are in (0 = FAT16 root) */
	uint32_t lba;		/* sector holding the slot */
	uint32_t left;		/* sectors after this one in the run */
	int off;		/* offset of the slot in that sector */
};

static void dpos_open(struct dirpos *p, uint32_t cl)
{
	p->clus = cl;
	if (cl == 0) {
		p->lba = root_lba;
		p->left = root_secs - 1;
	} else {
		p->lba = clus_lba(cl);
		p->left = spc - 1;
	}
	p->off = 0;
}

static void dir_zero(uint32_t cl)
{
	uint32_t lba = clus_lba(cl);
	uint8_t i;

	memset(sec, 0, SECSZ);
	for (i = 0; i < spc; i++)
		write_sec(lba + i, sec);
}

/* Step to the next slot.  grow extends the directory by a cluster when
   we reach the end of it, which the FAT16 fixed root can never do.
   Returns 0 when there is no next slot. */
static int dpos_next(struct dirpos *p, int grow)
{
	uint32_t nx;

	p->off += 32;
	if (p->off < SECSZ)
		return 1;
	p->off = 0;
	if (p->left) {
		p->lba++;
		p->left--;
		return 1;
	}
	if (p->clus == 0)		/* the fixed root is all there is */
		return 0;
	nx = fat_next(p->clus);
	if (nx < 2 || nx >= EOC) {
		if (!grow)
			return 0;
		nx = fat_alloc();
		if (nx == 0)
			return 0;
		fat_set(nx, EOCW);
		dir_zero(nx);		/* free slots are zero slots */
		fat_set(p->clus, nx);
	}
	p->clus = nx;
	p->lba = clus_lba(nx);
	p->left = spc - 1;
	return 1;
}

/* Reserve `need` consecutive slots, extending the directory if that is
   the only way.  Returns 0 (no room), 1 (a run of deleted entries, with
   the end of the directory still marked somewhere beyond) or 2 (fresh
   ground, so the caller must write the end marker itself). */
static int dir_reserve(uint32_t dirclus, int need, struct dirpos *out)
{
	struct dirpos p, start;
	uint32_t cur = 0xFFFFFFFFUL;
	int run = 0, i;

	dpos_open(&p, dirclus);
	start = p;
	for (;;) {
		uint8_t *e;

		if (p.lba != cur) {
			read_sec(p.lba, sec);
			cur = p.lba;
		}
		e = sec + p.off;
		if (e[0] == 0x00)
			break;			/* unused from here on */
		if (e[0] == 0xE5) {
			if (run == 0)
				start = p;
			if (++run == need) {
				*out = start;
				return 1;
			}
		} else
			run = 0;
		if (!dpos_next(&p, 0))
			return 0;		/* no marker and no room */
	}
	if (run == 0)
		start = p;
	/* Walking `need` steps from the first slot proves that all of them
	   exist AND that there is one more behind them to carry the end
	   marker; growing here rather than while writing keeps the failure
	   (a full disk) in front of any change to the file itself. */
	p = start;
	for (i = 0; i < need; i++)
		if (!dpos_next(&p, 1))
			return 0;
	*out = start;
	return 2;
}

/* Lay the prepared entries down, and behind them the zero slot that
   ends the directory when we have built on fresh ground. */
static void dir_emit(struct dirpos *p, uint8_t ents[][32], int n, int marker)
{
	uint32_t cur = 0xFFFFFFFFUL;
	int i, total = n + (marker ? 1 : 0);

	for (i = 0; i < total; i++) {
		if (p->lba != cur) {
			if (cur != 0xFFFFFFFFUL)
				write_sec(cur, sec);
			read_sec(p->lba, sec);
			cur = p->lba;
		}
		if (i < n)
			memcpy(sec + p->off, ents[i], 32);
		else
			memset(sec + p->off, 0, 32);
		if (i + 1 < total)
			dpos_next(p, 0);
	}
	write_sec(cur, sec);
}

/* The characters an 8.3 name may hold besides letters and digits. */
static int sn_ok(int c)
{
	return isalnum(c) || (c && strchr("$%'-_@~`!(){}^#&", c) != NULL);
}

/* Can this be stored as a plain 8.3 entry?  A name that is simply lower
   case in either half still can: it goes down upper case with the NT
   flag that says so, which is how "myprog.bas" makes the trip without
   a long name entry at all. */
static int fits_short(const char *name, uint8_t *sn, uint8_t *nt)
{
	const char *dot = strrchr(name, '.');
	int nb, ne, i;
	int lo = 0, up = 0, elo = 0, eup = 0;

	if (name[0] == 0 || name[0] == '.')
		return 0;
	if (dot && strchr(name, '.') != dot)
		return 0;		/* more than one dot */
	nb = dot ? (int)(dot - name) : (int)strlen(name);
	ne = dot ? (int)strlen(dot + 1) : 0;
	if (nb == 0 || nb > 8 || ne > 3 || (dot && ne == 0))
		return 0;
	memset(sn, ' ', 11);
	for (i = 0; i < nb; i++) {
		unsigned char c = name[i];
		if (!sn_ok(c))
			return 0;
		if (islower(c))
			lo = 1;
		else if (isupper(c))
			up = 1;
		sn[i] = (uint8_t)toupper(c);
	}
	for (i = 0; i < ne; i++) {
		unsigned char c = dot[1 + i];
		if (!sn_ok(c))
			return 0;
		if (islower(c))
			elo = 1;
		else if (isupper(c))
			eup = 1;
		sn[8 + i] = (uint8_t)toupper(c);
	}
	if ((lo && up) || (elo && eup))
		return 0;		/* mixed case needs a long name */
	*nt = (uint8_t)((lo ? 0x08 : 0) | (elo ? 0x10 : 0));
	if (sn[0] == 0xE5)
		sn[0] = 0x05;
	return 1;
}

/* The 8.3 name that stands in for a long one: up to six usable
   characters and then "~N", with N chosen so that nothing in this
   directory already answers to it. */
static int make_basis(const char *name, uint32_t dirclus, uint8_t *sn)
{
	const char *dot = strrchr(name, '.');
	uint8_t stem[8], ext[3];
	int i, n = 0, k = 0;
	long tail;

	if (dot == name)
		dot = NULL;
	memset(ext, ' ', 3);
	if (dot)
		for (i = 1; dot[i] && k < 3; i++) {
			unsigned char c = dot[i];
			if (c == ' ' || c == '.')
				continue;
			ext[k++] = (uint8_t)(sn_ok(c) ? toupper(c) : '_');
		}
	for (i = 0; name[i] && n < 6; i++) {
		unsigned char c = name[i];
		if (dot && name + i >= dot)
			break;
		if (c == ' ' || c == '.')
			continue;
		stem[n++] = (uint8_t)(sn_ok(c) ? toupper(c) : '_');
	}
	if (n == 0)
		stem[n++] = '_';

	for (tail = 1; tail <= 999999L; tail++) {
		char num[8];
		char rev[7];
		int nd = 0, ln = 0, j;
		long t = tail;

		do {
			rev[nd++] = (char)('0' + (int)(t % 10));
			t /= 10;
		} while (t);
		num[ln++] = '~';
		while (nd)
			num[ln++] = rev[--nd];

		memset(sn, ' ', 11);
		j = (n + ln > 8) ? 8 - ln : n;
		memcpy(sn, stem, j);
		memcpy(sn + j, num, ln);
		memcpy(sn + 8, ext, 3);
		if (!short_taken(dirclus, sn))
			return 1;
	}
	return 0;
}

static void put_lfn_frag(uint8_t *e, const char *name, int nlen, int seq,
			 int last, uint8_t sum)
{
	int base = (seq - 1) * 13;
	int i;

	memset(e, 0, 32);
	e[0] = (uint8_t)(seq | (last ? 0x40 : 0));
	e[11] = 0x0F;
	e[13] = sum;
	for (i = 0; i < 13; i++) {
		int pos = base + i;
		uint16_t u;

		if (pos < nlen)
			u = (unsigned char)name[pos];
		else if (pos == nlen)
			u = 0;			/* the terminator */
		else
			u = 0xFFFF;		/* padding */
		wr16(e + lfn_at[i], u);
	}
}

static void put_short(uint8_t *e, const uint8_t *sn, uint8_t nt,
		      uint16_t wdate, uint16_t wtime)
{
	memset(e, 0, 32);
	memcpy(e, sn, 11);
	e[11] = 0x20;			/* archive */
	e[12] = nt;
	wr16(e + 14, wtime);		/* created */
	wr16(e + 16, wdate);
	wr16(e + 18, wdate);		/* last accessed */
	wr16(e + 22, wtime);		/* last written */
	wr16(e + 24, wdate);
}

/* A name FAT can hold, and that this tool can read back afterwards:
   the reader stops at LFNMAX characters, so accepting a longer one
   would mean writing a file we could not then name. */
static int name_ok(const char *n)
{
	size_t len = strlen(n);
	const char *p;

	if (len == 0 || len > LFNMAX)
		return 0;
	if (n[len - 1] == '.' || n[len - 1] == ' ')
		return 0;
	if (!strcmp(n, ".") || !strcmp(n, ".."))
		return 0;
	for (p = n; *p; p++)
		if ((unsigned char)*p < 0x20 || strchr("\\/:*?\"<>|", *p))
			return 0;
	return 1;
}

static const char *basename_of(const char *p)
{
	const char *s = strrchr(p, '/');

	return s ? s + 1 : p;
}

static int read_full(int fd, uint8_t *buf, int len)
{
	int got = 0, n;

	while (got < len) {
		n = read(fd, buf + got, len - got);
		if (n < 0)
			return -1;
		if (n == 0)
			break;
		got += n;
	}
	return got;
}

static int do_put(const char *src, const char *dest)
{
	static char dpath[LFNMAX + 1];
	static char dpar[LFNMAX + 1];
	uint8_t ents[NLFN + 1][32];
	uint8_t sn[11], nt = 0, isdir = 0, sleft = 0;
	struct dirpos start;
	uint32_t dirclus, elba = 0, oldclus = 0;
	uint32_t first = 0, prev = 0, size = 0, lba = 0;
	uint16_t wdate, wtime;
	const char *name;
	time_t now;
	struct tm *lt;
	int eoff = 0, exists, nent = 0, marker = 0, fd, i, n;

	if (!fat32 && root_secs == 0)
		fail("this filesystem has no root directory");

	fd = open(src, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "fat: cannot open %s\n", src);
		return 1;
	}

	/* Where it lands: "fat put x" puts x in the root under its own
	   name, "fat put x dir" puts it in dir, "fat put x dir/y" names it
	   as well. */
	dirclus = fat32 ? root_clus : 0;
	name = basename_of(src);
	if (dest) {
		size_t dl;
		uint8_t *e;

		while (*dest == '/')
			dest++;
		dl = strlen(dest);
		while (dl && dest[dl - 1] == '/')
			dl--;
		if (dl > LFNMAX) {
			fprintf(stderr, "fat: %s: path too long\n", dest);
			close(fd);
			return 1;
		}
		memcpy(dpath, dest, dl);
		dpath[dl] = 0;
		if (dl) {
			e = lookup(dpath, &dirclus);
			if (e && (e[11] & 0x10))
				dirclus = dir_clus(e);
			else {
				const char *slash = strrchr(dpath, '/');

				if (slash == NULL) {
					dirclus = fat32 ? root_clus : 0;
					name = dpath;
				} else {
					size_t pn = (size_t)(slash - dpath);

					memcpy(dpar, dpath, pn);
					dpar[pn] = 0;
					e = lookup(dpar, &dirclus);
					if (e == NULL || !(e[11] & 0x10)) {
						fprintf(stderr, "fat: %s: no "
							"such directory\n",
							dpar);
						close(fd);
						return 1;
					}
					dirclus = dir_clus(e);
					name = slash + 1;
				}
			}
		}
	}

	if (!name_ok(name)) {
		fprintf(stderr, "fat: %s: not a name FAT can hold\n", name);
		close(fd);
		return 1;
	}

	exists = dir_find(dirclus, name, &elba, &eoff, &isdir, &oldclus);
	if (exists && isdir) {
		fprintf(stderr, "fat: %s: is a directory\n", name);
		close(fd);
		return 1;
	}

	time(&now);
	lt = localtime(&now);
	if (lt && lt->tm_year >= 80) {
		wdate = (uint16_t)(((lt->tm_year - 80) << 9) |
				   ((lt->tm_mon + 1) << 5) | lt->tm_mday);
		wtime = (uint16_t)((lt->tm_hour << 11) | (lt->tm_min << 5) |
				   (lt->tm_sec / 2));
	} else {
		wdate = (1 << 5) | 1;		/* 1980-01-01 */
		wtime = 0;
	}

	/* Build the entries and claim room for them before a byte of the
	   file is written, so that "the directory is full" costs nothing. */
	if (!exists) {
		if (fits_short(name, sn, &nt))
			nent = 0;
		else {
			if (!make_basis(name, dirclus, sn)) {
				fprintf(stderr, "fat: %s: no free short name\n",
					name);
				close(fd);
				return 1;
			}
			nent = ((int)strlen(name) + 12) / 13;
			nt = 0;
		}
		for (i = 0; i < nent; i++)
			put_lfn_frag(ents[i], name, (int)strlen(name),
				     nent - i, i == 0, lfn_sum(sn));
		put_short(ents[nent], sn, nt, wdate, wtime);

		marker = dir_reserve(dirclus, nent + 1, &start);
		if (marker == 0) {
			fprintf(stderr, "fat: %s: directory full\n",
				dest ? dest : "/");
			close(fd);
			return 1;
		}
		marker = (marker == 2);
	}

	/* The file itself.  Clusters are linked as they are filled, so a
	   put that dies here leaves a chain nothing points at. */
	for (;;) {
		n = read_full(fd, sec, SECSZ);
		if (n < 0) {
			fprintf(stderr, "fat: read error on %s\n", src);
			close(fd);
			free_chain(first);
			fat_flush();
			return 1;
		}
		if (n == 0)
			break;
		if (n < SECSZ)
			memset(sec + n, 0, SECSZ - n);
		if (sleft == 0) {
			uint32_t cl = fat_alloc();

			if (cl == 0) {
				fprintf(stderr, "fat: %s: no room on %s\n",
					src, devname);
				close(fd);
				free_chain(first);
				fat_flush();
				return 1;
			}
			fat_set(cl, EOCW);
			if (prev)
				fat_set(prev, cl);
			else
				first = cl;
			prev = cl;
			lba = clus_lba(cl);
			sleft = spc;
		}
		write_sec(lba++, sec);
		sleft--;
		size += (uint32_t)n;
		if (n < SECSZ)
			break;			/* that was the last of it */
	}
	close(fd);

	/* The chain reaches the card before anything names it. */
	fat_flush();

	if (exists) {
		uint8_t *e;

		read_sec(elba, sec);
		e = sec + eoff;
		wr16(e + 20, (uint16_t)(first >> 16));
		wr16(e + 26, (uint16_t)first);
		wr32(e + 28, size);
		wr16(e + 22, wtime);
		wr16(e + 24, wdate);
		write_sec(elba, sec);
		free_chain(oldclus);	/* only now is the old one unwanted */
		fat_flush();
	} else {
		wr16(ents[nent] + 20, (uint16_t)(first >> 16));
		wr16(ents[nent] + 26, (uint16_t)first);
		wr32(ents[nent] + 28, size);
		dir_emit(&start, ents, nent + 1, marker);
	}

	fsinfo_update();
	sync();
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
	    "       fat [-d device] put src [dest] dest may name a\n"
	    "                                      directory to put it in\n"
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
	fat_mount(!strcmp(argv[a], "put"));
	if (!strcmp(argv[a], "ls"))
		return do_ls(argv[a + 1]);
	if (!strcmp(argv[a], "get")) {
		if (a + 1 >= argc)
			usage();
		return do_get(argv[a + 1], argv[a + 2]);
	}
	if (!strcmp(argv[a], "put")) {
		if (a + 1 >= argc)
			usage();
		return do_put(argv[a + 1], argv[a + 2]);
	}
	if (!strcmp(argv[a], "info"))
		return do_info();
	usage();
	return 1;
}

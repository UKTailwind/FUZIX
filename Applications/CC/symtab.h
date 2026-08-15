/*
 *	Distinct identifiers cc0 can hold.  A large generated program -
 *	picofrog's translation was the first - carries more names than
 *	any hand-written source here ever did, and the failure is a hard
 *	stop ("too many sybmols").  The table costs MAXNAME * sizeof
 *	(struct name), about 40 bytes an entry on ARM: 2048 is +40K
 *	against a 256K process, which is the NAMELEN argument below over
 *	again.  The host takes headroom on top under BIG_TABLES, and a
 *	Z80-class target keeps the old size.
 */
#ifndef MAXNAME
#ifdef BIG_TABLES
#define MAXNAME		4096
#elif defined(CPU_armm0)
#define MAXNAME		2048
#else
#define MAXNAME		1024
#endif
#endif

/*
 *	Significant characters in an identifier, including the NUL.
 *
 *	C89 requires 31 for an internal name. At 16 this compiler had 15,
 *	and silently folded anything longer together - two globals
 *	differing only after the fifteenth character became one variable,
 *	with no diagnostic. That is the worst kind of dialect gap because
 *	the program still builds.
 *
 *	The cost is NAMELEN bytes times MAXNAME in cc0's table, so 32
 *	costs 16K more than 16 did. That is nothing against a 256K
 *	process and would matter on a Z80, hence the split.
 */
#ifdef CPU_armm0
#define	NAMELEN		32	/* 31 significant, as C89 asks */
#else
#define	NAMELEN		16	/* 15 usable due to _ lead on C names */
#endif

struct name {
	char name[NAMELEN];
	uint16_t id;
	struct name *next;
};

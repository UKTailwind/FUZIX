#ifndef MMB_PEEK_H
#define MMB_PEEK_H
/*
 *	PEEK(BYTE addr) and its wider relatives - reading memory by
 *	address, which on this machine means reading memory.
 *
 *	    PEEK(BYTE addr)		one unsigned byte
 *	    PEEK(SHORT addr)		sixteen bits, signed, MMBasic's
 *	    PEEK(WORD addr)		thirty-two bits, unsigned
 *	    PEEK(INTEGER addr)		sixty-four bits, signed
 *	    PEEK(FLOAT addr)		a double
 *
 *	MMBasic's cmd/fun_peek (MM_Misc.c) with the same option names and
 *	the same widths.  What it does NOT have is MMBasic's VAR, VARADDR
 *	and CFUNADDR forms: those need the symbol table on this side of
 *	the translator, and an honest gap is better than a spelling that
 *	means something slightly different.
 *
 *	There is no MMU and no MPU on an RP2350B, so an address here is a
 *	machine address and every one of these is a load instruction with
 *	nothing between it and the bus.  That is the point - it is what
 *	makes MM.INFO(FONT ADDRESS n) worth having, because the fonts are
 *	in the kernel's flash and a program can read them where they lie.
 *	It is also the risk: a wrong address does not raise "Address out
 *	of range", it faults and the process dies, exactly as it would in
 *	C.  MMBasic on a PicoMite behaves the same way.
 *
 *	ALIGNMENT.  MMBasic insists an address be a multiple of the width
 *	and errors if it is not; that check is here for the same reason
 *	it is there, and a stronger one on this part - an unaligned STRD
 *	is a HardFault on Cortex-M33 rather than a slow load, and the
 *	failure looks like the VM stack bug in bcrun, not like a bad
 *	PEEK.  Refusing it early names the mistake.
 */

#include "mmb_runtime.h"

/*	The same bargain the other mmb_*.h headers make: keyed on which
 *	compiler compiles the OUTPUT, because fccbuild.sh preprocesses
 *	with gcc and then feeds cc1.  Guarded so a program including
 *	several of these defines it once.
 *
 *	This header is the one that most needs its own copy: a program can
 *	PEEK without touching a pin, so it is the only mmb_*.h that gets
 *	included with none of the others.  Leaving it out compiled every
 *	program that also opened SPI or a pin and failed only on the ones
 *	that did not - which is to say it passed on the board and failed
 *	on the one-page example. */
#ifndef MMG_FN
#if defined(MM_FCC) || defined(MM_PC3)
#define MMG_FN static
#else
#define MMG_FN static __inline__ __attribute__((unused))
#endif
#endif

/*	The address arrives as a signed 64-bit BASIC integer and has to
 *	become a pointer.  Through uintptr_t rather than straight to the
 *	pointer type: the board is 32-bit and the gates run on a 64-bit
 *	host, and this is the one cast that is correct on both.  The
 *	unsigned step first, so a program that computed its address in a
 *	way that set the top bit does not sign-extend into nonsense. */
#define MMPK_PTR(t, a)	((const t *)(uintptr_t)(uint64_t)(a))

/*	mm_error takes one string, so the message is the caller's - which
 *	is how MMBasic words it too, naming the width that was wanted. */
MMG_FN int mmpk_aligned(MMINTEGER addr, int width, const char *msg)
{
	if (addr & (MMINTEGER)(width - 1)) {
		mm_error(msg);
		return 0;
	}
	return 1;
}

MMG_FN MMINTEGER mmpk_byte(MMINTEGER addr)
{
	return (MMINTEGER)*MMPK_PTR(unsigned char, addr);
}

MMG_FN MMINTEGER mmpk_short(MMINTEGER addr)
{
	if (!mmpk_aligned(addr, 2, "Address not divisible by 2"))
		return 0;
	return (MMINTEGER)*MMPK_PTR(short, addr);
}

MMG_FN MMINTEGER mmpk_word(MMINTEGER addr)
{
	if (!mmpk_aligned(addr, 4, "Address not divisible by 4"))
		return 0;
	return (MMINTEGER)*MMPK_PTR(unsigned int, addr);
}

MMG_FN MMINTEGER mmpk_integer(MMINTEGER addr)
{
	if (!mmpk_aligned(addr, 8, "Address not divisible by 8"))
		return 0;
	return *MMPK_PTR(MMINTEGER, addr);
}

MMG_FN MMFLOAT mmpk_float(MMINTEGER addr)
{
	if (!mmpk_aligned(addr, 8, "Address not divisible by 8"))
		return 0;
	return *MMPK_PTR(MMFLOAT, addr);
}

/*
 *	POKE BYTE addr, value and its wider relatives - writing memory by
 *	address, which on this machine means writing memory.
 *
 *	MMBasic's cmd_poke (MM_Misc.c:8236), same option names, same
 *	widths, and the same absence of a range check: its POKERANGE test
 *	is commented out in the firmware, so a wrong address faults here
 *	exactly as it does there.  Everything the PEEK comment above says
 *	about there being no MMU applies with more force - a stray read
 *	is a wrong answer, a stray write is a corrupted machine.
 *
 *	The alignment rule is the same and for the same reason: a byte
 *	needs none, the rest do, and an unaligned 64-bit store on a
 *	Cortex-M33 is a HardFault rather than a slow store.
 */
#define MMPK_WPTR(t, a)	((t *)(uintptr_t)(uint64_t)(a))

MMG_FN void mmpk_poke_byte(MMINTEGER addr, MMINTEGER v)
{
	*MMPK_WPTR(unsigned char, addr) = (unsigned char)v;
}

MMG_FN void mmpk_poke_short(MMINTEGER addr, MMINTEGER v)
{
	if (!mmpk_aligned(addr, 2, "Address not divisible by 2"))
		return;
	*MMPK_WPTR(short, addr) = (short)v;
}

MMG_FN void mmpk_poke_word(MMINTEGER addr, MMINTEGER v)
{
	if (!mmpk_aligned(addr, 4, "Address not divisible by 4"))
		return;
	*MMPK_WPTR(unsigned int, addr) = (unsigned int)v;
}

MMG_FN void mmpk_poke_integer(MMINTEGER addr, MMINTEGER v)
{
	if (!mmpk_aligned(addr, 8, "Address not divisible by 8"))
		return;
	*MMPK_WPTR(MMINTEGER, addr) = v;
}

MMG_FN void mmpk_poke_float(MMINTEGER addr, MMFLOAT v)
{
	if (!mmpk_aligned(addr, 8, "Address not divisible by 8"))
		return;
	*MMPK_WPTR(MMFLOAT, addr) = v;
}

#endif /* MMB_PEEK_H */

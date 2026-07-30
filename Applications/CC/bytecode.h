/*
 *	The PC3 bytecode: a language-neutral intermediate form.
 *
 *	Frozen 2026-07-28. See BYTECODE.md for the rationale and the
 *	machine model; this header is the normative encoding and is
 *	shared by the emitter (cc2), the interpreter and the Thumb
 *	translator.
 *
 *	Derived from the operation set in Operations.md, with one
 *	significant change: arithmetic is always 32bit. Width only
 *	matters when touching memory or converting, which is how the
 *	target machine actually behaves and removes every 16/32 variant
 *	from the arithmetic opcodes.
 */

#ifndef _BYTECODE_H
#define _BYTECODE_H

#include <stdint.h>

#define BC_MAGIC	"FBC1"
#define BC_VERSION	1
/*
 *	An object whose code segment contains native (Thumb) functions
 *	carries version 2, so an interpreter that predates mixed mode
 *	rejects it cleanly at load ("version 2, expected 1") instead of
 *	faulting on the marker mid-run.  Interpreters from version 2 on
 *	accept both.
 */
#define BC_VERSION_NATIVE 2

/*
 *	Machine model
 *
 *	A	accumulator, 32bit, holds any value
 *	S	evaluation stack of 32bit slots
 *	FP	frame pointer; locals and arguments are at FP + offset
 *	PC	program counter
 *
 *	Binary operators take the left operand from the stack and the
 *	right from A, leaving the result in A:  A = pop() OP A.
 *	That is the order cc2's tree walker already produces.
 */

/* Constants and addresses ------------------------------------------ */
#define BC_NOP		0x00
#define BC_CONST8	0x01	/* i8	A = sign extend imm */
#define BC_CONST16	0x02	/* i16	A = sign extend imm */
#define BC_CONST32	0x03	/* i32	A = imm */
#define BC_ADDR		0x04	/* u32	A = address, patched via a fixup */
#define BC_LOCAL8	0x05	/* u8	A = FP + imm */
#define BC_LOCAL16	0x06	/* u16	A = FP + imm */

/* Stack ------------------------------------------------------------ */
#define BC_PUSH		0x08	/* push A */
#define BC_POP		0x09	/* A = pop */
#define BC_DUP		0x0A	/* push top of stack again */
#define BC_SWAP		0x0B	/* exchange A with top of stack */
#define BC_DROP		0x0C	/* discard top of stack */

/* Loads: A holds the address, and is replaced by the value ---------- */
#define BC_LOAD8S	0x10
#define BC_LOAD8U	0x11
#define BC_LOAD16S	0x12
#define BC_LOAD16U	0x13
#define BC_LOAD32	0x14

/* Stores: address from the stack, value in A, which is retained so an
   assignment yields its value ------------------------------------- */
#define BC_STORE8	0x18
#define BC_STORE16	0x19
#define BC_STORE32	0x1A

/* Arithmetic, all 32bit -------------------------------------------- */
#define BC_ADD		0x20
#define BC_SUB		0x21
#define BC_MUL		0x22
#define BC_DIVS		0x23
#define BC_DIVU		0x24
#define BC_REMS		0x25
#define BC_REMU		0x26
#define BC_AND		0x27
#define BC_OR		0x28
#define BC_XOR		0x29
#define BC_SHL		0x2A
#define BC_SHRS		0x2B
#define BC_SHRU		0x2C
#define BC_NEG		0x2D	/* A = -A */
#define BC_NOT		0x2E	/* A = ~A */
#define BC_LNOT		0x2F	/* A = !A */

/* Comparison, result 0 or 1 ---------------------------------------- */
#define BC_EQ		0x30
#define BC_NE		0x31
#define BC_LTS		0x32
#define BC_LTU		0x33
#define BC_GTS		0x34
#define BC_GTU		0x35
#define BC_LES		0x36
#define BC_LEU		0x37
#define BC_GES		0x38
#define BC_GEU		0x39
#define BC_BOOL		0x3A	/* A = (A != 0) */

/* Width conversion -------------------------------------------------- */
#define BC_SEXT8	0x40
#define BC_SEXT16	0x41
#define BC_ZEXT8	0x42
#define BC_ZEXT16	0x43

/* Control ----------------------------------------------------------- */
#define BC_JUMP		0x50	/* i16 pc-relative from end of instruction */
#define BC_JFALSE	0x51	/* i16 */
#define BC_JTRUE	0x52	/* i16 */
#define BC_CALL		0x53	/* u32 address, patched via a fixup */
#define BC_CALLA	0x54	/* call the address in A */
#define BC_RET		0x55
#define BC_ENTER	0x56	/* u16 bytes of locals to reserve */
#define BC_LEAVE	0x57	/* u16 bytes of locals to release */
#define BC_ARGS		0x58	/* u8  bytes of arguments to discard */
#define BC_LIBCALL	0x59	/* u16 runtime library function index */
#define BC_SWITCH	0x5A	/* u32 table address, patched via a fixup */

/*
 *	64-bit forms, for long long (and later double, which wants the
 *	same 64-bit accumulator and slot).
 *
 *	The accumulator is 64 bits wide. The 32-bit operations above
 *	truncate their result back to 32 bits, so "int" keeps wrapping
 *	the way C requires; these do not. A 64-bit value occupies two
 *	stack slots, which is why push and pop have their own forms -
 *	the slots stay 4 bytes so argument offsets are unaffected.
 */
#define BC_CONST64	0x60	/* i64 */
#define BC_LOAD64	0x61
#define BC_STORE64	0x62
#define BC_PUSH64	0x63
#define BC_POP64	0x64
#define BC_SEXT32	0x65	/* 32 -> 64, signed */
#define BC_ZEXT32	0x66	/* 32 -> 64, unsigned */
#define BC_TRUNC64	0x67	/* 64 -> 32 */

#define BC_ADD64	0x68
#define BC_SUB64	0x69
#define BC_MUL64	0x6A
#define BC_DIVS64	0x6B
#define BC_DIVU64	0x6C
#define BC_REMS64	0x6D
#define BC_REMU64	0x6E
#define BC_AND64	0x6F
#define BC_OR64		0x70
#define BC_XOR64	0x71
#define BC_SHL64	0x72
#define BC_SHRS64	0x73
#define BC_SHRU64	0x74
#define BC_NEG64	0x75
#define BC_NOT64	0x76
#define BC_LNOT64	0x77

#define BC_EQ64		0x78
#define BC_NE64		0x79
#define BC_LTS64	0x7A
#define BC_LTU64	0x7B
#define BC_GTS64	0x7C
#define BC_GTU64	0x7D
#define BC_LES64	0x7E
#define BC_LEU64	0x7F
#define BC_GES64	0x80
#define BC_GEU64	0x81
#define BC_BOOL64	0x82

/*
 *	Floating point.
 *
 *	A double lives in the accumulator as its IEEE754 bit pattern and a
 *	float in the low 32 bits of it, so none of these need loads,
 *	stores, pushes or constants of their own - CONST64/LOAD64/STORE64/
 *	PUSH64/POP64 already move eight bytes and the 32-bit forms move
 *	four. Only the operations that have to interpret the bits are new.
 *
 *	Float is not done by promoting to double and back. That would need
 *	the stacked operand converted as well as the one in A, which the
 *	machine has no way to reach.
 */
#define BC_ADDD		0x90
#define BC_SUBD		0x91
#define BC_MULD		0x92
#define BC_DIVD		0x93
#define BC_NEGD		0x94
#define BC_EQD		0x95
#define BC_NED		0x96
#define BC_LTD		0x97
#define BC_GTD		0x98
#define BC_LED		0x99
#define BC_GED		0x9A
#define BC_BOOLD	0x9B	/* A = (d != 0) */
#define BC_LNOTD	0x9C	/* A = (d == 0) */

#define BC_ADDF		0xA0
#define BC_SUBF		0xA1
#define BC_MULF		0xA2
#define BC_DIVF		0xA3
#define BC_NEGF		0xA4
#define BC_EQF		0xA5
#define BC_NEF		0xA6
#define BC_LTF		0xA7
#define BC_GTF		0xA8
#define BC_LEF		0xA9
#define BC_GEF		0xAA
#define BC_BOOLF	0xAB
#define BC_LNOTF	0xAC

/*
 *	Conversions. The integer side is always the full 64-bit
 *	accumulator, so an int converts by widening to 64 first with
 *	SEXT32/ZEXT32 and a narrower result truncates afterwards with
 *	TRUNC64. That keeps one conversion per pair instead of one per
 *	width.
 */
#define BC_I2D		0xB0	/* int64  -> double */
#define BC_U2D		0xB1	/* uint64 -> double */
#define BC_D2I		0xB2	/* double -> int64 */
#define BC_D2U		0xB3	/* double -> uint64 */
#define BC_I2F		0xB4
#define BC_U2F		0xB5
#define BC_F2I		0xB6
#define BC_F2U		0xB7
#define BC_F2D		0xB8	/* float  -> double */
#define BC_D2F		0xB9	/* double -> float */

/*
 *	Aggregates.
 *
 *	A struct is too big for the accumulator, so a struct valued
 *	expression is represented by its address and moving one is a block
 *	copy. That covers assignment, passing by value and returning by
 *	value - all three are the same operation.
 *
 *	    u16  bytes to copy
 *	    source address in A, destination on the stack
 *	    the destination is left in A, so "a = b = c" works
 *
 *	The size is an immediate because the code generator cannot size a
 *	struct: that knowledge lives in cc1's symbol table.
 */
#define BC_COPY		0xC0	/* u16 length */
#define BC_PUSHN	0xC1	/* u16 length: push that many bytes from
				   the address in A, rounded up to a whole
				   number of stack slots */

#define BC_MAXOP	0xC1

/*
 *	Mixed mode.  Not an executable opcode: the first code byte of a
 *	native (Thumb) function is this marker, and every call goes
 *	through a dispatch that tests it.  Layout:
 *
 *	    +0  BC_NATIVE
 *	    +1  u32  bytecode alias - the code offset of the function's
 *	        original bytecode, still present in the object, so a host
 *	        that cannot execute Thumb (x86 development machine, or ARM
 *	        with BCRUN_BYTECODE=1 for A/B timing) interprets instead.
 *	        0xFFFFFFFF = none (hand-written native only).
 *	    +5  pad to even, then Thumb code - BC_NATIVE_ENTRY(off).
 *
 *	Registers on native entry: r4 = VM stack pointer as a native
 *	pointer (the return-pc slot at r4+0 exists for frame parity with
 *	a bytecode callee, args from r4+4 up), r5 = helper vector, r6 =
 *	mem[] base; the result returns in r0/r1 exactly as the
 *	accumulator A.  See PLAN-arm-backend.md.
 */
#define BC_NATIVE	0xF0
#define BC_NATIVE_ENTRY(off)	(((off) + 6) & ~1UL)

/*
 *	Object file layout. Everything is little endian.
 *
 *	  struct bc_header
 *	  code   [h_code]
 *	  data   [h_data]
 *	  fixups [h_nfixup] of struct bc_fixup
 *	  symbols[h_nsym]   of struct bc_sym
 *	  strings[h_strsize]  NUL separated, indexed by s_name
 *
 *	Names are kept because BC_SYM_LIB symbols have to be matched
 *	against the runtime the interpreter provides; an index alone
 *	would not say which function is meant.
 *
 *	There is no linker. A fixup names a symbol whose value the loader
 *	adds to the 32bit field at the given code or data offset. Symbols
 *	are local to the module except BC_SYM_LIB, which names a runtime
 *	library entry point the interpreter provides.
 */

struct bc_header {
	char h_magic[4];	/* BC_MAGIC */
	uint8_t h_version;
	uint8_t h_pad;
	uint16_t h_nsym;
	uint32_t h_code;	/* bytes of code */
	uint32_t h_data;	/* bytes of initialised data */
	uint32_t h_bss;	/* bytes of zeroed data */
	uint32_t h_entry;	/* code offset of the entry point */
	uint32_t h_nfixup;
	uint32_t h_strsize;	/* bytes of string table */
};

#define BC_SEG_CODE	0
#define BC_SEG_DATA	1

struct bc_fixup {
	uint32_t f_offset;	/* where to patch */
	uint16_t f_sym;	/* which symbol */
	uint8_t f_seg;	/* BC_SEG_* that f_offset is within */
	uint8_t f_pad;
};

#define BC_SYM_CODE	0	/* offset within the code segment */
#define BC_SYM_DATA	1	/* offset within data */
#define BC_SYM_BSS	2	/* offset within bss */
#define BC_SYM_LIB	3	/* runtime library index, not an address */

struct bc_sym {
	uint32_t s_value;
	uint32_t s_name;	/* offset into the string table */
	uint8_t s_type;	/* BC_SYM_* */
	uint8_t s_pad[3];
};

#endif

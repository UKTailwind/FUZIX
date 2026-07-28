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

#define BC_MAXOP	0x5A

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

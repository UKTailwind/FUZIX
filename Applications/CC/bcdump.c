/*
 *	Disassemble a PC3 bytecode object.
 *
 *	Verification tool for the emitter, and the reference decoder for
 *	the interpreter and the Thumb translator to be written against.
 *
 *	  bcdump file.bc
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bytecode.h"

static unsigned char *code;
static unsigned char *data;
static struct bc_fixup *fix;
static struct bc_sym *sym;
static struct bc_header h;
static char *strtab;

static const char *sname(unsigned i)
{
	if (strtab == NULL || i >= h.h_nsym)
		return "?";
	return strtab + sym[i].s_name;
}

/* Operand form for each opcode */
enum { OP_NONE, OP_I8, OP_U8, OP_I16, OP_U16, OP_I32, OP_REL16, OP_A32,
       OP_I64 };

static struct {
	unsigned char op;
	const char *name;
	unsigned char form;
} ops[] = {
	{ BC_NOP,	"nop",		OP_NONE },
	{ BC_CONST8,	"const8",	OP_I8 },
	{ BC_CONST16,	"const16",	OP_I16 },
	{ BC_CONST32,	"const32",	OP_I32 },
	{ BC_ADDR,	"addr",		OP_A32 },
	{ BC_LOCAL8,	"local8",	OP_U8 },
	{ BC_LOCAL16,	"local16",	OP_U16 },
	{ BC_PUSH,	"push",		OP_NONE },
	{ BC_POP,	"pop",		OP_NONE },
	{ BC_DUP,	"dup",		OP_NONE },
	{ BC_SWAP,	"swap",		OP_NONE },
	{ BC_DROP,	"drop",		OP_NONE },
	{ BC_LOAD8S,	"load8s",	OP_NONE },
	{ BC_LOAD8U,	"load8u",	OP_NONE },
	{ BC_LOAD16S,	"load16s",	OP_NONE },
	{ BC_LOAD16U,	"load16u",	OP_NONE },
	{ BC_LOAD32,	"load32",	OP_NONE },
	{ BC_STORE8,	"store8",	OP_NONE },
	{ BC_STORE16,	"store16",	OP_NONE },
	{ BC_STORE32,	"store32",	OP_NONE },
	{ BC_ADD,	"add",		OP_NONE },
	{ BC_SUB,	"sub",		OP_NONE },
	{ BC_MUL,	"mul",		OP_NONE },
	{ BC_DIVS,	"divs",		OP_NONE },
	{ BC_DIVU,	"divu",		OP_NONE },
	{ BC_REMS,	"rems",		OP_NONE },
	{ BC_REMU,	"remu",		OP_NONE },
	{ BC_AND,	"and",		OP_NONE },
	{ BC_OR,	"or",		OP_NONE },
	{ BC_XOR,	"xor",		OP_NONE },
	{ BC_SHL,	"shl",		OP_NONE },
	{ BC_SHRS,	"shrs",		OP_NONE },
	{ BC_SHRU,	"shru",		OP_NONE },
	{ BC_NEG,	"neg",		OP_NONE },
	{ BC_NOT,	"not",		OP_NONE },
	{ BC_LNOT,	"lnot",		OP_NONE },
	{ BC_EQ,	"eq",		OP_NONE },
	{ BC_NE,	"ne",		OP_NONE },
	{ BC_LTS,	"lts",		OP_NONE },
	{ BC_LTU,	"ltu",		OP_NONE },
	{ BC_GTS,	"gts",		OP_NONE },
	{ BC_GTU,	"gtu",		OP_NONE },
	{ BC_LES,	"les",		OP_NONE },
	{ BC_LEU,	"leu",		OP_NONE },
	{ BC_GES,	"ges",		OP_NONE },
	{ BC_GEU,	"geu",		OP_NONE },
	{ BC_BOOL,	"bool",		OP_NONE },
	{ BC_SEXT8,	"sext8",	OP_NONE },
	{ BC_SEXT16,	"sext16",	OP_NONE },
	{ BC_ZEXT8,	"zext8",	OP_NONE },
	{ BC_ZEXT16,	"zext16",	OP_NONE },
	{ BC_JUMP,	"jump",		OP_REL16 },
	{ BC_JFALSE,	"jfalse",	OP_REL16 },
	{ BC_JTRUE,	"jtrue",	OP_REL16 },
	{ BC_CALL,	"call",		OP_A32 },
	{ BC_CALLA,	"calla",	OP_NONE },
	{ BC_RET,	"ret",		OP_NONE },
	{ BC_ENTER,	"enter",	OP_U16 },
	{ BC_LEAVE,	"leave",	OP_U16 },
	{ BC_ARGS,	"args",		OP_U8 },
	{ BC_LIBCALL,	"libcall",	OP_U16 },
	{ BC_SWITCH,	"switch",	OP_A32 },

	{ BC_CONST64,	"const64",	OP_I64 },
	{ BC_LOAD64,	"load64",	OP_NONE },
	{ BC_STORE64,	"store64",	OP_NONE },
	{ BC_PUSH64,	"push64",	OP_NONE },
	{ BC_POP64,	"pop64",	OP_NONE },
	{ BC_SEXT32,	"sext32",	OP_NONE },
	{ BC_ZEXT32,	"zext32",	OP_NONE },
	{ BC_TRUNC64,	"trunc64",	OP_NONE },
	{ BC_ADD64,	"add64",	OP_NONE },
	{ BC_SUB64,	"sub64",	OP_NONE },
	{ BC_MUL64,	"mul64",	OP_NONE },
	{ BC_DIVS64,	"divs64",	OP_NONE },
	{ BC_DIVU64,	"divu64",	OP_NONE },
	{ BC_REMS64,	"rems64",	OP_NONE },
	{ BC_REMU64,	"remu64",	OP_NONE },
	{ BC_AND64,	"and64",	OP_NONE },
	{ BC_OR64,	"or64",		OP_NONE },
	{ BC_XOR64,	"xor64",	OP_NONE },
	{ BC_SHL64,	"shl64",	OP_NONE },
	{ BC_SHRS64,	"shrs64",	OP_NONE },
	{ BC_SHRU64,	"shru64",	OP_NONE },
	{ BC_NEG64,	"neg64",	OP_NONE },
	{ BC_NOT64,	"not64",	OP_NONE },
	{ BC_LNOT64,	"lnot64",	OP_NONE },
	{ BC_EQ64,	"eq64",		OP_NONE },
	{ BC_NE64,	"ne64",		OP_NONE },
	{ BC_LTS64,	"lts64",	OP_NONE },
	{ BC_LTU64,	"ltu64",	OP_NONE },
	{ BC_GTS64,	"gts64",	OP_NONE },
	{ BC_GTU64,	"gtu64",	OP_NONE },
	{ BC_LES64,	"les64",	OP_NONE },
	{ BC_LEU64,	"leu64",	OP_NONE },
	{ BC_GES64,	"ges64",	OP_NONE },
	{ BC_GEU64,	"geu64",	OP_NONE },
	{ BC_BOOL64,	"bool64",	OP_NONE },
	{ 0, NULL, 0 }
};

static unsigned long get32(unsigned char *p)
{
	return p[0] | ((unsigned long)p[1] << 8) |
	    ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

static unsigned get16(unsigned char *p)
{
	return p[0] | (p[1] << 8);
}

static const char *symtype(unsigned t)
{
	switch (t) {
	case BC_SYM_CODE: return "code";
	case BC_SYM_DATA: return "data";
	case BC_SYM_BSS:  return "bss";
	case BC_SYM_LIB:  return "LIB";
	}
	return "?";
}

/* Is there a fixup covering this code offset? */
static int fixat(unsigned long off)
{
	unsigned long i;
	for (i = 0; i < h.h_nfixup; i++)
		if (fix[i].f_seg == BC_SEG_CODE && fix[i].f_offset == off)
			return fix[i].f_sym;
	return -1;
}

int main(int argc, char *argv[])
{
	FILE *f;
	unsigned long pc = 0;
	unsigned long i;

	if (argc != 2) {
		fprintf(stderr, "usage: bcdump file.bc\n");
		return 1;
	}
	f = fopen(argv[1], "rb");
	if (f == NULL) {
		perror(argv[1]);
		return 1;
	}
	if (fread(&h, sizeof(h), 1, f) != 1 ||
	    memcmp(h.h_magic, BC_MAGIC, 4) != 0) {
		fprintf(stderr, "%s: not a bytecode object\n", argv[1]);
		return 1;
	}
	code = malloc(h.h_code ? h.h_code : 1);
	data = malloc(h.h_data ? h.h_data : 1);
	fix = malloc((h.h_nfixup ? h.h_nfixup : 1) * sizeof(struct bc_fixup));
	sym = malloc((h.h_nsym ? h.h_nsym : 1) * sizeof(struct bc_sym));
	if (fread(code, 1, h.h_code, f) != h.h_code)
		fprintf(stderr, "short code\n");
	if (fread(data, 1, h.h_data, f) != h.h_data)
		fprintf(stderr, "short data\n");
	for (i = 0; i < h.h_nfixup; i++)
		fread(&fix[i], sizeof(struct bc_fixup), 1, f);
	for (i = 0; i < h.h_nsym; i++)
		fread(&sym[i], sizeof(struct bc_sym), 1, f);
	strtab = malloc(h.h_strsize ? h.h_strsize : 1);
	if (h.h_strsize && fread(strtab, 1, h.h_strsize, f) != h.h_strsize)
		fprintf(stderr, "short string table\n");
	fclose(f);

	printf("version %u  code %lu  data %lu  bss %lu  entry %lu  "
	       "syms %u  fixups %lu\n\n",
	       h.h_version, h.h_code, h.h_data, h.h_bss, h.h_entry,
	       h.h_nsym, h.h_nfixup);

	while (pc < h.h_code) {
		unsigned char op = code[pc];
		unsigned long at = pc;
		int j, found = -1;
		int fs;

		for (j = 0; ops[j].name; j++)
			if (ops[j].op == op) {
				found = j;
				break;
			}
		printf("%04lx: ", at);
		if (found < 0) {
			printf("?? %02x\n", op);
			pc++;
			continue;
		}
		pc++;
		printf("%-8s", ops[found].name);
		switch (ops[found].form) {
		case OP_NONE:
			break;
		case OP_I8:
			printf(" %d", (signed char)code[pc]);
			pc += 1;
			break;
		case OP_U8:
			printf(" %u", code[pc]);
			pc += 1;
			break;
		case OP_I16:
			printf(" %d", (short)get16(code + pc));
			pc += 2;
			break;
		case OP_U16:
			if (op == BC_LIBCALL)
				printf(" %s", sname(get16(code + pc)));
			else
				printf(" %u", get16(code + pc));
			pc += 2;
			break;
		case OP_I32:
			printf(" %ld", (long)get32(code + pc));
			pc += 4;
			break;
		case OP_I64:
			{
				unsigned long long v = get32(code + pc) |
				    ((unsigned long long)get32(code + pc + 4) << 32);
				printf(" %lld", (long long)v);
				pc += 8;
			}
			break;
		case OP_REL16:
			{
				short d = (short)get16(code + pc);
				pc += 2;
				printf(" %+d\t; -> %04lx", d, pc + d);
			}
			break;
		case OP_A32:
			fs = fixat(pc);
			printf(" +%ld", (long)get32(code + pc));
			if (fs >= 0)
				printf("\t; %s (%s)", sname(fs),
				       symtype(sym[fs].s_type));
			pc += 4;
			break;
		}
		putchar('\n');
	}

	printf("\nsymbols:\n");
	for (i = 0; i < h.h_nsym; i++)
		printf("  %3lu  %-5s %-6lu %s\n", i, symtype(sym[i].s_type),
		       sym[i].s_value, sname(i));
	printf("\nfixups:\n");
	for (i = 0; i < h.h_nfixup; i++)
		printf("  %s+%lu -> sym %u\n",
		       fix[i].f_seg == BC_SEG_CODE ? "code" : "data",
		       fix[i].f_offset, fix[i].f_sym);
	return 0;
}

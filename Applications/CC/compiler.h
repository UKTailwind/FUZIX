
/* Pass 2 values */

/* This controls the number of symbols (including complex types, arrays and
   unique function prototypes. Cost is 10 bytes per node on a small box. We
   can probably make symbols the self expanding one eventually */
/* 768 was not enough for a real program: picofrog, a 1200-line BASIC game
   translated to C, ran out at "<stdin>:9600 - too many symbols".  A
   symbol here is 16 bytes on this target, so 2048 costs 20K more bss in
   a compiler that had 113K in a 256K process, and nothing else changes -
   the table is scanned by pointer, and the 11-bit S_INDEX field indexes
   the IDX data rather than this table, so there is no packing limit to
   run into. */
/*
 * PC_TABLES is a PC product build - pc3host's Linux and Windows tools -
 * which has no board to fit and so no reason to stop where the board
 * does: MMbg, a 5500-line BASIC backgammon program, needs ~155 struct
 * fields and more complex types than a 16-bit type word can number
 * (type.h).  It is NOT BIG_TABLES, which sizes the host GATE builds
 * like the board so that a gate never passes what the board refuses.
 */
#ifdef PC_TABLES
#define MAXSYM			32768
#define NUM_NODES		16384
#define IDX_SIZE		1048576
#define MAXLABEL		8192
#define NUM_STRUCT_FIELD	4096
#define NUM_SWITCH		8192
#define NUM_CONSTANT		8192
#endif
#ifndef MAXSYM
#define MAXSYM			2048
#endif
/* Expression nodes. Currently 16 bytes on a small box will be about 24 once
   we have everything in */
#ifndef NUM_NODES
#define NUM_NODES		512
#endif
/* Number of bytees of index data used for tagging structs, prototypes etc */
#ifndef IDX_SIZE
#define IDX_SIZE		2560
#endif
/* Maximum number of goto labels per function (not switches), 4 bytes each */
#ifndef MAXLABEL
#define MAXLABEL		256
#endif
/* Maximum number of fields per structure, 6 bytes per entry on stack, per
   recursive struct definition.  mmbc puts every BASIC array, string and
   structure into one mm_vars struct, so this is really "how many bulk
   variables may a BASIC program have" - 50 was hit by the firmware's
   own StructTest.bas.  128 costs ~1.5K of cc1 stack per textually
   nested struct DEFINITION, and definitions do not nest in generated
   code. */
#ifndef NUM_STRUCT_FIELD
#define NUM_STRUCT_FIELD	128
#endif
/* Number of switch entries within the current scope. 4 bytes per entry */
#ifndef NUM_SWITCH
#define NUM_SWITCH		128
#endif
/* Number of constants from enum. 4 bytes per entry */
#ifndef NUM_CONSTANT
#define NUM_CONSTANT		50
#endif

#include <stdio.h>

/* Out of alphabetical order deliberately: it defines cval_t, the width
   a constant is carried at, which the headers below use */
#include "target.h"

#include "symbol.h"

#include "body.h"
#include "declaration.h"
#include "enum.h"
#include "error.h"
#include "expression.h"
#include "header.h"
#include "idxdata.h"
#include "initializer.h"
#include "label.h"
#include "lex.h"
#include "primary.h"
#include "storage.h"
#include "stackframe.h"
#include "struct.h"
#include "switch.h"
#include "token.h"
#include "tree.h"
#include "type.h"
#include "type_iterator.h"

extern FILE *debug;

extern unsigned in_sizeof;

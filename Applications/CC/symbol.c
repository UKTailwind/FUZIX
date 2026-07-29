/*
 *	Look up and manage symbols. We have two lists, one global and one
 *	local. To avoid two lists we keep a "last local" and "last global"
 *	pointer. This allows us to keep dumping local names whilst still
 *	being able to defines globals in local contexts.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "compiler.h"

static void symbol_bss(struct symbol *);

struct symbol symtab[MAXSYM];
struct symbol *last_sym = symtab - 1;
struct symbol *local_top = symtab;
/*
 *	Where the current block's symbols start. Everything above this was
 *	declared in this block; everything below is an enclosing block, a
 *	parameter or a global. Redeclaring a name from below is shadowing,
 *	which is legal C - redeclaring one from above is a duplicate.
 *	statement_block() maintains this.
 */
struct symbol *block_base = symtab;

struct symbol *symbol_ref(unsigned type)
{
	return symtab + INFO(type);
}

/* Local symbols have priority in all cases */
/* Find a symbol in the normal name space */
struct symbol *find_symbol(unsigned name, unsigned global)
{
	struct symbol *s = last_sym;
	struct symbol *gmatch = NULL;
	/* Walk backwards so that the first local we find is highest priority
	   by scope */
	while (s >= symtab) {
		if (s->name == name && s->infonext < S_TYPEDEF) {
			if (s->infonext < S_STATIC) {
				if (!global)
					return s;
			} else	/* Still need to look for a local */
				gmatch = s;
		}
		s--;
	}
	return gmatch;
}

struct symbol *find_symbol_by_class(unsigned name, unsigned class)
{
	struct symbol *s = last_sym;
	struct symbol *gmatch = NULL;
	/* Walk backwards so that the first local we find is highest priority
	   by scope */
	while (s >= symtab) {
		if (s->name == name && S_STORAGE(s->infonext) == class) {
			/* S_TAGLOCAL is how a typedef or tag says "block
			   scope"; its storage class cannot, being above
			   S_STATIC. Without this the loop falls into the
			   gmatch branch below, which - because the walk is
			   backwards - keeps overwriting and ends up
			   returning the *outermost* match. A block typedef
			   shadowing a file scope one then had no effect:
			   sizeof(t) gave the outer type. */
			if (s->infonext < S_STATIC ||
			    (s->infonext & S_TAGLOCAL))
				return s;
			else	/* Still need to look for a local */
				gmatch = s;
		}
		s--;
	}
	return gmatch;
}

void pop_local_symbols(struct symbol *top)
{
	struct symbol *s = top + 1;
	while (s <= last_sym) {
		unsigned st = S_STORAGE(s->infonext);
		if (st < S_STATIC) {
			/* Write out any storage if needed */
			symbol_bss(s);
			s->infonext = S_FREE;
			s->name = 0;
		} else if ((st == S_STRUCT || st == S_UNION ||
			    st == S_TYPEDEF) &&
			   (s->infonext & S_TAGLOCAL)) {
			/* A tag or typedef declared inside this block goes
			   with it */
			s->infonext = S_FREE;
			s->name = 0;
		}
		s++;
	}
	local_top = top;
}

struct symbol *mark_local_symbols(void)
{
	return local_top;
}

/* The symbols from 0 to local_top are a mix of kinds but as we have not
   discarded below that point are all full. Between that and last_sym there
   may be holes, above last_sym is free */
struct symbol *alloc_symbol(unsigned name, unsigned local)
{
	struct symbol *s = local_top;
	while (s <= &symtab[MAXSYM]) {
		if (s->infonext == S_FREE) {
			if (local && local_top < s)
				local_top = s;
			if (last_sym < s)
				last_sym = s;
			s->name = name;
			s->data.idx = 0;
			return s;
		}
		s++;
	}
	fatal("too many symbols");
}

/*
 *	Create or update a symbol. Warn about any symbol we are hiding.
 *	A symbol can be setup as C_ANY meaning "we've no idea yet" to hold
 *	the slot. Once the types are found it will get updated with the
 *	types and any checking done.
 */
struct symbol *update_symbol(struct symbol *sym, unsigned name, unsigned storage,
			     unsigned type)
{
	unsigned local = 0;
	unsigned symst;

	if (storage < S_STATIC)
		local = 1;
	if (sym != NULL && sym->type != C_ANY) {
		if (type == C_ANY)
			return sym;
		symst = S_STORAGE(sym->infonext);
		if (symst > S_TYPEDEF)
			error("invalid name");
		else if (symst < S_STATIC || !local) {
			if (sym->type != type) {
				unsigned keep;
				if (IS_ARRAY(type) && IS_ARRAY(sym->type))
					sym->type = array_compatible(type, sym->type);
				else if (type_func_compatible(sym->type, type,
							      &keep))
					/* A prototype and a K&R definition of
					   the same function. Keep whichever
					   states the arguments. */
					sym->type = keep;
				else
					typemismatch();
			}
			if (symst == storage)
				return sym;
			/* extern foo and now found foo */
			if (symst == S_EXTERN && storage == S_EXTDEF) {
				sym->infonext = S_INDEX(sym->infonext);
				sym->infonext |= S_EXTDEF;
				return sym;
			}
			/* foo and now found extern foo */
			if (symst == S_EXTDEF && storage == S_EXTERN)
					return sym;
			error("storage class mismatch");
			return sym;
		}
	}
	/* Insert new symbol */
	if (sym == NULL)
		sym = alloc_symbol(name, local);
	/* Fill in the new or reserved symbol */
	sym->type = type;
	sym->infonext = storage;
	sym->data.idx = 0;
	return sym;
}

/* Update a symbol by name. In the case of things like typedefs we need
   to do an explicit search, otherwise we look for conventional names.

   Not strictly correct at this point - each block is a new context and
   block locals can hide the previous block. TODO sort this once we have
   the chains/hashing in */
struct symbol *update_symbol_by_name(unsigned name, unsigned storage,
			     unsigned type)
{
	struct symbol *sym;
	unsigned global = 0;
	/* If we are updating a static/global symbol then ignore any local names
	   obscuring it. In the reverse case the priority rule will fix it for now
	   FIXME: when we fix the block contexts we have to fix this hack too */
	if (storage >= S_STATIC)
		global = 1;
	if (storage >= S_TYPEDEF)
		sym = find_symbol_by_class(name, storage);
	else
		sym = find_symbol(name, global);
	/* Is it local ? if the one we found is global then we don't update
	   it - we create a local one masking it */
	if (sym && global == 0 && sym->infonext >= S_STATIC)
		sym = NULL;
	/*
	 * A local may not be declared twice in the same block, but it may
	 * perfectly well shadow one from an enclosing block, or a
	 * parameter. Only the first is an error; the second has to become
	 * a new symbol rather than an update of the outer one, or the two
	 * names end up sharing storage.
	 *
	 * Passing NULL is what makes it a new symbol, and it also side
	 * steps update_symbol's storage class check - shadowing a
	 * parameter with an auto is not a "storage class mismatch".
	 */
	if (sym && !global) {
		if (sym > block_base)
			error("duplicate name");
		else
			sym = NULL;
	}
	return update_symbol(sym, name, storage, type);
}

/*
 *	A typedef declared inside a function body is scoped to its block,
 *	exactly as a struct tag is - see update_struct(). Two things follow
 *	and neither is handled by the generic path:
 *
 *	It must be allocated *local* so that local_top moves past it and an
 *	inner block's pop cannot reach back over it, and it must carry
 *	S_TAGLOCAL so pop_local_symbols() frees it at all - S_TYPEDEF sorts
 *	above S_STATIC, which otherwise means "permanent".
 *
 *	And it must never update an outer typedef of the same name. An
 *	inner "typedef char foo;" shadows a file scope "typedef int foo;"
 *	and both have to survive until the block ends, so a match found
 *	outside this block means allocate, not overwrite.
 */
struct symbol *update_typedef(unsigned name, unsigned type)
{
	struct symbol *sym;

	if (!in_funcbody)
		return update_symbol_by_name(name, S_TYPEDEF, type);

	sym = find_symbol_by_class(name, S_TYPEDEF);
	if (sym && sym > block_base && (sym->infonext & S_TAGLOCAL)) {
		error("duplicate name");
		return sym;
	}
	sym = alloc_symbol(name, 1);
	sym->type = type;
	sym->infonext = S_TYPEDEF | S_TAGLOCAL;
	sym->data.idx = 0;
	return sym;
}

/*
 *	Find or insert a function prototype. We keep these in the sym table
 *	as a handy way to get an index for types.
 *
 *	Although it has a cost we really need to fold all the equivalently
 *	typed argument sets into a single instance to save memory.
 *
 *	TODO: we need to hash this, but differently to the usual symbol
 *	indexing as we care about the template pattern most
 */
static struct symbol *do_type_match(unsigned st, unsigned rtype, unsigned *idx)
{
	struct symbol *sym = symtab;
	while(sym <= last_sym) {
		if (S_STORAGE(sym->infonext) == st && sym->type == rtype && sym->data.idx == idx) {
			return sym;
		}
		sym++;
	}
	sym = alloc_symbol(0xFFFF, 0);
	sym->infonext = st;
	sym->data.idx = idx;
	sym->type = rtype;
	return sym;
}

unsigned *sym_find_idx(unsigned storage, unsigned *idx, unsigned len)
{
	struct symbol *sym = symtab;
	unsigned blen = len * sizeof(unsigned);
	while (sym <= last_sym) {
		if (S_STORAGE(sym->infonext) == storage && memcmp(sym->data.idx, idx, blen) == 0)
			return sym->data.idx;
		sym++;
	}
	return idx_copy(idx, len);
}

unsigned func_return(unsigned n)
{
	if (!IS_FUNCTION(n))
		return CINT;
	return symtab[INFO(n)].type;	/* Type of function is its return type */
}

unsigned *func_args(unsigned n)
{
	if (!IS_FUNCTION(n))
		return NULL;
	return symtab[INFO(n)].data.idx;	/* Type of function is its return type */
}

unsigned make_function(unsigned type, unsigned *idx)
{
	/* Can we use the one we found (if we did) */
	struct symbol *sym = do_type_match(S_FUNCDEF, type, idx);
	return C_FUNCTION | ((sym - symtab) << 3);
}

unsigned func_symbol_type(unsigned type, unsigned *idx)
{
	struct symbol *sym;
	idx = sym_find_idx(S_FUNCDEF, idx, *idx + 1);
	sym = do_type_match(S_FUNCDEF, type, idx);
	return C_FUNCTION | ((sym - symtab) << 3);
}

/*
 *	Array type helpers
 */

unsigned array_num_dimensions(unsigned type)
{
	struct symbol *sym = symbol_ref(type);
	return *sym->data.idx;
}

unsigned array_dimension(unsigned type, unsigned depth)
{
	struct symbol *sym = symbol_ref(type);
	return sym->data.idx[depth];
}

unsigned make_array(unsigned type, unsigned *idx)
{
	struct symbol *sym = do_type_match(S_ARRAY, type, idx);
	return C_ARRAY | ((sym - symtab) << 3) | *idx;
}

unsigned array_type(unsigned n)
{
	if (!IS_ARRAY(n))
		return CINT;
	return symtab[INFO(n)].type;	/* Type of array is its element type */
}

/* Make a sized version of the array type given */
unsigned array_with_size(unsigned t, unsigned size)
{
	struct symbol *sym = symbol_ref(t);
	unsigned x[9];	/* Max ptr depth of 8  + size */
	unsigned *idx;

	memcpy(x, sym->data.idx, 9 * sizeof(unsigned));
	x[1] = size;

	/* See if this type exists */
	idx = sym_find_idx(S_ARRAY, x, *x + 1);
	/* Make the new one, or find the reference */
	return make_array(sym->type, idx);
}

/*
 *	Check if two arrays are compatible. If so return the type of the
 *	one with the actual dimensions, if not 0xFFFF
 */
unsigned array_compatible(unsigned t1, unsigned t2)
{
	unsigned *i1 = symtab[INFO(t1)].data.idx;
	unsigned *i2 = symtab[INFO(t2)].data.idx;
	/* Check the depth and dimensions beyond the first */
	if (*i1 != *i2 || (*i1 > 1 && memcmp(i1 + 2, i2 + 2, ((*i1) * sizeof(unsigned)) - 1)))
		typemismatch();
	if (i1[1] == 0)
		return t2;
	return t1;
}

/*
 *	Struct helpers
 */

static struct symbol *find_struct(unsigned name)
{
	struct symbol *sym = last_sym;
	/* Anonymous structs are unique each time */
	if (name == 0)
		return 0;
	/* Backwards, so the innermost declaration of a tag wins - the same
	   rule find_symbol uses for ordinary identifiers. Searching
	   forwards returned the outermost, so a tag redeclared in an inner
	   block could never be seen. */
	while(sym >= symtab) {
		if (sym->name == name) {
			unsigned st = S_STORAGE(sym->infonext);
			if (st == S_STRUCT || st == S_UNION)
				return sym;
		}
		sym--;
	}
	return NULL;
}

/*
 *	Look up or create a struct/union tag.
 *
 *	"defining" says a body follows - "struct T { ... }" rather than a
 *	mention of T. That is the difference between using the tag from
 *	whatever scope declared it and declaring a new one here: an inner
 *	block may define its own T, and it is a distinct type from the
 *	outer one rather than a redefinition of it.
 */
struct symbol *update_struct(unsigned name, unsigned t, unsigned defining)
{
	struct symbol *sym;
	if (t)
		t = S_STRUCT;
	else
		t = S_UNION;
	sym = find_struct(name);
	/*
	 * A body makes this a definition. If the tag we found belongs to
	 * an enclosing scope we want a new one; only a tag declared in
	 * *this* scope is a redefinition, which struct_declaration then
	 * reports.
	 *
	 * Which scope it belongs to cannot be decided by position alone:
	 * a file scope tag sits above symtab while block_base may still be
	 * symtab. So ask the flag first - a tag without S_TAGLOCAL is at
	 * file scope, and redefining it from inside a function always
	 * makes a new one - and only compare positions between two tags
	 * that are both block local, where block_base does mean something.
	 */
	if (sym && defining) {
		if (!(sym->infonext & S_TAGLOCAL)) {
			if (in_funcbody)
				sym = NULL;
		} else if (sym <= block_base)
			sym = NULL;
	}
	if (sym == NULL) {
		/*
		 * A tag declared inside a function body dies with its block.
		 * Allocated as local so that local_top moves past it: that
		 * is what makes the mark an enclosing block took cover it,
		 * and stops an inner block's pop reaching back and freeing
		 * a tag that belongs to the block outside it.
		 */
		sym = alloc_symbol(name, in_funcbody ? 1 : 0);
		sym->infonext = t | (in_funcbody ? S_TAGLOCAL : 0);
		sym->data.idx = NULL;	/* Not yet known */
	} else {
		if (S_STORAGE(sym->infonext) != t)
			error("declared both union and struct");
	}
	return sym;
}

unsigned *struct_find_member(unsigned name, unsigned fname)
{
	struct symbol *s = symtab + INFO(name);
	unsigned *ptr, idx;
	/* May be a known type but not one with fields yet declared */
	if (s->data.idx == NULL)
		return NULL;
	idx = *s->data.idx;	/* Number of fields */
	ptr = s->data.idx + 2;	/* num fields, sizeof */
	while(idx--) {
		/* name, type, offset tuples */
		if (*ptr == fname)
			return ptr;
		ptr += 3;
	}
	return NULL;
}

unsigned type_of_struct(struct symbol *sym)
{
	return C_STRUCT|((sym - symtab) << 3);
}

/*
 *	Generate the BSS at the end (and at scope end for static local)
 */

static void symbol_bss(struct symbol *s)
{
	unsigned st = S_STORAGE(s->infonext);
	if ((PTR(s->type) || !IS_FUNCTION(s->type)) && st != S_EXTERN && st >= S_LSTATIC && st <= S_EXTDEF) {
		if (st == S_EXTDEF)
			header(H_EXPORT, s->name, 0);
		if (!(s->infonext & INITIALIZED)) {
			unsigned n = type_sizeof(s->type);
			unsigned l = s->name;
			if (st == S_LSTATIC)
				l = s->data.offset;
			header(H_BSS, l, target_alignof(s->type, st));
			put_padding_data(n);
			footer(H_BSS, l, 0);
		}
	}
}

void write_bss(void)
{
	struct symbol *s = symtab;

	while(s <= last_sym) {
#ifdef DEBUG
		if (debug)
			fprintf(debug, "sym %x %x %x\n", s->name, s->type, s->infonext);
#endif
		symbol_bss(s);
		s++;
	}
}

/*
 *	Tree operations to build a node tree
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "compiler.h"

static struct node node_table[NUM_NODES];
static struct node *nodes;

struct node *new_node(void)
{
	struct node *n;
	if (nodes == NULL) {
		error("too complex");
		exit(1);
	}
	n = nodes;
	nodes = n->right;
	n->left = n->right = NULL;
	n->value = 0;
	n->flags = 0;
	n->type = 0;
	return n;
}

void free_node(struct node *n)
{
	n->right = nodes;
	nodes = n;
}

void init_nodes(void)
{
	int i;
	struct node *n = node_table;
	for (i = 0; i < NUM_NODES; i++)
		free_node(n++);
}


struct node *tree(unsigned op, struct node *l, struct node *r)
{
	struct node *n = new_node();
	struct node *c;
#ifdef DEBUG
	if (debug) {
		fprintf(debug, "tree %04x [", op);
		if (l)
			fprintf(debug, "%04x ", l->op);
		if (r)
			fprintf(debug, "%04x ", r->op);
		fprintf(debug, "]\n");
	}
#endif
	n->left = l;
	n->right = r;
	n->op = op;
	/* Inherit from left if present, right if not */
	if (l)
		n->type = l->type;
	else if (r)
		n->type = r->type;
	c = constify(n);
	if (c)
		return c;
	return n;
}

struct node *sf_tree(unsigned op, struct node *l, struct node *r)
{
	struct node *n = tree(op, l, r);
	/* A dereference is only a side effect if it might be volatile */
	if (op == T_DEREF) {
		if (voltrack)
			n->flags |= SIDEEFFECT;
	} else
		n->flags |= SIDEEFFECT;
	return n;
}

struct node *make_constant(cval_t value, unsigned type)
{
	struct node *n = new_node();
	n->op = T_CONSTANT;
	n->value = value;
	n->type = type;
	return n;
}

struct node *make_symbol(struct symbol *s)
{
	struct node *n = new_node();

	n->value = s->data.offset;
	n->val2 = 0;

	switch(S_STORAGE(s->infonext)) {
	case S_LSTATIC:
		n->op = T_LABEL;
		n->val2 = s->data.offset;
		n->value = 0;
		break;
	case S_AUTO:
		n->op = T_LOCAL;
		break;
	case S_ARGUMENT:
		n->op = T_ARGUMENT;
		break;
	case S_REGISTER:
		n->op = T_REG;
		break;
	default:
		n->value = 0;
		n->op = T_NAME;
	}
	n->snum = s->name;
	n->flags = LVAL;
	n->type = s->type;
	/* Rewrite implicit pointer forms */
#if 0
	if (!PTR(s->type)) {
		if (IS_FUNCTION(s->type) || IS_ARRAY(s->type))
			n->type++;
	}
#endif
#ifdef DEBUG
	if (debug)
		fprintf(debug, "name %04x type %04x\n", s->name, s->type);
#endif		
	return n;
}

struct node *make_label(unsigned label)
{
	struct node *n = new_node();
	n->op = T_LABEL;
	n->val2 = label;
	n->value = 0;
	n->flags = 0;
#ifdef TARGET_CHAR_UNSIGNED
	n->type = PTRTO|UCHAR;
#else
	n->type = PTRTO|CHAR;
#endif
	return n;
}

unsigned is_constant(struct node *n)
{
	return (n->op == T_CONSTANT) ? 1 : 0;
}

/* Constant or name in linker constant form */
unsigned is_constname(struct node *n)
{
	/* The address of a symbol is a link time constant so can go in initializers */
	/* A dereferenced form however is not */
	/* Locals are not a fixed address */
	if (n->op == T_NAME && (n->flags & LVAL))
		return 1;
	/* A label is also fixed by the linker so constant, thus we can fix
	   up stuff like "hello" + 3 */
	if (n->op == T_LABEL)
		return 1;
	return is_constant(n);
}

unsigned is_constant_zero(struct node *n)
{
	if (is_constant(n))
		return !n->value;
	return 0;
}


#define IS_NAME(x)		((x) >= T_NAME && (x) <= T_ARGUMENT)

static void nameref(struct node *n)
{
	if (is_constant(n->right) && IS_NAME(n->left->op)) {
		cval_t value = n->left->value + n->right->value;
		struct node *l = n->left;
		memcpy(n , n->right, sizeof(*n));
		free_node(n->right);
		n->value = value;
		n->left = NULL;
		n->right = NULL;
		free_node(l);
	}
}

static unsigned transitive(unsigned op)
{
	if (op == T_AND || op == T_OR || op == T_HAT ||
	    op == T_PLUS || op == T_STAR)
		return 1;
	return 0;
}

struct node *make_rval(struct node *n)
{
	unsigned nt = n->type;
	if (n->flags & LVAL) {
		if (IS_ARRAY(nt)) {
			if (PTR(nt) == array_num_dimensions(nt)) {
				n->flags &= ~LVAL;
				return n;
			}
#if 0
			n = sf_tree(T_DEREF, NULL, n);
			/* Decay to base type of array */
			if (!PTR(nt))
				n->type = type_canonical(nt);
#endif
			return n;
		} else if (IS_FUNCTION(nt) && !PTR(nt)) {
			n->flags &= ~LVAL;
		} else
			return sf_tree(T_DEREF, NULL, n);
	}
	return n;
}

struct node *make_noreturn(struct node *n)
{
	n->flags |= NORETURN;
	return n;
}

struct node *make_cast(struct node *n, unsigned t)
{
	unsigned nt = type_canonical(n->type);
	n->type = nt;
	if (nt != t) {
		struct node *c, *f;
		/*
		 * Not tree(), because tree() folds the node before the type
		 * can be set on it and a cast is the one operation whose
		 * whole meaning is its result type. Folding saw the source
		 * type as the destination, so "(float)1.25" collapsed to
		 * the double 1.25 with the cast thrown away, and the four
		 * byte store that followed wrote the bottom half of it.
		 */
		c = new_node();
		c->op = T_CAST;
		c->right = n;
		c->type = t;
		f = constify(c);
		return f ? f : c;
	}
	return n;
}

void free_tree(struct node *n)
{
	if (n->left)
		free_tree(n->left);
	if (n->right)
		free_tree(n->right);
	free_node(n);
}

static void write_subtree(struct node *n)
{
	out_block(n, sizeof(struct node));
	if (n->left)
		write_subtree(n->left);
	if (n->right)
		write_subtree(n->right);
	free_node(n);
}

void write_tree(struct node *n)
{
	out_block("%^", 2);
	write_subtree(n);
}

void write_null_tree(void)
{
	write_tree(tree(T_NULL, NULL, NULL));
}

void write_logic_tree(struct node *n, unsigned truth)
{
	if (truth == -1)
		write_tree(n);
	else
		free_tree(n);
}

/*
 *	Trees with type rule.
 */

/*
 *	A bool tree is special, we don't optimize the T_BOOL at the
 *	top level or we'll just (wrongly) remove it.
 */
struct node *bool_tree(struct node *n, unsigned flags)
{
	struct node *b;
	if (n->op == T_BOOL)
		return n;
	if (flags & NEEDCC) {
		/* The subtree should already be optimized */
		/* Make the new node */
		b = new_node();
		b->op = T_BOOL;
		b->type = CINT;
		b->right = n;
		b->flags |= flags;
		return b;
	} else {
		n = tree(T_BOOL, NULL, n);
		n->type = CINT;
	}
	return n;
}

/* Calculate arithmetic promotion */
static unsigned arith_pro(unsigned lt, unsigned rt)
{
	if (PTR(lt))
		lt = UINT;
	if (PTR(rt))
		rt = UINT;
	/* Our types are ordered for a reason */
	/* Does want review versus standard TODO */
	if (rt > lt)
		lt = rt;
	if (lt < CINT)
		lt = CINT;
	if (lt < FLOAT) {
		if((rt | lt) & UNSIGNED)
			lt |= UNSIGNED;
	}
	return lt;
}

struct node *arith_pro_tree(unsigned op, struct node *l,
				  struct node *r)
{
	/* We know both sides are arithmetic */
	unsigned lt = type_canonical(l->type);
	unsigned rt = type_canonical(r->type);
	struct node *n;

	lt = arith_pro(lt, rt);

	if (l->type != lt)
		l = make_cast(l, lt);
	if (r->type != lt)
		r = make_cast(r, lt);
	n = tree(op, l, r);
	n->type = lt;
	return n;
}

/* Two argument arithmetic including float - multiply and divide, plus
   some subsets of more general operations below */
struct node *arith_tree(unsigned op, struct node *l, struct node *r)
{
	if (!IS_ARITH(l->type) || !IS_ARITH(r->type))
		badtype();
	return arith_pro_tree(op, l, r);
}

/* Two argument integer or bit pattern
   << >> & | ^ */
struct node *intarith_tree(unsigned op, struct node *l, struct node *r)
{
	unsigned lt = l->type;
	unsigned rt = r->type;
	if (!IS_INTARITH(lt) || !IS_INTARITH(rt))
		badtype();
	if (op == T_LTLT || op == T_GTGT) {
		struct node *n;
		lt = arith_pro(lt, lt);
		if (lt != rt)
			l = make_cast(l, lt);
		n = tree(op, l, make_cast(r, CINT));
		n->type = lt;
		return n;
	} else
		return arith_pro_tree(op, l, r);
}

/* Two argument ordered compare - allows pointers
		< > <= >= == != */
struct node *ordercomp_tree(unsigned op, struct node *l, struct node *r)
{
	struct node *n;
	if (type_pointermatch(l, r))
		n = tree(op, l, r);
	else
		n = arith_tree(op, l, r);
	return bool_tree(n, 0);
}

struct node *assign_tree(struct node *l, struct node *r)
{
	unsigned lt = type_canonical(l->type);
	unsigned rt = type_canonical(r->type);

	if (lt == rt)
		return sf_tree(T_EQ, l, r);
	if (PTR(lt)) {
		type_pointermatch(l, r);
		return sf_tree(T_EQ, l, r);
	} else if (PTR(rt))
		typemismatch();
	else if (!IS_ARITH(lt) || !IS_ARITH(rt)) {
		invalidtype();
	}
	return sf_tree(T_EQ, l, make_cast(r, l->type));
}

/* && || */
struct node *logic_tree(unsigned op, struct node *l, struct node *r)
{
	unsigned lt = l->type;
	unsigned rt = r->type;
	struct node *n;

	if (!PTR(lt) && !IS_ARITH(lt))
		badtype();
	if (!PTR(rt) && !IS_ARITH(rt))
		badtype();
	n = tree(op, bool_tree(l, 0), bool_tree(r, 0));
	n->type = CINT;
	return n;
}

/* Constant conversion

   Needs review and to be a bit more precise
 */
cval_t trim_constant(unsigned t, cval_t value, unsigned warn)
{
	cval_t ov = value;
	cval_t mask, sbit;

	/*
	 * Match on the base type with the sign bit already masked off, so
	 * the labels must be the signed forms. The cases here used to be
	 * UCHAR/USHORT/ULONG (0x08/0x18/0x28), which "t & 0xF0" can never
	 * produce - so nothing was ever trimmed and a narrowing cast of a
	 * constant kept its full value: "(int)(signed char)200" folded to
	 * 200 instead of -56.
	 */
	switch (t & 0xF0) {
	case CCHAR:
		mask = TARGET_CHAR_MASK;
		break;
	case CSHORT:
		mask = TARGET_SHORT_MASK;
		break;
	case CLONG:
		mask = TARGET_LONG_MASK;
		break;
	default:
		return value;
	}
	sbit = (mask >> 1) + 1;

	value &= mask;
	/* Masking alone is not enough for a signed type: the value has to
	   be sign extended out of the target's width, or a negative result
	   comes back as a large positive one. */
	if (!(t & UNSIGNED) && (value & sbit))
		value |= ~mask;

	if (warn && ov != value)
		warning("out of range");
	return value;
}

static struct node *replace_constant(struct node *n, unsigned t, cval_t value);

/*
 *	Constant folding where floating point is involved.
 *
 *	A floating constant is carried as its IEEE754 bit pattern, so it
 *	has to be unpacked before anything can be done with it and packed
 *	again afterwards. Only a static initialiser really needs this -
 *	the code generator has opcodes for the rest - but an initialiser
 *	has nowhere to put an instruction, so "float f = 1.25;" has to be
 *	converted here or it stores the bottom half of a double.
 *
 *	This uses the host's own floating point rather than assembling the
 *	bits by hand. FCC avoids that in general so it can bootstrap from
 *	an integer-only compiler, but this target has double in the
 *	compiler it is built with and in the one it now generates, and
 *	using the same arithmetic the interpreter uses is one fewer place
 *	for the two to disagree.
 */

#ifdef TARGET_HAS_DOUBLE

static double fp_unpack(cval_t v, unsigned t)
{
	if (type_sizeof(t) == 8) {
		union { cval_t b; double d; } u;
		u.b = v;
		return u.d;
	} else {
		union { uint32_t b; float f; } u;
		u.b = (uint32_t)v;
		return (double)u.f;
	}
}

static cval_t fp_pack(double d, unsigned t)
{
	if (type_sizeof(t) == 8) {
		union { cval_t b; double d; } u;
		u.d = d;
		return u.b;
	} else {
		union { uint32_t b; float f; } u;
		u.f = (float)d;
		return (cval_t)u.b;
	}
}

static struct node *fold_float_unary(struct node *n, struct node *r,
				     unsigned op)
{
	unsigned rt = r->type;
	unsigned lt = n->type;
	double d;

	switch (op) {
	case T_NEGATE:
		/* Just the sign bit, so this one needs no arithmetic at
		   all - but it has to be the right sign bit */
		r->value ^= ((cval_t)1) << (8 * type_sizeof(rt) - 1);
		return r;
	case T_BANG:
		return replace_constant(n, lt, fp_unpack(r->value, rt) == 0.0);
	case T_BOOL:
		if (n->flags & NEEDCC)
			return NULL;
		return replace_constant(n, lt, fp_unpack(r->value, rt) != 0.0);
	case T_CAST:
		if (IS_FLOATING(rt) && IS_FLOATING(lt)) {
			if (type_sizeof(rt) == type_sizeof(lt))
				return replace_constant(n, lt, r->value);
			return replace_constant(n, lt,
				fp_pack(fp_unpack(r->value, rt), lt));
		}
		if (IS_FLOATING(lt)) {		/* integer -> floating */
			if (rt & UNSIGNED)
				d = (double)(cval_t)r->value;
			else
				d = (double)(long long)r->value;
			return replace_constant(n, lt, fp_pack(d, lt));
		}
		if (IS_FLOATING(rt) && IS_INTARITH(lt)) {
			d = fp_unpack(r->value, rt);
			if (lt & UNSIGNED)
				return replace_constant(n, lt, (cval_t)d);
			return replace_constant(n, lt,
					(cval_t)(long long)d);
		}
		/* To a pointer, which is not something to fold */
		return NULL;
	default:
		return NULL;
	}
}

#else

/*
 *	Without double in the compiler, only the sign flip is safe to do
 *	on the bits, and that is all the front end needs: it tokenises a
 *	negative constant as a negate of a positive one.
 */
static struct node *fold_float_unary(struct node *n, struct node *r,
				     unsigned op)
{
	if (op == T_NEGATE) {
		r->value ^= ((cval_t)1) << (8 * type_sizeof(r->type) - 1);
		return r;
	}
	return NULL;
}

#endif

static struct node *replace_constant(struct node *n, unsigned t, cval_t value)
{
	if (n->left)
		free_node(n->left);
	if (n->right)
		free_node(n->right);
	free_node(n);
	value = trim_constant(t, value, 0);
	return make_constant(value, t);
}

static unsigned is_name(unsigned n)
{
	if (n >= T_NAME && n <= T_ARGUMENT)
		return 1;
	return 0;
}

/* Check of the tree has side effects */
static unsigned tree_impure(struct node *n)
{
	if (n->right) {
		if (tree_impure(n->right))
			return 1;
	}
	if (n->left) {
		if (tree_impure(n->left))
			return 1;
	}
	return n->flags & SIDEEFFECT;
}

/*
 *	TODO:
 *	We need a sensible way of not rewalking the same trees
 *
 *	Walk down a tree and attempt to reduce it to the simplest
 *	form we can manage. At the moment we do this repeatedly as
 *	we build the tree but that needs rethinking. On the other hand
 *	we don't want to do it at the end or we risk running out of nodes
 *	as we remove a lot of nodes as we go.
 */

struct node *constify(struct node *n)
{
	struct node *l = n->left;
	struct node *r = n->right;
	unsigned op = n->op;

	/* Remember if we are a node or child of a node that has a side
	   effect. This determines what can be eliminated */

	/* Casting of constant form objects */
	/* We block casting of structures and arrays to each other higher up
	   so all we have to worry about is truncating constants and just
	   relabelling the type on a name or label */
	if (op == T_CAST) {
		if (r->op == T_CONSTANT) {
			/*
			 * Relabelling the type is the whole conversion for
			 * an integer, whose value is a number in both. It
			 * is not for floating point, where the value is a
			 * bit pattern that means something different under
			 * the new type: (float)1.25 relabelled is the
			 * bottom half of the double, which is zero.
			 */
			if (IS_FLOATING(n->type) || IS_FLOATING(r->type))
				return fold_float_unary(n, r, op);
			return replace_constant(n, n->type, r->value);
		}
		if (r->op == T_NAME || r->op == T_LABEL) {
			r->type = n->type;
			free_node(n);
			return r;
		}
	}
        /* Deal with x + y + z where it's not all constant but we can
	   combine the constant part. This gets generated by struct
	   references and the like so needs to be handled */
	if (op == T_PLUS && l->op == T_PLUS && l->right->op == T_CONSTANT && r->op == T_CONSTANT) {
		r->value += l->right->value;
		n->left = l->left;
		free_node(l->right);
		free_node(l);
		l = n->left;
	}
	/* Remove multiply by 1 or 0 */
	/* TODO: We can do the same for floats but a float 0 isn't necessarily 0 */
	if (op == T_STAR && r->op == T_CONSTANT && IS_INTORPTR(r->type)) {
		if (r->value == 1) {
			free_node(r);
			free_node(n);
			return l;
		}
		/* We can only do this if n and l have no side effects */
		if (r->value == 0 && !tree_impure(l)) {
			l = make_constant(0, n->type);
			free_tree(n);
			return l;
		}
	}
	/* Divide by 1 */
	if (op == T_SLASH && r->op == T_CONSTANT && IS_INTORPTR(r->type)) {
		if (r->value == 1) {
			free_node(n);
			free_tree(r);
			return l;
		}
	}
	/* Unsigned ops that resolve to never true/false */
	if (r && r->op == T_CONSTANT && r->value == 0 && (n->type & UNSIGNED)) {
		if (op == T_LT && !tree_impure(l)) {
			free_tree(l);
			free_node(r);
			free_node(n);
			warning("always false");
			return bool_tree(make_constant(0, CINT), 0);
		}
		if (op == T_GTEQ && !tree_impure(l)) {
			free_tree(l);
			free_node(r);
			free_node(n);
			warning("always true");
			return bool_tree(make_constant(1, CINT), 0);
		}
	}
	/* The logic ops are special as they permit shortcuts and need to be
	   dealt with l->r */
	if (op == T_ANDAND || op == T_OROR) {
		l = constify(l);
		if (l == NULL || !IS_INTARITH(l->type) || (l->flags & LVAL))
			return NULL;
		if (op == T_OROR) {
			free_tree(l);
			free_node(n);
			if (l->value) {
				free_tree(r);
				return bool_tree(make_constant(1, CINT), 0);
			} else {
				return bool_tree(r, 0);
			}
		}
		if (op == T_ANDAND) {
			free_tree(l);
			free_node(n);
			if (!l->value) {
				free_tree(r);
				return bool_tree(make_constant(0, CINT), 0);
			} else {
				return bool_tree(r, 0);
			}
		}
		/* TODO: We don't deal with the X && 0 case - we need an
		   op for evaluate, throw away result and return 0. Maybe
		   we can build a tree of expr,0 ?, ditto || 1 */
		return NULL;
	}
	if (r) {
		r = constify(r);
		if (r == NULL)
			return NULL;
		n->right = r;
	}
	if (l) {
		unsigned lt = l->type;
		cval_t value = l->value;

		/* Lval names are constant but a maths operation on two name lval is not */
		if (is_name(l->op) || is_name(r->op)) {
			if (op != T_PLUS && op != T_MINUS)
				return NULL;
			/* Special case for name + const */
			if (is_name(l->op)) {
				if (is_name(r->op))
					return NULL;
				if (op == T_PLUS)
					l->value += r->value;
				else
					l->value -= r->value;
				free_node(r);
				free_node(n);
				return l;
			}
			r->value += l->value;
			free_node(l);
			free_node(n);
			return r;
		}
		/* This works for FP for all the wrong reasons - FIXME */
		if ((op == T_PLUS || op == T_MINUS) && r->type == T_CONSTANT && r->value == 0) {
			free_node(n);
			free_node(r);
			return l;
		}
		if (l) {
			l = constify(l);
			if (l == NULL)
				return NULL;
			n->left = l;
		}
		/* Only do constant work with simple types */
		if (!IS_INTORPTR(lt))
			return NULL;
		if (l->flags & LVAL)
			return NULL;

		switch(op) {
		case T_PLUS:
			value += r->value;
			break;
		case T_MINUS:
			value -= r->value;
			break;
		case T_STAR:
			value *= r->value;
			break;
		case T_SLASH:
			/* Zero may cause an exception which may be what
			   the programmer wanted so don't optimize it out */
			if (r->value == 0) {
				divzero();
				return NULL;
			} else if (l->type & UNSIGNED)
				value /= r->value;
			else
				value = (signed long)value / r->value;
			break;
		case T_PERCENT:
			if (r->value == 0) {
				divzero();
				return NULL;
			} else if (l->type & UNSIGNED)
				value %= r->value;
			else
				value = (signed long)value % r->value;
			break;
		case T_ANDAND:
			value = value && r->value;
			break;
		case T_OROR:
			value = value || r->value;
			break;
		case T_AND:
			value &= r->value;
			break;
		case T_OR:
			value |= r->value;
			break;
		case T_HAT:
			value ^= r->value;
			break;
		case T_LTLT:
			value <<= r->value;
			break;
		case T_GTGT:
			if (l->type & UNSIGNED)
				value >>= r->value;
			else
				value = ((signed long)value) >> r->value;
			break;
		case T_LT:
			if (l->type & UNSIGNED)
				value = value < r->value;
			else
				value = (signed long)value < (signed long )r->value;
			break;
		case T_LTEQ:
			if (l->type & UNSIGNED)
				value = value <= r->value;
			else
				value = (signed long)value <= (signed long )r->value;
			break;
		case T_GT:
			if (l->type & UNSIGNED)
				value = value < r->value;
			else
				value = (signed long)value < (signed long )r->value;
			break;
		case T_GTEQ:
			if (l->type & UNSIGNED)
				value = value < r->value;
			else
				value = (signed long)value < (signed long )r->value;
			break;
		default:
			return NULL;
		}
		return replace_constant(n, lt, value);
	}
	if (r) {
		/* Uni-ops */
		unsigned rt = r->type;
		cval_t value = r->value;

		if (r->flags & LVAL)
			return NULL;

		/*
		 * A unary operation on a constant with floating point on
		 * either side. The value here is an IEEE754 bit pattern,
		 * not a number, so none of the integer folding below applies
		 * to it.
		 *
		 * This used to be one line - flip bit 31 and return - which
		 * was right only for negating a float. It ran for every
		 * unary operator and both widths, so negating a double
		 * flipped a bit in the middle of its mantissa, and casting
		 * a floating constant to anything returned it unconverted
		 * with a mantissa bit flipped for good measure.
		 */
		if (r->op == T_CONSTANT && (IS_FLOATING(rt) ||
					    IS_FLOATING(n->type)))
			return fold_float_unary(n, r, op);

		if (!IS_INTORPTR(rt))
			return NULL;

		switch(op) {
		case T_NEGATE:
			/* This also cleans up any negative constants that were tokenized
			   as T_NEGATE, T_CONST <n> */
			value =  -value;
			break;
		case T_TILDE:
			value = ~value;
			break;
		case T_BANG:
			value = !value;
			break;
		/* Rewriting bool may lose condition code setting, but we can
		   fix that up at the end */
		case T_BOOL:
			if (n->flags & NEEDCC)
				return NULL;
			/* Fall through */
		case T_CAST:
			/* We are working with integer constant types so this is ok */
			return replace_constant(n, n->type, value);
		default:
			return NULL;
		}
		return replace_constant(n, rt, value);
	}
	/* Terminal node.. are we const ?? */
	if (is_constname(n))
		return n;
	return NULL;
}

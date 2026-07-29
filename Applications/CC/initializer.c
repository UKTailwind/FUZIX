#include "compiler.h"

/*
 *	An object on the stack is initialized by generating assignments,
 *	a static or global one by generating a stream of typed data. Both
 *	walk the same structure, so every function here carries a byte
 *	offset into the object being initialized: the static side ignores
 *	it and just writes the next thing, the auto side needs it to know
 *	which part of the frame slot to store into.
 *
 *	An automatic aggregate occupies a contiguous run of the frame, so
 *	the element at byte offset "off" is simply the symbol's own local
 *	node with that much added to its frame offset.
 */

static unsigned is_auto(unsigned storage)
{
    return storage == S_AUTO || storage == S_REGISTER;
}

static struct node *auto_at(struct symbol *sym, unsigned type, unsigned off)
{
    struct node *n = make_symbol(sym);
    n->value += off;
    n->type = type;
    n->flags = LVAL;
    return n;
}

static void auto_store(struct symbol *sym, unsigned type, unsigned off,
                       struct node *v)
{
    write_tree(tree(T_EQ, auto_at(sym, type, off), v));
}

/*
 *	C requires everything an initializer does not mention to be zero,
 *	so the slack has to be written out rather than left as whatever
 *	the frame happened to hold. Widest aligned store each time, which
 *	keeps "struct { char c; int i; } s = { 1 };" down to a couple of
 *	instructions.
 */
static void auto_pad(struct symbol *sym, unsigned off, unsigned size)
{
    while (size) {
        unsigned t, s;
        if (size >= 4 && (off & 3) == 0) {
            t = CINT;
            s = 4;
        } else if (size >= 2 && (off & 1) == 0) {
            t = CSHORT;
            s = 2;
        } else {
            t = CCHAR;
            s = 1;
        }
        auto_store(sym, t, off, make_constant(0, t));
        off += s;
        size -= s;
    }
}

/*
 *	Pad the rest of an object, whichever kind of storage it has.
 */
static void ini_pad(struct symbol *sym, unsigned storage, unsigned off,
                    unsigned size)
{
    if (!size)
        return;
    if (is_auto(storage))
        auto_pad(sym, off, size);
    else
        put_padding_data(size);
}

/*
 *	Write a single initialization element to the stream. For auto variables
 *	we generate the assignment tree, for static or globals we generate a
 *	stream of data with types for the backend.
 */
static void ini_single(struct symbol *sym, unsigned type, unsigned storage,
                       unsigned off)
{
    struct node *n = expression_tree(0);
    n = typeconv(n, type, 1);
    if (is_auto(storage)) {
        auto_store(sym, type, off, n);
    } else {
        put_typed_data(n);
        free_tree(n);
    }
}

/* C99 permits trailing comma and ellipsis */
/* Strictly {} is not permitted - there must be at least one value */

static unsigned ini_string(unsigned n)
{
        /* This one is weird because the string is not literal */
        if (n)
            n = copy_string(0, n, 1, 0);
        else
            n = copy_string(0, TARGET_MAX_PTR, 0, 0);
        return n;
}

/*
 *	Braces may be elided from the initializer of a sub-aggregate:
 *
 *	    struct { int c[4]; int b, e, k; } t[] = { 1,2,3,4, 5,6,7 };
 *
 *	is C89 (6.5.7) and is how the J interpreter snippet in c-testsuite
 *	00205 is written. When the brace is absent this level consumes as
 *	many items as it needs from the list its parent is already reading,
 *	and must not look for a closing brace either.
 *
 *	The other half of it is the comma. An elided group stops as soon as
 *	its quota is filled and leaves the separator alone, because that
 *	comma belongs to the parent's list - eating it would make the parent
 *	think its own list had ended.
 */
static unsigned ini_braced(void)
{
    return match(T_LCURLY);
}

/*
 *	Array bottom level initializer: repeated runs of the same type
 *
 *	TODO: In theory we could have a platform that needs padding
 *	and we don't deal with that aspect of alignment yet
 */
static unsigned ini_group(struct symbol *sym, unsigned type, unsigned n,
                          unsigned storage, unsigned off)
{
    unsigned sized = n;
    unsigned string = 0;
    unsigned count = 0;
    unsigned esize = type_sizeof(type);
    unsigned braced;
    /* C has a funky special case rule that you can write
       char x[16] = "foo"; which creates a copy of the string in that
       array not a literal reference. It's also got a second funky special case
       rule that you can write { "string" }. */

    if ((type_canonical(type) & ~UNSIGNED) == CCHAR)
        string = 1;

    if (token == T_STRING) {
        if (!string)
            typemismatch();
        if (is_auto(storage)) {
            /* Would have to be copied into the frame a byte at a
               time; the data stream form below only works for a
               static object. */
            error("auto string initializer");
            return n;
        }
        return ini_string(n);
    }
    /* Braces may be elided from an inner initializer - C89 6.5.7 - in
       which case this level takes what it needs from the list its
       parent is already reading. See ini_braced(). */
    braced = ini_braced();
    if (!sized)
        n = TARGET_MAX_PTR;
    while(n && token != T_RCURLY) {
        /* Deal with the second string special case, gotta love C some days */
        if (token == T_STRING && string) {
            if (is_auto(storage)) {
                error("auto string initializer");
                return count;
            }
            n = ini_string(sized);
            if (braced)
                require(T_RCURLY);
            return n;
        }
        string = 0;	/* Only valid first */
        if (token == T_ELLIPSIS)
            break;
        n--;
        initializers(sym, type, storage, off + count * esize);
        count++;
        if (!n && !braced)
            break;	/* Quota met - the comma belongs to the parent */
        if (!match(T_COMMA))
            break;
    }
    if (n && sized)
        ini_pad(sym, storage, off + count * esize, esize * n);
    /* Catches any excess elements */
    if (braced)
        require(T_RCURLY);
    return count;
}

/*
 *	Struct and union initializer
 *
 *	This is similar to an array but each element has its own expected
 *	type, and some elements may themselves be structures or arrays. It's
 *	mostly recursion.
 *
 *	Remaining space in the object is padded.
 *
 *	Automatic objects work here too now: "pos" is the offset within
 *	this struct and "off" where the struct itself starts, so a field
 *	assignment lands at off + pos exactly as the data stream lands at
 *	pos.
 */
static void ini_struct(struct symbol *psym, unsigned type, unsigned storage,
                       unsigned off)
{
    struct symbol *sym = symbol_ref(type);
    unsigned *p = sym->data.idx;
    unsigned n = *p;
    unsigned s = p[1];	/* Size of object (needed for union) */
    unsigned pos = 0;
    unsigned braced;

    p += 2;
    /* We only initialize the first object */
    if (S_STORAGE(sym->infonext) == S_UNION)
        n = 1;
    braced = ini_braced();
    while(n-- && token != T_RCURLY) {
        /* Name, type, offset tuples */
        type = p[1];

        /* Align */
        if (pos != p[2]) {
            ini_pad(psym, storage, off + pos, p[2] - pos);
            pos = p[2];
        }
        /* Write out field */
        initializers(psym, type, storage, off + pos);
        pos += type_sizeof(type);

        /* Next field */
        p += 3;

        if (!n && !braced)
            break;	/* Quota met - the comma belongs to the parent */
        if (!match(T_COMMA))
            break;
    }
    if (braced) {
        if (n == -1 && token != T_RCURLY)
            error("too many initializers");
        require(T_RCURLY);
    }
    /* For a union zerofill the slack if other elements are bigger */
    /* For a struct fill from the offset of the next field to the size of
       the base object */
    if (pos != s)
        ini_pad(psym, storage, off + pos, s - pos);	/* Fill remaining space */
}

/*
 *	Array initializer.
 *
 *	We recursively call down through the layers until we hit the bottom
 *	layer of the array which should be a series of values in the type
 *	of the array. The base value may be a structure.
 */
static void ini_array(struct symbol *sym, unsigned type, unsigned depth,
                      unsigned storage, unsigned off)
{
    unsigned n = array_dimension(type, depth);
    unsigned sized = n;
    unsigned count = 0;

    if (depth < array_num_dimensions(type)) {
        unsigned esize;
        unsigned braced;
        type = type_deref(type);
        esize = type_sizeof(type);
        braced = ini_braced();
        if (n == 0)
            n = TARGET_MAX_PTR;
        while(n--) {
            ini_array(sym, type, depth + 1, storage, off + count * esize);
            count++;
            if (!n && !braced)
                break;	/* Quota met - the comma belongs to the parent */
            /* Trailing comma is allowed so eat it before checking n */
            if (match(T_COMMA) && n)
                continue;
            break;
        }
        if (array_dimension(type, 1) == 0)
            sym->type = array_with_size(type, count);
        /* Pad the remaining pieces. n is what the loop above left,
           which is exactly the number still to fill; an unsized array
           has nothing to pad because it is only as long as its
           initializer. */
        if (sized)
            ini_pad(sym, storage, off + count * esize, esize * n);
        if (braced)
            require(T_RCURLY);
    } else {
        n = ini_group(sym, type_deref(type), n, storage, off);
        if (array_dimension(type, 1) == 0)
            sym->type = array_with_size(type, n);
    }
}

/*
 *	Initialize an object.
 */
void initializers(struct symbol *sym, unsigned type, unsigned storage,
                  unsigned off)
{
    /* A pointer to an array - "char (*p)[4]" - is a pointer object and
       initialises like any other scalar. Testing !IS_ARRAY refused it,
       because such a type is an array type with one more indirection
       than it has dimensions. */
    if (type_is_pointer_object(type)) {
        ini_single(sym, type, storage, off);
        return;
    }
    if (IS_ARITH(type)) {
        ini_single(sym, type, storage, off);
        return;
    }
    if (storage == S_EXTERN) {
        error("cannot initialize external");
        return;
    }
    if (IS_FUNCTION(type))
        error("init function");	/* Shouldn't get here, we don't use "=" for
                                   function forms even if it would be more
                                   logical than the C syntax */
    else if (IS_ARRAY(type))
        ini_array(sym, type, 1, storage, off);
    else if (IS_STRUCT(type))
        ini_struct(sym, type, storage, off);
    else
        error("cannot initialize this type");
}

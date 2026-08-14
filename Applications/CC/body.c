#include <stdio.h>
#include "compiler.h"

void statement_block(unsigned brack);
static void statement(void);

static unsigned next_tag;
static unsigned func_tag;
static unsigned break_tag;
static unsigned cont_tag;
static unsigned switch_tag;
static unsigned switch_count;
static unsigned switch_type;
static unsigned switch_default;
static unsigned func_type;

unsigned func_flags;
/* Nonzero while a function body is being parsed. See function_body. */
unsigned in_funcbody;

/* C keyword statements */

static void if_statement(void)
{
	struct node *n;
	unsigned tag = next_tag++;
	unsigned t;

	next_token();
	n = logic_expression(&t);

	header(H_IF, tag, t);
	write_logic_tree(n, t);
	statement_block(0);
	if (token == T_ELSE) {
		next_token();
		header(H_ELSE, tag, t);
		statement_block(0);
		footer(H_IF, tag, 1);
	} else
		footer(H_IF, tag, 0);
}

static void while_statement(void)
{
	unsigned oldbrk = break_tag;
	unsigned oldcont = cont_tag;
	struct node *n;
	unsigned t;

	break_tag = next_tag++;
	cont_tag = break_tag;

	next_token();
	n = logic_expression(&t);

	header(H_WHILE, cont_tag, t);
	write_logic_tree(n, t);
	statement_block(0);
	footer(H_WHILE, cont_tag, t);

	break_tag = oldbrk;
	cont_tag = oldcont;
}

static void do_statement(void)
{
	struct node *n;
	unsigned oldbrk = break_tag;
	unsigned oldcont = cont_tag;
	unsigned t;

	break_tag = next_tag++;
	cont_tag = break_tag;

	next_token();
	header(H_DO, cont_tag, 0);
	statement_block(0);
	require(T_WHILE);
	n = logic_expression(&t);
	header(H_DOWHILE, cont_tag, t);
	require(T_SEMICOLON);
	write_logic_tree(n, t);
	footer(H_DOWHILE, cont_tag, t);

	break_tag = oldbrk;
	cont_tag = oldcont;
}

/* TODO: optimize the cases where the for loop condition is 0 or 1 */
static void for_statement(void)
{
	unsigned oldbrk = break_tag;
	unsigned oldcont = cont_tag;

	break_tag = next_tag++;
	cont_tag = break_tag;

	next_token();
	header(H_FOR, cont_tag, break_tag);
	require(T_LPAREN);
	expression_or_null(0, NORETURN);
	require(T_SEMICOLON);
	expression_or_null(1, CCONLY);
	require(T_SEMICOLON);
	expression_or_null(0, NORETURN);
	require(T_RPAREN);
	statement_block(0);
	footer(H_FOR, cont_tag, break_tag);

	break_tag = oldbrk;
	cont_tag = oldcont;
}

/*
 *	"return expr;" from a function returning a struct or union.
 *
 *	The caller passed the address of space for the result as a hidden
 *	first argument, so this is a block copy into it that then leaves
 *	that same address behind as the return value - which is exactly
 *	what T_EQ on an aggregate already generates.
 *
 *	The destination is the *contents* of argument zero, so it is a
 *	load, not the argument's own address. The source stays an address
 *	like every other struct valued expression, which is why hier0() is
 *	used directly rather than expression_tree().
 */
static void return_struct(void)
{
	struct node *dst, *src, *n;

	if (token == T_SEMICOLON) {
		error("return value expected");
		write_tree(tree(T_NULL, NULL, NULL));
		return;
	}
	src = hier0(0);
	if (type_canonical(src->type) != func_type || PTR(src->type)) {
		typemismatch();
		write_tree(tree(T_NULL, NULL, NULL));
		return;
	}
	dst = new_node();
	dst->op = T_ARGUMENT;
	dst->value = 0;
	dst->type = type_ptr(func_type);
	dst->flags = LVAL;
	dst = make_rval(dst);		/* load the hidden pointer */

	n = sf_tree(T_EQ, dst, src);
	n->type = func_type;
	n->value = type_sizeof(func_type);
	write_tree(n);
}

static void return_statement(void)
{
	next_token();
	header(H_RETURN, func_tag, 0);
	if (IS_STRUCT(func_type) && !PTR(func_type))
		return_struct();
	else
		expression_typed(func_type);
	footer(H_RETURN, func_tag, 0);
}

static void break_statement(void)
{
	next_token();
	if (break_tag == 0)
		error("break outside of block");
	header(H_BREAK, break_tag, 0);
}

static void continue_statement(void)
{
	next_token();
	if (cont_tag == 0)
		error("continue outside of block");
	header(H_CONTINUE, cont_tag, 0);
}

static void switch_statement(void)
{
	unsigned oldbrk = break_tag;
	unsigned oldswt = switch_tag;
	unsigned oldswc = switch_count;
	unsigned oldswtype = switch_type;
	unsigned olddefault = switch_default;
	cval_t *swptr;

	switch_tag = next_tag++;
	break_tag = next_tag++;
	switch_count = 0;

	next_token();
	header(H_SWITCH, switch_tag, break_tag);
	switch_type = bracketed_expression(0);

	/* Only integral types */
	if (!IS_INTARITH(switch_type)) {
		error("bad type");
		switch_type = CINT;
	}

	swptr = switch_alloc();

	statement_block(0);
	footer(H_SWITCH, switch_tag, break_tag);
	/* No default means non matched cases fall through to the end */
	if (!switch_default)
		header(H_DEFAULT, switch_tag, 0);

	switch_done(switch_tag, swptr, switch_type);

	switch_type = oldswtype;
	break_tag = oldbrk;
	switch_tag = oldswt;
	switch_count = oldswc;
	switch_default = olddefault;
}

static void case_statement(void)
{
	struct node *n;
	if (switch_tag == 0)
		error("case outside of switch");
	next_token();
	/* FIXME: type check range... */
	n = expression_tree(0);
	if (!is_constant(n))
		notconst();
	else
		switch_add_node(n->value);
	free_tree(n);
	header(H_CASE, switch_tag, ++switch_count);
	require(T_COLON);
}

static void default_statement(void)
{
	if (switch_tag == 0)
		error("default outside of switch");
	if (switch_default)
		error("two default cases");
	switch_default = 1;
	header(H_DEFAULT, switch_tag, 0);
	next_token();
	require(T_COLON);
}

static void goto_statement(void)
{
	unsigned n;
	next_token();
	if ((n = symname()) == 0)
		error("label required");
	/* We will work out if the label existed later */
	use_label(n);
	header(H_GOTO, func_tag, n);
}

/*
 *	C statements.
 *
 *	This can be a declaration, in which case it starts with a token that
 *	describes storage properties, a keyword, a name followed by a colon
 *	(which is a label), or an expression. A null expression is also allowed.
 */
static void statement(void)
{
	/* It's valid to have a {} block */
	if (token == T_RCURLY)
		return;

	/*
	 * This is where a "declaration_block()" call used to sit disabled,
	 * marked "C99 for later if we want it". We do want it, but not
	 * here: statement() is also what a bare "if (x) ..." body is, and
	 * "if (x) int y;" is legal in no dialect. Mixed declarations are
	 * handled in statement_block(), where a declaration really is
	 * allowed.
	 */
	/* Check for keywords */
	switch (token) {
	case T_IF:
		if_statement();
		return;
	case T_WHILE:
		while_statement();
		return;
	case T_SWITCH:
		switch_statement();
		return;
	case T_DO:
		do_statement();
		return;
	case T_FOR:
		for_statement();
		return;
	case T_RETURN:
		return_statement();
		break;
	case T_BREAK:
		break_statement();
		break;
	case T_CONTINUE:
		continue_statement();
		break;
	case T_GOTO:
		goto_statement();
		break;
	/* statement_block() consumes any run of labels before calling us,
	   so these are only reached for a case or default that is not
	   prefixing a statement at all. Report and move on rather than
	   letting it fall into the expression parser. */
	case T_CASE:
		case_statement();
		return;
	case T_DEFAULT:
		default_statement();
		return;
	case T_SEMICOLON:
		next_token();
		return;
	default:
		/* It is valid to follow a label with just ; */
		if (token != T_SEMICOLON) {
			struct node *n = expression_tree(1);
			/* A statement top node need not worry about
			   generating the correct result */
			n->flags |= NORETURN;
			write_tree(n);
		}
		break;
	}
	require(T_SEMICOLON);
}


/*
 *	Either a statement or a sequence of statements enclosed in { }. In
 *	some cases the sequence is mandatory (eg a function) so we pass in
 *	need_brack to tell us what to do.
 */
void statement_block(unsigned need_brack)
{
	struct symbol *ltop;
	struct symbol *obase;
	if (token == T_EOF) {
		fatal("unexpected EOF");
		return;
	}
	/*
	 * Labels prefix a statement, they are not statements themselves:
	 * "case 1: return 1;" is one labelled statement. So consume any
	 * run of them here and then parse the statement they belong to.
	 *
	 * case and default have to be in this loop with ordinary labels.
	 * They used to be handled in statement(), which returned after
	 * emitting the label - fine inside a { } body, where the enclosing
	 * loop picks up the next statement anyway, and wrong when the
	 * switch body is a bare statement:
	 *
	 *     switch (x)
	 *         case 1:
	 *             return 1;
	 *
	 * ended the switch at the colon, so the case body landed *after*
	 * the break label and ran whether or not the case matched.
	 *
	 * We could write this not to push back a token but it's
	 * actually much cleaner to push back
	 */
	for (;;) {
		if (token == T_CASE) {
			case_statement();
			continue;
		}
		if (token == T_DEFAULT) {
			default_statement();
			continue;
		}
		if (token < T_SYMBOL)
			break;
		{
			unsigned name = token;
			next_token();
			if (token == T_COLON) {
				next_token();
				/* We found a label */
				add_label(name);
				header(H_LABEL, func_tag, name);
			} else {
				push_token(name);
				break;
			}
		}
	}
	if (token != T_LCURLY) {
		if (need_brack)
			require(T_LCURLY);
		statement();
		return;
	}
	next_token();
	ltop = mark_local_symbols();
	/*
	 * Names declared from here on belong to this block and may shadow
	 * anything below. Saved and restored because blocks nest.
	 *
	 * Not for a function body: its parameters and its outermost block
	 * are one scope in C, so type_name_parse has already set the base
	 * below the parameters and this must not move it above them.
	 */
	obase = block_base;
	if (!need_brack)
		block_base = ltop;

	/*
	 * Declarations and statements, in any order.
	 *
	 * C89 wants every declaration at the head of the block, and this
	 * used to parse them once here and then loop over statements
	 * only. Mixing them is a C99 rule, but it is what everyone writes
	 * and refusing it is a nuisance out of all proportion to the
	 * standard it comes from - so the compiler is C89 plus this.
	 *
	 * It is a pure relaxation: nothing that was legal before changes
	 * meaning, and block scope already works, so a declaration part
	 * way down a block behaves exactly like one at the top of it.
	 *
	 * A declaration only where a *statement* may appear, mind - not
	 * inside statement() - or "if (x) int y;" would be accepted, and
	 * that is legal in no dialect at all.
	 */
	while (token != T_RCURLY) {
		/* A typedef is a declaration but not one declaration() can
		   handle - it has its own syntax - and it was previously
		   only ever recognised in toplevel(), so "typedef int myint;"
		   inside any block was rejected. It is scoped to the block:
		   update_typedef() marks it so pop_local_symbols() below
		   discards it. */
		if (token == T_TYPEDEF) {
			next_token();
			dotypedef();
		} else if (is_modifier() || is_storage_word() ||
				is_type_word() || is_typedef())
			declaration(S_AUTO);
		else
			statement_block(0);
	}
	block_base = obase;
	pop_local_symbols(ltop);
	next_token();
}

/*
 *	We have parsed the declaration part of a function and found it
 *	is followed by a body. Set up the headersfor the backend and turn
 *	the contents into expressions and headers.
 */
void function_body(unsigned st, unsigned name, unsigned type)
{
	/* This makes me sad, but there isn't a nice way to work out
	   the frame size ahead of time */
	unsigned long hrw;
	unsigned *p;
	unsigned n;
	unsigned dead;

	func_flags = 0;

	/* Pass useful information flags to the backend */
	func_type = func_return(type);
	if (func_type == VOID)
		func_flags |= F_VOIDRET;
	p = func_args(type);
	n = *p++;
	if (n == 1 && *p == VOID)
		func_flags |= F_VOID;
	while(n--) {
		if (*p++ == ELLIPSIS) {
			func_flags |= F_VARARG;
			break;
		}
	}

	if (st == S_AUTO || st == S_EXTERN)
		error("invalid storage class");

	/*
	 * A file scope static whose name occurs exactly once in the token
	 * stream - here, in its own definition - can have no caller and
	 * nothing can have taken its address, so parse it for its errors
	 * and generate nothing. That is what lets a header carry a
	 * library of helpers and a program pay only for the ones it uses;
	 * with no linker there is nothing to strip it later.
	 *
	 * name_unreachable is the same idea carried further: counting
	 * names keeps whatever a DEAD function mentions, which was fine
	 * for a header holding one primitive and useless against the
	 * sprite and blit engines, where one entry point named the other
	 * fourteen. It walks the static call graph from the roots
	 * instead. Both are asked; either is enough to drop the code.
	 */
	dead = (st == S_STATIC &&
		(name_used_once(name) || name_unreachable(name)));
	if (dead)
		out_off++;

	func_tag = next_tag++;
	header(H_FUNCTION, func_tag, name);
	hrw = mark_header();
	header(H_FRAME, 0, 0);

	init_labels();

	/* Note that we are *inside* a body. funcbody looks like it says
	   this and does not - it is set after function_body() returns, to
	   mean "one has just been parsed". */
	in_funcbody++;
	statement_block(1);
	in_funcbody--;

	/*
	 * Falling off the end of main returns 0.
	 *
	 * C89 leaves that value undefined, but every implementation makes
	 * it zero and programs rely on it. Without this, main returned
	 * whatever happened to be in the accumulator - so a program that
	 * ended in a printf exited with the character count as its status,
	 * which is what stopped printf being able to return one.
	 *
	 * Emitted unconditionally: if main already ended with a return
	 * this is a few unreachable bytes, which is cheaper than working
	 * out whether the last statement could fall through.
	 */
	if (name == T_MAIN && func_type != VOID) {
		header(H_RETURN, func_tag, 0);
		write_tree(make_constant(0, func_type));
		footer(H_RETURN, func_tag, 0);
	}

	footer(H_FUNCTION, func_tag, name);

	rewrite_header(hrw, H_FRAME, frame_size(), func_flags);
	check_labels();

	if (dead)
		out_off--;
}

/*
 * Preprocessor cases that had no gate.
 *
 * Every host-side test in this tree preprocesses with "gcc -E" - both
 * Applications/CC/hosttest/ctest.sh and mmb2c's fccbuild.sh do - so
 * this cpp is only ever exercised on the board, and three C89 gaps sat
 * in it undisturbed until the c-testsuite was run there.
 *
 * A failure is an #error, so cpp exits non-zero and says which case.
 * Run with cpptest.sh.
 */

/* ---- ?: in #if expressions (was TODO) ---------------------------- */

#if (0 ? 1 : 3) != 3
#error conditional, false arm
#endif
#if (1 ? 3 : 1) != 3
#error conditional, true arm
#endif
#if (1 || 0 ? 5 : 6) != 5
#error conditional binds looser than ||
#endif
#if (0 && 1 ? 5 : 6) != 6
#error conditional binds looser than &&
#endif
#if (0 ? 1 : 0 ? 2 : 3) != 3
#error conditional is right associative
#endif
#if (1 ? 0 ? 7 : 8 : 9) != 8
#error conditional nested in the middle arm
#endif
#if (2 > 1 ? 10 : 20) != 10
#error relational condition
#endif
#if (3 ? 3 : 3) + 1 != 4
#error conditional feeds the enclosing expression
#endif
#if (-1 ? 3 : (0/0)) != 3
#error the untaken arm must not spoil the value
#endif
#if (1 ? 2 : 3) * (0 ? 4 : 5) != 10
#error two conditionals in one expression
#endif

/* ---- __LINE__ in #if expressions --------------------------------- */

#if __LINE__ != 48
#error __LINE__ in a constant expression
#endif

/* ---- #line, plain and through a macro (was TODO) ------------------ */

#line 500
#if __LINE__ != 500
#error #line with a plain number
#endif
#if __LINE__ != 503
#error #line numbering continues
#endif

#define line 1000
#line line
#if __LINE__ != 1000
#error #line with a macro argument
#endif

/* ---- a directive name is never macro replaced --------------------- */

#define define 1
#define include 2
#define endif 3
#undef define
#undef include
#undef endif

#line 90
#if __LINE__ != 90
#error a macro named like a directive must not break directives
#endif

/* ---- an argument is expanded before substitution, but not when it
        is an operand of ## --------------------------------------- */

#define CATN(x,y) x ## y
#define XCATN(x,y) CATN(x,y)
#define ONE 1
#define TWO 2
#define ONETWO 99

/* CATN's own parameters ARE operands of ##, so they paste as written:
   ONE ## TWO is ONETWO, which the rescan then finds is 99. */
#if CATN(ONE,TWO) != 99
#error a ## operand must not be expanded before it is pasted
#endif

/* XCATN's are not, so they expand first and CATN pastes 1 and 2. */
#if XCATN(ONE,TWO) != 12
#error an argument must be expanded before substitution
#endif

/* the same through two levels of indirection */
#define YCATN(x,y) XCATN(x,y)
#if YCATN(ONE,TWO) != 12
#error expansion through two levels
#endif

int pptest_ok;

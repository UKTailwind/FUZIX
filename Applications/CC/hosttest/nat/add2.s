@ Hand-written native add2(a, b) - the whole point is what it does NOT
@ do: no million.  Register file per bytecode.h: r4 = VM stack pointer
@ at the return-pc parity slot (args from +4 up), result in r0/r1 as
@ the accumulator - 32-bit values sign-extended, like everything in A.
	.syntax unified
	.thumb
	.text
add2:
	ldr	r0, [r4, #4]	@ first argument, nearest the slot
	ldr	r1, [r4, #8]	@ second
	adds	r0, r0, r1
	asrs	r1, r0, #31	@ sign-extend into A's high half
	bx	lr

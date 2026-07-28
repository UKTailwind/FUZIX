# The PC3 bytecode

Frozen 2026-07-28. `bytecode.h` is normative; this explains the model
and the conventions a front end has to follow.

The point of the layer is that C, BASIC and Pascal front ends all target
it, and there are two back ends: an interpreter, and later a Thumb
translator. See PC3-COMPILER-PLAN.md.

## Machine

    A     accumulator, 32 bits, holds any value
    S     evaluation stack of 32-bit slots
    FP    frame pointer: locals and arguments live at FP + offset
    PC    program counter

Binary operators take the left operand from the stack and the right from
the accumulator, leaving the result in the accumulator:

    A = pop() OP A

That is the order cc2's tree walker already produces, so the C front end
needs no reordering.

## Everything arithmetic is 32-bit

This is the one real departure from `Operations.md`, which carries 16-
and 32-bit variants of every operator. Here, **width only matters when
touching memory or converting**. `char` and `short` exist in memory and
nowhere else; loaded values are sign- or zero-extended to 32 bits at the
point of load, exactly as the target machine behaves.

That removes a variant from every arithmetic and comparison opcode, and
means a front end never has to reason about the width of an
intermediate. It costs nothing on a 32-bit machine.

## Loads and stores

Loads take the address in `A` and replace it with the value:

    BC_LOAD8S  BC_LOAD8U  BC_LOAD16S  BC_LOAD16U  BC_LOAD32

Stores take the address from the stack and the value from `A`, and
**leave `A` alone**, so an assignment is an expression yielding the value
assigned:

    BC_STORE8  BC_STORE16  BC_STORE32

So `x = y + 1` is: address of x, `BC_PUSH`, evaluate `y + 1` into A,
`BC_STORE32`.

## Calls and frames

A function is entered with arguments already on the stack, pushed right
to left, each occupying a whole 32-bit slot.

    BC_ENTER n     reserve n bytes of locals, set FP
    ...
    BC_LEAVE n     release them
    BC_RET         return, result in A

The caller discards arguments with `BC_ARGS n` after the call returns.
`BC_CALL` takes a 32-bit address patched by a fixup; `BC_CALLA` calls
the address already in `A`, which is how calls through function
pointers work.

`BC_LIBCALL n` calls runtime library function `n`. There is no linker,
so the library — `printf` and friends — is provided by the interpreter
rather than linked in. That is what makes single-file programs viable
without a linker, and it is the mechanism a BASIC front end would use
for its own runtime too.

## Object format

No linker, so the format is deliberately thin: a header, code, data,
a fixup table and a symbol table. A fixup names a symbol whose value the
loader adds to the 32-bit field at the given offset. Symbols are local
to the module apart from `BC_SYM_LIB`, which names a library entry point
rather than an address.

Jumps are PC-relative signed 16-bit from the end of the instruction, so
code is position independent and needs no fixups at all — only addresses
of data, functions and switch tables do.

## Notes for non-C front ends

The op set is deliberately C-shaped, because that is where it came from.
Two things a BASIC or Pascal front end will notice:

* **No string, set or array operations.** Those go through `BC_LIBCALL`
  into the runtime, which is the right place for them — they are not
  machine operations on any real target either.
* **No nested procedures.** Pascal's static links would have to be
  passed explicitly as a hidden argument, or the front end would have to
  lambda-lift. Nothing in the encoding prevents either.

If a future front end genuinely needs a new primitive, add an opcode
below `BC_MAXOP` and bump `BC_VERSION`. There is plenty of room: the set
uses about 50 of 256 encodings.

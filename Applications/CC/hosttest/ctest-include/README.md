# Headers for the conformance run

The c-testsuite tests tagged `needs-libc` include `<stdio.h>` and
friends. Preprocessing them against the *host's* glibc headers is
useless: those are full of GNU extensions - `__builtin_va_list`,
`__attribute__`, `__restrict` - that a C89 compiler cannot parse, and
every such test then fails in cc1 for a reason that has nothing to do
with the test. The first run of `ctest.sh` did exactly that and blamed
43 tests on "missing semicolon".

These are minimal C89 headers declaring **only what bcrun's runtime
actually provides**. That matters: promising a function we do not have
would turn an honest "needs a runtime function we lack" into a
mysterious crash. If a test needs something not declared here, it
should fail, and the failure should say so.

`ctest.sh` uses them with `-nostdinc`, so nothing of the host's leaks
in.

Declarations are deliberately in the C89 unspecified-argument form
(`int printf();`) rather than prototyped, because that is the form the
rest of this compiler's test material uses and it exercises the same
path the on-target code takes.

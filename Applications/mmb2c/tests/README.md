# Test programs

`make check` builds every `.bas` here, runs it, and diffs the output
against the matching `.expected`. A test may bring two companions:

| file | what it does |
|---|---|
| `<name>.in` | fed to the program on stdin |
| `<name>.expected` | what it should print |

Lines reporting elapsed time are filtered from both sides before the
comparison, since they vary run to run. `make bless` regenerates the
`.expected` files — only do that once you have checked the new output is
actually right.

## What each one covers

| test | covers |
|---|---|
| `t1` | declarations, assignment, PRINT, every loop form, SELECT CASE, SUBs and FUNCTIONs, string returns, STATIC |
| `t2` | the scope model: implied globals, globals reached from inside a SUB, LOCAL shadowing, by-reference vs BYVAL, array parameters (including a non-square 2-D one, which is what pins down the row stride) |
| `t3` | every shape of IF, and colon-separated statements |
| `t4` | IF and colon edge cases: calls inside a single-line IF, labels vs subroutine calls, nesting |
| `t5` | the built-in functions, checked against the manual's own worked examples |
| `t6` | file handling: write, append, read by line and by field, LOF/LOC/SEEK, random access, copy/rename/delete, directories |
| `t7` | DATA/READ/RESTORE, SORT, CONTINUE, INC, CAT, ON GOTO, PAUSE, TIMER=, ARRAY/MATH, ERASE |
| `t8` | the LONGSTRING family, and GOSUB/RETURN including nesting and ON n GOSUB |
| `solar_eclipse` | 3213 lines of real astronomy — see below |
| `xcheck/` | not a `.bas`: compiles the interpreter's own number formatting alongside this runtime's and diffs them over 4896 cases. `make xcheck`. |

## solar_eclipse.bas

Predicts the local circumstances of solar eclipses. Byte-for-byte as
supplied — a Micromite eXtreme program dated 14 March 2017, carrying no
attribution in its header; it is not original to this project and is
included only as a translation fixture.

It earns its place by being 3213 lines of code nobody wrote with this
translator in mind. Finding and fixing what it broke gave: built-ins that
take arguments being told apart from variables of the same name (`min`,
`max`), multi-dimensional array parameters, bare `EXIT` inside a SUB, and
a DOS end-of-file marker.

The fixture inputs are Denver on 1 December 2000, searching 30 days:

```
12,1,2000        December 1, 2000
39,40,36         latitude  39 deg 40 min 36 sec north
-104,57,12       longitude 104 deg 57 min 12 sec west
1644             altitude, metres
30               search duration, days
```

It should find the partial eclipse of 25 December 2000, greatest at
16:43:32.47 UTC, duration 2.61100904 hours. Those numbers were confirmed
correct against the interpreter.

One caveat worth knowing: every matrix in this program is 3x3, so a wrong
row stride in a multi-dimensional array parameter is invisible to it.
That is why `t2` carries a deliberately non-square 2-D array.

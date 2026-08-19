Cross-check of the number formatting against the interpreter itself.

ref_body.c is IntToStr / IntToStrPad / FloatToStr lifted unchanged from
PicoMite's core/MMBasic.c (https://github.com/UKTailwind/PicoMite).
xcheck.c compiles those alongside mmb_runtime.c and compares the two
implementations over several thousand combinations of value, field
width, precision and pad character.

  gcc -std=c99 -w -I.. -o xcheck xcheck.c ../mmb_runtime.c -lm && ./xcheck

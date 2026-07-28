#!/bin/bash
#
# Differential opcode test.
#
#   ./optest.sh samples/optest.c
#
# The test programs are written in the subset both gcc and FCC accept,
# so gcc is the oracle: build and run the program natively, then build
# and run it under bcrun, and diff. Any difference is a bug in the
# compiler or the interpreter rather than a judgement call.
#
# Also reports which bytecode opcodes the program actually exercised,
# so "untested" is a measurement and not an assumption.

CC=$(cd "$(dirname "$0")/.." && pwd)
W=${W:-/tmp/fcc-optest}
src=$1
[ -n "$src" ] || { echo "usage: optest.sh file.c" >&2; exit 1; }
case "$src" in /*) ;; *) src="$PWD/$src" ;; esac

rm -rf "$W"; mkdir -p "$W"; cd "$W" || exit 1
b=$(basename "$src" .c)

# --- reference: the same source under gcc ---------------------------
# -funsigned-char: plain char is unsigned on our target (gcc's ARM
# default), so the oracle must be told or every char test disagrees.
# -std=gnu89: FCC's dialect is C89, where "int printf();" means an
# unspecified argument list. Modern gcc defaults to C23 and reads that
# as "no arguments", rejecting every call.
if ! gcc -std=gnu89 -funsigned-char -w -o "$b.ref" "$src" 2> "$b.gcc.err"; then
    echo "gcc could not build the test:"; sed 's/^/  /' "$b.gcc.err"; exit 1
fi
./"$b.ref" > "$b.ref.out" 2>&1

# --- ours: cc0 | cc1 | cc2, then interpret --------------------------
gcc -E -P "$src" > "$b.pp" || exit 1
"$CC/host-armm0/cc0" < "$b.pp" > "$b.tok" 2> "$b.cc0.err"
rm -f "$b.ir"; "$CC/host-armm0/cc1" < "$b.tok" 1<> "$b.ir" 2> "$b.cc1.err"
if grep -q ' - ' "$b.cc1.err"; then
    echo "cc1 errors:"; grep ' - ' "$b.cc1.err" | head -10 | sed 's/^/  /'
fi
rm -f "$b.bc"; "$CC/host-armm0/cc2" .symtmp armm0 0 < "$b.ir" 1<> "$b.bc" 2> "$b.cc2.err"
[ -s "$b.cc2.err" ] && { echo "cc2 errors:"; sed 's/^/  /' "$b.cc2.err"; }

"$CC/host-armm0/bcrun" "$b.bc" > "$b.bc.out" 2>&1
rc=$?

# --- compare --------------------------------------------------------
if diff -u "$b.ref.out" "$b.bc.out" > "$b.diff"; then
    echo "PASS  output identical to gcc ($(wc -l < "$b.ref.out") lines, bcrun rc=$rc)"
    ok=0
else
    echo "FAIL  output differs from gcc:"
    sed 's/^/  /' "$b.diff" | head -40
    ok=1
fi

# --- opcode coverage ------------------------------------------------
"$CC/host-armm0/bcdump" "$b.bc" 2>/dev/null > "$b.dis"
used=$(grep -oE '^[0-9a-f]{4}: [a-z0-9_]+' "$b.dis" | sed 's/.*: //' | sort -u)
all="nop const8 const16 const32 addr local8 local16 push pop dup swap drop
load8s load8u load16s load16u load32 store8 store16 store32
add sub mul divs divu rems remu and or xor shl shrs shru neg not lnot
eq ne lts ltu gts gtu les leu ges geu bool
sext8 sext16 zext8 zext16
jump jfalse jtrue call calla ret enter leave args libcall switch"

echo
echo "opcodes exercised:"
echo "$used" | tr '\n' ' ' | fmt -w 72 | sed 's/^/  /'
echo "not exercised:"
for op in $all; do
    echo "$used" | grep -qx "$op" || printf '%s ' "$op"
done | fmt -w 72 | sed 's/^/  /'
echo

exit $ok

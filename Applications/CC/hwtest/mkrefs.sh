#!/bin/sh
# Build the transcript the board run should produce, in the same shape
# runsamples.sh prints it, so the two can be diffed line for line.
cd "$(dirname "$0")" || exit 1
out=/tmp/hostrefs.txt
: > "$out"
for f in autoinit blocktypedef braceelide dbl escapes fileio fmt fp \
         goto libtest ll2 mainret mixdecl namespace optest ptrarray \
         rpn scope sieve statics strs struct2 struct3 struct4 sw2 \
         tagscope unaryplus voidcomma width3
do
	echo "=== $f" >> "$out"
	cat "$f.ref.out" >> "$out"
done
echo "=== END" >> "$out"
wc -l "$out"

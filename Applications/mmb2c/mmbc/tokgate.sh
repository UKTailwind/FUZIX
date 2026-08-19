#!/bin/bash
#
# Stage-1 gate: the C tokenizer's --tokens stream must be byte-identical
# to the Python's over every tests/*.bas.
#
#   bash mmbc/tokgate.sh

M=$(cd "$(dirname "$0")/.." && pwd)
W=${W:-/tmp/mmbcgate}
mkdir -p "$W"

make -s -C "$M/mmbc" || exit 1

pass=0; fail=0
for src in "$M"/tests/*.bas; do
	b=$(basename "$src" .bas)
	python3 "$M/mmb2c.py" --tokens "$src" > "$W/$b.py.tok"
	"$M/mmbc/mmbc" --tokens "$src" > "$W/$b.c.tok"
	if diff -q "$W/$b.py.tok" "$W/$b.c.tok" > /dev/null; then
		echo "pass  $b"
		pass=$((pass + 1))
	else
		echo "FAIL  $b"
		diff "$W/$b.py.tok" "$W/$b.c.tok" | head -6
		fail=$((fail + 1))
	fi
done
echo "tokgate: $pass pass, $fail fail"
[ $fail = 0 ]

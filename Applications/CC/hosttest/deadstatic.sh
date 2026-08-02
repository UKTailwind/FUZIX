#!/bin/bash
#
# cc1 drops a file scope static that nothing mentions twice.
#
#   bash deadstatic.sh
#
# There is no linker on this target, so a header carrying a library of
# static helpers would otherwise put every one of them in every program
# that includes it. The rule cc1 uses is deliberately conservative - a
# name occurring once in the token stream, which can only be its own
# definition - so the cases below check both directions: that dead code
# goes, and that anything even slightly ambiguous stays.

CC=$(cd "$(dirname "$0")/.." && pwd)
BIN=$CC/host-armm0
W=${W:-/tmp/deadstatic}
mkdir -p "$W"

build() {			# $1 = source file -> prints object size
	local b=$W/$(basename "$1" .c)
	gcc -E -P -nostdinc -U__LP64__ -U__LLP64__ -D__ILP32__ \
		"$1" > "$b.pp" 2>/dev/null
	"$BIN/cc0" < "$b.pp" > "$b.tok" 2>/dev/null
	rm -f "$b.ir" "$b.raw"
	"$BIN/cc1" < "$b.tok" 1<> "$b.ir" 2>/dev/null
	"$BIN/cc2" .symtmp armm0 0 < "$b.ir" 1<> "$b.raw" 2>/dev/null
	stat -c %s "$b.raw"
}

BODY='{int i,s=0;for(i=0;i<a;i++)s+=i*3+1;return s;}'

cat > "$W/base.c" <<EOF
int main(){return 0;}
EOF
cat > "$W/dead.c" <<EOF
static int unused(int a)$BODY
int main(){return 0;}
EOF
cat > "$W/live.c" <<EOF
static int used(int a)$BODY
int main(){return used(3);}
EOF
cat > "$W/recurse.c" <<EOF
static int rec(int a){return a?rec(a-1):0;}
int main(){return 0;}
EOF
cat > "$W/addr.c" <<EOF
static int taken(int a){return a;}
int main(){int(*p)(int)=taken;return p(1);}
EOF
{
	for k in 0 1 2 3 4 5 6 7 8 9; do
		echo "static int f$k(int a)$BODY"
	done
	echo "int main(){return f3(2);}"
} > "$W/ten.c"

base=$(build "$W/base.c")
dead=$(build "$W/dead.c")
live=$(build "$W/live.c")
rec=$(build "$W/recurse.c")
addr=$(build "$W/addr.c")
ten=$(build "$W/ten.c")

fail=0
check() {			# name, actual, test, expected, why
	if [ "$2" $3 "$4" ]; then
		echo "  ok   $1 ($2 bytes)"
	else
		echo "  FAIL $1: $2 bytes, expected $3 $4 - $5"
		fail=1
	fi
}

echo "empty main is $base bytes"
check "unused static dropped"   "$dead" -eq "$base" "it should cost nothing"
check "used static kept"        "$live" -gt "$base" "it is called"
check "recursive static kept"   "$rec"  -gt "$base" "it names itself, so the count is 2"
check "address-taken kept"      "$addr" -gt "$base" "CORRECTNESS: p() calls it"
check "ten statics, one used"   "$ten"  -lt "$((base + (live - base) * 3))" \
	"nine of the ten should be gone"

exit $fail

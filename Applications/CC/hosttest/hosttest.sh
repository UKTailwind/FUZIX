#!/bin/bash
#
# Differential front-end test for the ARM (32-bit) type model.
#
# Builds nothing: run "make -f Makefile.host" and
# "make -f Makefile.host CPU=z80" in the parent directory first, then
#
#     ./hosttest.sh torture.c
#     ./hosttest.sh /path/to/fuzix/Applications/util/*.c
#
# The method is differential. Anything that fails under armm0 but not
# under z80 is a bug in the 32-bit model, which is what we are looking
# for. Anything failing under both is a pre-existing FCC limitation and
# not our problem. That matters because FCC cannot parse parts of gcc's
# own stdint.h/stddef.h (long long, long double, __attribute__), so real
# sources produce plenty of noise -- but identical noise in both models,
# so it cancels.
#
# Gotcha worth knowing: cc1 seeks and reads back its own stdout to patch
# record headers (lex.c:out_record_read), so its output must be opened
# read-write. Shell ">" gives O_WRONLY and cc1 then dies with "read
# error" at a line number past the end of the file. Use "1<> file", and
# remove the file first because "1<>" does not truncate.

CC=$(cd "$(dirname "$0")/.." && pwd)
ROOT=$(cd "$CC/../.." && pwd)
WORK=${WORK:-/tmp/fcc-hosttest}

if [ ! -x "$CC/host-armm0/cc1" ] || [ ! -x "$CC/host-z80/cc1" ]; then
    echo "build both models first:" >&2
    echo "  make -f Makefile.host && make -f Makefile.host CPU=z80" >&2
    exit 1
fi

# Absolute-ise the arguments before changing directory.
srcs=()
for s in "$@"; do
    case "$s" in
    /*) srcs+=("$s") ;;
    *)  srcs+=("$PWD/$s") ;;
    esac
done

rm -rf "$WORK"; mkdir -p "$WORK"; cd "$WORK" || exit 1

differ=0; both=0; ok=0; total=0; better=0

for src in "${srcs[@]}"; do
    base=$(basename "$src" .c)
    # Preprocess as Target/rules.armm0 does.
    if ! arm-none-eabi-gcc -E -P -isystem "$ROOT/Library/include" \
            "$src" > "$base.pp" 2>/dev/null; then
        echo "SKIP(cpp) $base"
        continue
    fi
    total=$((total + 1))

    for m in armm0 z80; do
        "$CC/host-$m/cc0" < "$base.pp" > "$base.$m.tok" 2>/dev/null
        rm -f "$base.$m.ir"
        "$CC/host-$m/cc1" < "$base.$m.tok" 1<> "$base.$m.ir" 2> "$base.$m.err"
    done

    a=$(grep -c ' - ' "$base.armm0.err")
    z=$(grep -c ' - ' "$base.z80.err")

    if [ "$a" = "0" ] && [ "$z" = "0" ]; then
        ok=$((ok + 1))
    elif [ "$a" -gt "$z" ]; then
        # Only this direction is a bug: the 32-bit model rejecting
        # something the 16-bit one accepts.
        differ=$((differ + 1))
        echo "REGRESSION  $base  armm0=$a z80=$z"
        diff <(grep ' - ' "$base.z80.err") <(grep ' - ' "$base.armm0.err") \
            | grep '^>' | head -5 | sed 's/^/    /'
    elif [ "$a" -lt "$z" ]; then
        better=$((better + 1))
        echo "improved    $base  armm0=$a z80=$z (32-bit accepts more)"
    else
        both=$((both + 1))
    fi
done

echo
echo "corpus: $total files   (work kept in $WORK)"
echo "  clean in both models     : $ok"
echo "  same errors in both      : $both   (pre-existing, not ours)"
echo "  32-bit accepts more      : $better"
echo "  REGRESSIONS (32-bit bugs): $differ"

[ "$differ" = "0" ]

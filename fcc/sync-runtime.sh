#!/bin/sh
#
# The master copies of the mm runtime AND of the C translator live in
# this repo.  bcrun's hosted build (FUZIX Applications/CC, bcrun_mm.c)
# compiles verbatim copies of the runtime, and the board's mmbc is built
# there from verbatim copies of mmbc*.c; re-run this after editing
# mmb_runtime.c, any mmb_*.h, or any mmbc source, so the two trees stay
# identical.
#
# THE TRANSLATOR WAS NOT IN HERE UNTIL 2026-08-18, and it is the worst
# omission of the set: mmbc is what turns BASIC into C ON THE BOARD, so
# a stale copy there builds a translator that does not know a keyword
# this repo's gates have just proved.  It is silent - the build
# succeeds, cgate passes here, and the board rejects the program.
# Found when LOAD JPG translated on the host and Applications/CC still
# held the previous day's mmbc_stmt.c.
#
# EVERY mmb_*.h, by glob, deliberately.  This used to be a
# hand-maintained list and the list was wrong three separate times:
# mmb_int.h, mmb_pwm.h and mmb_i2c.h were each added to the card image
# and to hwbuild without being added here, and later mmb_sprite.h,
# mmb_play.h and mmb_playctl.h were never in it at all - so the two
# trees held different copies and an edit in this repo simply did not
# reach the board or the gates.  That is not a mistake anyone makes
# once: the failure is silent, the stale copy still compiles, and the
# board runs code the source no longer says.  A glob cannot forget.
#
# It also VERIFIES afterwards, and says so loudly if a file did not
# make it - a copy that fails must not look like a copy that worked.

M=$(cd "$(dirname "$0")/.." && pwd)
FCC=${FCC:-/home/peter/src/FUZIX/Applications/CC}

[ -d "$FCC" ] || { echo "sync-runtime: no $FCC" >&2; exit 1; }

cp "$M/mmb_runtime.c" "$M"/mmb_*.h "$FCC/" || exit 1
cp "$M"/mmbc/mmbc*.c "$M"/mmbc/mmbc*.h "$FCC/" || exit 1

bad=0
n=0
for f in "$M/mmb_runtime.c" "$M"/mmb_*.h "$M"/mmbc/mmbc*.c "$M"/mmbc/mmbc*.h; do
	b=$(basename "$f")
	n=$((n + 1))
	if ! cmp -s "$f" "$FCC/$b"; then
		echo "sync-runtime: FAILED to sync $b" >&2
		bad=$((bad + 1))
	fi
done
if [ "$bad" != 0 ]; then
	echo "sync-runtime: $bad file(s) differ after copying" >&2
	exit 1
fi
echo "synced $n files (mmb_runtime.c, every mmb_*.h, every mmbc source) -> $FCC"

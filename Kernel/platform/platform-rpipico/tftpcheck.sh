#!/bin/sh
#
# tftpd(1) still speaks TFTP.
#
#	sh tftpcheck.sh
#
# Applications/netd/tftpd.c is ordinary POSIX - sockets, open, read,
# write, alarm - so it builds for the host and the protocol can be
# driven there.  That is worth having because the cases that matter
# are the ones a happy transfer never reaches: a stale ACK, a
# duplicate DATA, a second client, a path trying to leave the
# directory.  Finding those on a board means a serial cable, a radio
# and ten minutes; finding them here takes fifteen seconds.
#
# WHAT THE HOST CANNOT TELL YOU.  This links glibc, and the board
# links Fuzix's libc - so this proves the protocol, not the library
# underneath it (see the netd tools' history of little-endian and
# time_t bugs that only a real machine showed).  Run it before
# sending a new tftpd to a board, not instead of.
#
# ONE SHIM IS NEEDED, and the reason is the interesting part.  Fuzix
# has no sigaction: signal() is all there is, and a signal there
# always interrupts a sleeping syscall - psleep_flags() returns EINTR.
# glibc's signal() is the BSD one and sets SA_RESTART, so recvfrom()
# restarts and the SIGALRM retransmit timer can never fire.  Without
# the shim below the retransmit case silently passes by never being
# reached.  It was written after exactly that happened.
#
# Needs python3 and a host cc.  Exit 1 on any failing case.

set -e
D=$(cd "$(dirname "$0")" && pwd)
R=$(cd "$D/../../.." && pwd)
SRC=$R/Applications/netd/tftpd.c
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT

[ -r "$SRC" ] || { echo "no $SRC" >&2; exit 1; }
command -v python3 > /dev/null 2>&1 || {
	echo "no python3 - not checked" >&2; exit 1; }
command -v cc > /dev/null 2>&1 || {
	echo "no host cc - not checked" >&2; exit 1; }

cat > "$W/hostsig.c" <<'EOF'
/*  Host-test scaffolding: give glibc's signal() the target's
    semantics, so that a signal interrupts rather than restarts.  */
#include <signal.h>

void (*signal(int s, void (*h)(int)))(int)
{
	struct sigaction sa, old;

	sa.sa_handler = h;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;		/* no SA_RESTART: as Fuzix */
	if (sigaction(s, &sa, &old) < 0)
		return SIG_ERR;
	return old.sa_handler;
}
EOF

cc -w -O2 -o "$W/tftpd" "$SRC" "$W/hostsig.c"
python3 "$D/tftpcheck.py" "$W/tftpd"

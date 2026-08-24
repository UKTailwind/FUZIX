"""Push everything that has to move together when BASIC support changes.

  python pushall.py            # build nothing, send what is already built
  python pushall.py --headers  # only the cc headers

Add a statement, or change mmb_runtime.c/.h or the mmb_gfx*.h set, and
these things on the board are involved:

    /usr/lib/cc/include/mmb_*.h         every header the on-board cc
                                        reads - one per primitive or
                                        family, taken by glob from
                                        Applications/mmb2c
    /usr/bin/bcrun                      the runtime is compiled INTO it
    /usr/bin/mmbc                       the translator
    /usr/bin/mmedit                     its keyword colouring

Miss one and it fails at RUN time, not build time, because the on-board
cc allows implicit declarations: a call to a function a stale header no
longer declares still compiles, and comes out with the WRONG ARGUMENT
WIDTHS.  That cost an afternoon once - mm_map's index arrived as
(colour << 32) | index and the error blamed the index.

Build first, from the FUZIX tree:

    make -f Makefile.armm0 FUZIX_ROOT=... USERCPU=armm0 bcrun mmbc
    (cd ../../../Applications/mmedit && make -f Makefile.armm0 ...)

then run this.  Everything is stripped on the way.
"""
import glob
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
FUZIX = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))
CC = os.path.join(FUZIX, "Applications", "CC")
MMB = os.path.join(FUZIX, "Applications", "mmb2c")
MMEDIT = os.path.join(FUZIX, "Applications", "mmedit")
STRIP = "arm-none-eabi-strip"
TMP = os.environ.get("TEMP", "/tmp")

# EVERY mmb_*.h, BY GLOB, from the one place they live.
#
# This was a hand-written list of thirteen, and it named them in
# Applications/CC - where they were COPIES until the copies were
# deleted, so by the time anyone ran it the list was thirteen paths to
# nothing.  mkccimage.sh, which builds the card, has globbed this
# directory for releases and says why in its own comment: "a glob
# cannot forget".  The card and the incremental push now agree because
# they ask the same question.
HEADERS = [(p, os.path.basename(p),
            "/usr/lib/cc/include/" + os.path.basename(p))
           for p in sorted(glob.glob(os.path.join(MMB, "mmb_*.h")))]
if not HEADERS:
    sys.exit("no mmb_*.h in %s" % MMB)
# The STRIPPED binaries, which stageall.sh has already produced in
# hwtest/ - the same files the card image installs.  Stripping here
# instead would mean running the cross toolchain, and this script runs
# on the Windows side where the serial port is, while the toolchain
# lives in WSL.  So: build, stageall.sh, then this.
BINARIES = [
    (os.path.join(CC, "hwtest", "bcrun.s"), "bcrun", "/usr/bin/bcrun"),
    (os.path.join(CC, "hwtest", "mmbc.s"), "mmbc", "/usr/bin/mmbc"),
    (os.path.join(CC, "hwtest", "mmedit.s"), "mmedit", "/usr/bin/mmedit"),
]
# The compiler passes.  Not part of the BASIC set above - a board can
# run a program built on the host without them being current - but a
# change to the code generator only reaches an ON-BOARD compile if they
# go too, and a board whose cc2 is older than its bcrun quietly builds
# the objects the old one built.  --cc sends just these.
COMPILER = [
    (os.path.join(CC, "hwtest", "cc0.s"), "cc0", "/usr/lib/cc/cc0"),
    (os.path.join(CC, "hwtest", "cc1.s"), "cc1", "/usr/lib/cc/cc1"),
    (os.path.join(CC, "hwtest", "cc2.s"), "cc2", "/usr/lib/cc/cc2"),
]


def run(args):
    r = subprocess.run([sys.executable] + args, cwd=HERE)
    if r.returncode:
        sys.exit("failed: %s" % " ".join(args))


def send(local, remote):
    # NO gap_ms ARGUMENT.  This used to pass 5, reasoning that 0 had
    # overrun the rx FIFO and a little pacing had not wedged a board
    # yet.  It did, on 2026-08-18, on a run of four transfers - and 6
    # went down the same way.  The gap is not a knob to trade against
    # transfer time: a slow send costs minutes, a wedged board costs an
    # fsck and an afternoon.  uusend.py's own default is 25 ms and its
    # comment says why (the tty queue is 132 bytes).  Take it.
    run(["uusend.py", local, remote])


def shell(*cmds):
    run(["fzsh.py", "5"] + list(cmds))


def install(table):
    for src, name, dest in table:
        if not os.path.exists(src):
            print("skipping %s - run stageall.sh first" % name)
            continue
        send(src, name)
        # chmod before the mv: a half-sent binary should never be the
        # one sitting in /usr/bin.
        shell("chmod +x " + name, "mv %s %s" % (name, dest))


def main():
    only_headers = "--headers" in sys.argv
    only_cc = "--cc" in sys.argv

    if only_cc:
        install(COMPILER)
        return

    for src, name, dest in HEADERS:
        send(src, name)
        shell("mv %s %s" % (name, dest))
    if only_headers:
        return

    install(BINARIES)
    # The compiler last: it is the biggest send and the least likely to
    # be what is being iterated on.  --cc does it alone.
    install(COMPILER)


main()

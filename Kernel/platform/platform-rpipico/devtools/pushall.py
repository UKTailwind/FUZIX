"""Push everything that has to move together when BASIC support changes.

  python pushall.py            # build nothing, send what is already built
  python pushall.py --headers  # only the two cc headers

Add a statement, or change mmb_runtime.c/.h or mmb_gfx.h, and FIVE
things on the board are involved:

    /usr/lib/cc/include/mmb_runtime.h   what the on-board cc reads
    /usr/lib/cc/include/mmb_gfx.h       ditto
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
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
FUZIX = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))
CC = os.path.join(FUZIX, "Applications", "CC")
MMEDIT = os.path.join(FUZIX, "Applications", "mmedit")
STRIP = "arm-none-eabi-strip"
TMP = os.environ.get("TEMP", "/tmp")

HEADERS = [
    (os.path.join(CC, "mmb_runtime.h"), "mmb_runtime.h",
     "/usr/lib/cc/include/mmb_runtime.h"),
    (os.path.join(CC, "mmb_gfx.h"), "mmb_gfx.h",
     "/usr/lib/cc/include/mmb_gfx.h"),
    (os.path.join(CC, "mmb_gpio.h"), "mmb_gpio.h",
     "/usr/lib/cc/include/mmb_gpio.h"),
]
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


def run(args):
    r = subprocess.run([sys.executable] + args, cwd=HERE)
    if r.returncode:
        sys.exit("failed: %s" % " ".join(args))


def send(local, remote):
    run(["uusend.py", local, remote, "0"])


def shell(*cmds):
    run(["fzsh.py", "5"] + list(cmds))


def main():
    only_headers = "--headers" in sys.argv

    for src, name, dest in HEADERS:
        send(src, name)
        shell("mv %s %s" % (name, dest))
    if only_headers:
        return

    for src, name, dest in BINARIES:
        if not os.path.exists(src):
            print("skipping %s - run stageall.sh first" % name)
            continue
        send(src, name)
        # chmod before the mv: a half-sent binary should never be the
        # one sitting in /usr/bin.
        shell("chmod +x " + name, "mv %s %s" % (name, dest))


main()

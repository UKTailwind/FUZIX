"""Drive the PC3 through a demonstration, at a pace a camera can follow.

  python demo.py                    the whole thing, from the bootdev: prompt
  python demo.py --list             the scenes, and how long each one takes
  python demo.py --scene graphics   one scene, for rehearsing a single part
  python demo.py --from eclipse     pick up part way through
  python demo.py --fast             rehearsal speed: no reading pauses
  python demo.py --no-down          stop before the shutdown scene

The consoles are MIRRORED - one tty1 goes to both the HDMI display and
the serial line, and input is merged - so everything typed down the
serial port appears on the screen the camera is pointed at, as though a
hand were typing it.  That is the whole trick: this types, the video
records the screen, and nobody has to type accurately on camera.

Two settings do the pacing.  DEMO_CPM is the typing speed in characters
per minute and defaults to 150 - an ordinary hand at a keyboard, which
is what it has to look like.  DEMO_READ scales the pauses between steps,
which are what make a video watchable and the first thing to raise if it
feels rushed.  For a rehearsal, DEMO_CPM=2000 with --fast checks that
every command WORKS without also taking the full running time.

A step ends when the board has been quiet for `settle` seconds.  A
program that goes quiet WHILE IT WORKS needs a longer one: the compiler
prints a dot per function and is fine, but bench.bas computes silently
for 30 seconds a round and would have its output cut off - which is why
it is not in here.  Nothing in these scenes is silent for long.

Requires pyserial, and runs on the Windows side because the CH340 is a
COM port.  Set FZPORT if it is not COM14.
"""
import os
import sys
import time

import serial

PORT = os.environ.get("FZPORT", "COM14")
BAUD = 115200

# Typing speed in characters per minute.  150 is an ordinary hand at a
# keyboard - about 30 words a minute - and that is the point: it has to
# look like someone typing, not like a paste appearing.  Much above 300
# stops reading as typing on camera.
CPM = float(os.environ.get("DEMO_CPM", "150"))
CPS = 60.0 / CPM if CPM > 0 else 0.0                 # seconds per character
READ = float(os.environ.get("DEMO_READ", "1.0"))     # reading-pause scale

FAST = False
DRY = False          # count the take instead of driving the board
DRY_SECONDS = 0.0

CLS = r"echo -e '\033[2J\033[H'"    # no clear(1) on this machine; this works
F1 = b"\x1b[11~"                    # mmedit: save and exit (VT220 spelling)
DOWN = b"\x1b[B"
END = b"\x1b[F"                     # what this console's own End key sends
CTRL_D = b"\x04"


# --------------------------------------------------------------- primitives

def drain(ser, settle, limit=300.0):
    """Read until the board has been quiet for `settle` seconds."""
    global DRY_SECONDS
    if DRY:
        # A step costs its settle at worst; most commands answer sooner,
        # so this over-estimates slightly, which is the safe direction
        # for deciding how long to leave the camera running.
        DRY_SECONDS += settle
        return ""
    buf = b""
    last = t0 = time.time()
    while time.time() - t0 < limit:
        n = ser.in_waiting
        if n:
            buf += ser.read(n)
            last = time.time()
        elif time.time() - last > settle:
            break
        else:
            time.sleep(0.02)
    text = buf.decode("latin-1")
    sys.stdout.write(text)
    sys.stdout.flush()
    return text


def beat(seconds):
    """A reading pause.  Rehearsals skip these; the real take needs them."""
    global DRY_SECONDS
    if FAST:
        return
    if DRY:
        DRY_SECONDS += seconds * READ
        return
    time.sleep(seconds * READ)


def type_char(ser, ch):
    global DRY_SECONDS
    if DRY:
        DRY_SECONDS += CPS
        return
    ser.write(ch.encode("latin-1"))
    ser.flush()
    time.sleep(CPS)


def type_line(ser, line, settle=0.8, hold=0.7):
    """Type a line a character at a time, press Enter, wait for the reply.

    `hold` is the beat between the last character and Enter.  A person
    leaves one, and on camera it is what lets the viewer read the
    command BEFORE its effect arrives - which matters most for the
    commands that wipe the screen, where otherwise the clear and its
    cause land in the same frame and the viewer never sees what did it.
    """
    for ch in line:
        type_char(ser, ch)
    beat(hold)
    if not DRY:
        ser.write(b"\r")
        ser.flush()
    return drain(ser, settle)


def send_raw(ser, data, settle=0.8):
    """A control character or escape sequence - Ctrl-D, a function key."""
    if not DRY:
        ser.write(data)
        ser.flush()
    return drain(ser, settle)


def expect(ser, needle, limit=180.0):
    """Wait for a particular string - the boot prompts, which have no shell."""
    global DRY_SECONDS
    if DRY:
        DRY_SECONDS += 3.0        # the boot prompts, roughly
        return ""
    buf = ""
    t0 = time.time()
    while time.time() - t0 < limit:
        n = ser.in_waiting
        if n:
            chunk = ser.read(n).decode("latin-1")
            sys.stdout.write(chunk)
            sys.stdout.flush()
            buf += chunk
            if needle in buf:
                return buf
        else:
            time.sleep(0.05)
    raise SystemExit("timed out waiting for %r" % needle)


def settled_on(ser, already, needle):
    """Make sure `needle` has arrived, given text a drain already took.

    Every step here ends in a drain, and a drain consumes whatever the
    board sent - including the prompt the next step is waiting for.  So
    check what was already read BEFORE blocking on expect(), or the wait
    is for a second copy of something that has been and gone.  Both boot
    prompts arrive inside the preceding step's settle time, so this is
    the normal path and expect() is the fallback.
    """
    if needle in already:
        return already
    return already + expect(ser, needle)


def clear_screen(ser):
    """Wipe the screen, having given the viewer time to see what did it."""
    type_line(ser, CLS, settle=1, hold=1.5)


def heredoc(ser, name, lines):
    """Write a file the way the manual does it: cat > name, then Ctrl-D."""
    type_line(ser, "cat > " + name, settle=1)
    for line in lines:
        type_line(ser, line, settle=0.4, hold=0.25)
    send_raw(ser, CTRL_D, settle=1)


# ------------------------------------------------------------------- scenes
#
# Each scene is (name, function) and must stand on its own - every one
# starts by putting itself in the directory it needs, so --scene can
# rehearse any of them in any order.  The bench.bas grains benchmark is
# deliberately absent: four rounds of thirty silent seconds is two
# minutes of video in which nothing happens.


def sc_boot(ser):
    """From the bootdev: prompt to a shell."""
    # The board has usually been sitting at the prompt since long before
    # the camera started, so there is nothing left to receive.  A bare
    # Return makes it print the prompt again, which is both what we wait
    # for and a tidy thing for the video to open on.
    #
    settled_on(ser, send_raw(ser, b"\r", settle=1.5), "bootdev:")
    beat(2)
    settled_on(ser, type_line(ser, "hdb2", settle=8), "login:")
    beat(2)
    type_line(ser, "root", settle=3)
    beat(3)


def sc_system(ser):
    """A real Unix, not a monitor program."""
    type_line(ser, "banner PC3", settle=2); beat(3)
    type_line(ser, "uptime", settle=2); beat(2)
    type_line(ser, "free", settle=2); beat(3)
    type_line(ser, "df", settle=2); beat(3)
    type_line(ser, "ps", settle=2); beat(3)
    type_line(ser, "ls /usr/bin", settle=3); beat(5)
    clear_screen(ser)


def sc_c(ser):
    """A C compiler that runs on the machine itself."""
    type_line(ser, "cd /root", settle=1)
    heredoc(ser, "hello.c", [
        '#include <stdio.h>',
        '',
        'int main(void)',
        '{',
        '    int i;',
        '    for (i = 1; i <= 5; i++)',
        '        printf("%d squared is %d\\n", i, i * i);',
        '    return 0;',
        '}',
    ])
    beat(2)
    type_line(ser, "cc hello.c", settle=10); beat(1)
    type_line(ser, "./hello.bc", settle=4); beat(5)
    clear_screen(ser)


def sc_edit(ser):
    """Write BASIC, edit it in MMBasic's own editor, translate, compile, run."""
    type_line(ser, "cd /root", settle=1)
    heredoc(ser, "demo.bas", [
        "For i = 1 To 8",
        '  Print "  "; i; " cubed is "; i*i*i',
        "Next i",
    ])
    beat(2)
    # mmedit is the firmware's editor, ported.  Keywords mmbc can
    # translate are cyan and interpreter-only ones blue, which is the
    # thing worth seeing on camera - so add a line rather than just
    # opening the file, and watch it colour itself as it is typed.
    # Down to the end first: typing at line 1 would go INTO the For.
    type_line(ser, "mmedit demo.bas", settle=3)
    beat(5)
    send_raw(ser, DOWN * 3 + END, settle=1)
    send_raw(ser, b"\r", settle=1)
    beat(1)
    for ch in 'Print "  and that is compiled, not interpreted"':
        type_char(ser, ch)
    drain(ser, 0.6)
    beat(4)
    send_raw(ser, F1, settle=3)          # save and exit; it clears as it goes
    beat(2)
    type_line(ser, "mmbc demo.bas", settle=4); beat(2)
    type_line(ser, "cc demo.c", settle=12); beat(1)
    type_line(ser, "./demo.bc", settle=4); beat(5)
    clear_screen(ser)


def sc_graphics(ser):
    """Compiled BASIC drawing, against the interpreter's time for the same."""
    type_line(ser, "cd /root/cc", settle=1)
    # The first two lines of the file say what MMBasic takes to draw it,
    # so the comparison is on screen before the program runs.
    type_line(ser, "head -3 ripple.bas", settle=2); beat(6)
    type_line(ser, "mmbc ripple.bas", settle=4); beat(2)
    type_line(ser, "cc ripple.c", settle=25); beat(2)
    type_line(ser, "./ripple.bc", settle=12)
    # The picture is the point, so hold on it - and then clear before
    # anything else is typed.  Text and graphics share the framebuffer,
    # so a prompt printed now lands ON the ripple and neither is legible.
    beat(10)
    clear_screen(ser)


def sc_eclipse(ser):
    """A real application: local circumstances of a solar eclipse."""
    type_line(ser, "cd /root/cc", settle=1)
    type_line(ser, "mmbc solar_eclipse.bas", settle=6); beat(2)
    type_line(ser, "cc solar_eclipse.c", settle=30); beat(2)
    type_line(ser, "cat solar_eclipse.in", settle=2); beat(5)
    type_line(ser, "./solar_eclipse.bc < solar_eclipse.in", settle=20)
    beat(8)
    clear_screen(ser)


def sc_bbc(ser):
    """BBC BASIC - R. T. Russell's, the full modern dialect."""
    type_line(ser, "bbcbasic", settle=3); beat(3)
    type_line(ser, 'PRINT "Hello from BBC BASIC"', settle=2); beat(3)
    type_line(ser, "FOR I%=1 TO 5: PRINT I%, I%^2: NEXT", settle=2); beat(5)
    type_line(ser, "QUIT", settle=3); beat(2)
    clear_screen(ser)


def sc_forth(ser):
    """fforth - a third language, and a third way of thinking."""
    type_line(ser, "fforth", settle=3); beat(3)
    type_line(ser, ": SQUARES 6 1 DO I DUP * . LOOP ;", settle=2); beat(3)
    type_line(ser, "SQUARES", settle=2); beat(5)
    type_line(ser, "BYE", settle=3); beat(2)
    clear_screen(ser)


def sc_down(ser):
    """A clean shutdown - and the screen goes dark."""
    # Just "shutdown", and nothing before it.  It asks init for run
    # level 6 by writing /var/run/initctl, and init runs /etc/rc.halt,
    # which does the syncing and unmounting properly.  Doing that by
    # hand FIRST breaks it: a root already remounted read-only cannot
    # take the initctl write, and reboot.c then falls through to
    # uadmin(A_REBOOT) - so the machine restarts instead of stopping,
    # which is exactly what the first dummy run did.
    type_line(ser, "shutdown", settle=15, hold=1.5)
    beat(4)


SCENES = [
    ("boot",     sc_boot),
    ("system",   sc_system),
    ("c",        sc_c),
    ("edit",     sc_edit),
    ("graphics", sc_graphics),
    ("eclipse",  sc_eclipse),
    ("bbc",      sc_bbc),
    ("forth",    sc_forth),
    ("down",     sc_down),
]


# --------------------------------------------------------------------- main

def main():
    global FAST, DRY, DRY_SECONDS
    args = sys.argv[1:]

    if "--list" in args:
        # Walk the scenes without a board, adding up typing time, reading
        # pauses and settle times, so the number is the script's own
        # rather than a guess that goes stale as the scenes are edited.
        DRY = True
        total = 0.0
        for name, fn in SCENES:
            DRY_SECONDS = 0.0
            fn(None)
            doc = (fn.__doc__ or "").strip().splitlines()[0]
            print("  %-9s %4.1f min   %s" % (name, DRY_SECONDS / 60.0, doc))
            total += DRY_SECONDS
        print("\n  %d scenes, about %.0f minutes at %.0f char/min."
              % (len(SCENES), total / 60.0, CPM))
        print("  Set DEMO_CPM / DEMO_READ to change the pace.")
        return

    FAST = "--fast" in args

    scenes = SCENES
    if "--scene" in args:
        want = args[args.index("--scene") + 1]
        scenes = [s for s in SCENES if s[0] == want]
        if not scenes:
            raise SystemExit("no scene %r - try --list" % want)
    elif "--from" in args:
        want = args[args.index("--from") + 1]
        names = [s[0] for s in SCENES]
        if want not in names:
            raise SystemExit("no scene %r - try --list" % want)
        scenes = SCENES[names.index(want):]
    if "--no-down" in args:
        scenes = [s for s in scenes if s[0] != "down"]

    print("# port %s, %.0f char/min (%.0f ms/char)%s" %
          (PORT, CPM, CPS * 1000,
           ", FAST (no reading pauses)" if FAST else ""))

    with serial.Serial(PORT, BAUD, timeout=0.1) as ser:
        time.sleep(0.3)
        ser.reset_input_buffer()    # anything here is from before the take
        for name, fn in scenes:
            print("\n\n########## %s ##########\n" % name)
            fn(ser)
        print("\n\n# done")


if __name__ == "__main__":
    main()

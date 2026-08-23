"""Flash a kernel to a running PC3, the whole sequence, checked.

  python reflash.py [path/to/fuzix.uf2]

Defaults to ../build/fuzix.uf2.  FZPORT names the console port as
everywhere else in this directory.

WHY THIS EXISTS.  The steps were folk knowledge and got improvised, and
improvising board commands over a serial console is how a machine ends
up halted with a dirty filesystem.  Everything the sequence needs to
know is here instead:

  0. LOG IN FIRST, and prove it.  At `login:` whatever you send is
     taken for a USERNAME and the session then sits at `Password:`
     eating everything after it, so a command sent to a board that is
     not at a shell prompt does nothing and says nothing.
  1. SYNC, and then make the root READ-ONLY.  `picoctl flash` calls the
     SDK's reset_usb_boot() and that is an immediate reset - the kernel
     does not unmount anything on the way out.  So whatever state the
     filesystem is in when you run it is the state it comes back in,
     and a mounted-dirty root means fsck on the next boot.  A remount
     read-only is the ONLY thing that prevents that.
  2. Step 1 has TWO SPELLINGS.  `mount` grew -n and -r late; a card
     whose mount predates them has a separate `remount` command that
     does the job, so both are tried before giving up.  If neither
     works that is not a reason to skip the check - it is a reason to
     SAY SO and let the operator decide, because the cost is a
     several-minute fsck they were not expecting.
  3. picoctl flash, then wait for the BOOTSEL volume to appear - it is
     the drive with INFO_UF2.TXT on it, whatever letter Windows gave
     it this time.  Never hard-code a letter; it moves.
  4. Copy the .uf2.  The board reboots itself the moment the copy
     completes, which is also how you know it took: the drive vanishes.
  5. Wait for the console to come back, log in AGAIN, and hand over at
     a shell prompt - not at the login prompt, which looks the same
     from here and is not the same thing at all.

Board commands here are one plain command each: no pipes, no quotes,
no $.  PowerShell eats quotes on the way to a native exe and expands $
before python ever sees it, and the shell on the far end then reads
what is left as a pipeline.  A `grep -E "shut|halt"` once became a
pipeline whose second stage was `halt`.
"""
import os
import string
import sys
import time

import fzport

UF2_DEFAULT = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                           os.pardir, "build", "fuzix.uf2")


def drives_with_uf2():
    """Every drive letter that looks like an RP2350 in BOOTSEL."""
    out = []
    for d in string.ascii_uppercase:
        try:
            if os.path.exists("%s:\\INFO_UF2.TXT" % d):
                out.append("%s:\\" % d)
        except OSError:
            pass
    return out


def read_until(ser, want, timeout, echo=True):
    """Read until one of `want` appears; return everything seen."""
    end = time.time() + timeout
    buf = ""
    while time.time() < end:
        n = ser.in_waiting
        chunk = ser.read(n if n else 1)
        if not chunk:
            continue
        text = chunk.decode("latin-1")
        buf += text
        if echo:
            sys.stdout.write(text)
            sys.stdout.flush()
        for w in want:
            if w in buf:
                return buf
    return buf


def send(ser, line, wait=2.0):
    """One command, echoed back by the tty, answered at a prompt."""
    ser.write((line + "\r").encode())
    ser.flush()
    return read_until(ser, ["# "], wait)


def last_prompt(buf):
    """Which of the three prompts the board is sitting at, if any."""
    best, name = -1, None
    for k in ("login:", "Password:", "# "):
        i = buf.rfind(k)
        if i > best:
            best, name = i, k
    return name


def login(ser, tries=4):
    """Get to a SHELL PROMPT, and say so honestly if it cannot.

    Sending anything at `login:` types it as a USERNAME, and the
    session then sits at `Password:` swallowing everything sent after
    it.  This used to fire and forget - write "root", wait for "# ",
    ignore whether it arrived, print "-- up --" - so a login that
    raced the boot messages handed back a board that looked ready and
    ate the next command as a username (2026-08-23).  Nothing here may
    report success it has not seen.
    """
    ser.write(b"\r")
    ser.flush()
    buf = read_until(ser, ["login:", "Password:", "# "], 4.0)
    for _ in range(tries):
        p = last_prompt(buf)
        if p == "# ":
            return True
        if p == "login:":
            ser.write(b"root\r")        # no password on this machine
        else:
            ser.write(b"\r")            # Password:, or nothing yet
        ser.flush()
        buf = read_until(ser, ["login:", "Password:", "# "], 8.0)
    return last_prompt(buf) == "# "


def rejected(out):
    """Did the board refuse the command outright, rather than run it?"""
    low = out.lower()
    return ("illegal option" in low or "usage:" in low
            or "not found" in low or "no such file" in low)


def main():
    args = [a for a in sys.argv[1:] if a != "--dirty-ok"]
    dirty_ok = "--dirty-ok" in sys.argv[1:]
    uf2 = args[0] if args else UF2_DEFAULT
    if not os.path.exists(uf2):
        sys.exit("no such file: %s" % uf2)
    size = os.path.getsize(uf2)

    already = drives_with_uf2()
    if already:
        # The board is already sitting in BOOTSEL - halted, or the
        # operator put it there.  Nothing to prepare; just write.
        print("BOOTSEL volume already present at %s" % already[0])
        return copy_and_wait(uf2, size, already[0])

    port = fzport.port_name()
    print("preparing %s" % port)
    ser = fzport.open_port(timeout=0.2)
    time.sleep(0.3)
    ser.reset_input_buffer()
    # A board sitting at a login prompt is the normal state after a
    # reboot, so log in rather than making the operator do it first -
    # and do not go near `sync` and `picoctl` until there is a shell to
    # run them in, or they are typed into a login prompt instead.
    if not login(ser):
        sys.exit("no shell prompt on %s - the board may be sitting at "
                 "login: or Password:; clear it with ctrl.py 0d.  "
                 "Nothing was flashed." % port)
    send(ser, "sync")
    send(ser, "sync")

    # The read-only remount, and an HONEST report when the card cannot
    # do it: a fsck on the next boot is the operator's time, not ours
    # to spend silently.
    #
    # TWO SPELLINGS, because the cards differ: `mount` grew -n and -r
    # later, and a card whose mount predates them has a separate
    # `remount` command that does the job perfectly well.  Probing for
    # the modern spelling alone declared such a card incapable and
    # threatened the operator with an fsck it did not need
    # (2026-08-23, COM17).
    out = send(ser, "mount -n -r /dev/hda2 /", 4.0)
    if rejected(out):
        out = send(ser, "remount -n / ro", 4.0)
    readonly = not rejected(out)
    if not readonly:
        print("\n*** this card can neither mount -n -r nor remount.")
        print("*** Flashing now WILL leave the filesystem dirty and the")
        print("*** next boot will run fsck (several minutes).")
        if dirty_ok:
            print("*** --dirty-ok given: going ahead.")
        else:
            ans = input("*** type yes to flash anyway: ").strip().lower()
            if ans != "yes":
                sys.exit("stopped; nothing was flashed")
    else:
        # Prove it, rather than trusting the absence of an error.
        out = send(ser, "touch /root/.rocheck", 3.0)
        if "read-only" not in out.lower() and "Read-only" not in out:
            print("\n*** remount reported success but the root still "
                  "accepts writes; stopping.")
            send(ser, "rm -f /root/.rocheck")
            sys.exit("stopped; nothing was flashed")
        print("\nroot is read-only")

    print("resetting into BOOTSEL")
    ser.write(b"picoctl flash\r")
    ser.flush()
    time.sleep(1.0)
    ser.close()

    end = time.time() + 30
    while time.time() < end:
        d = drives_with_uf2()
        if d:
            return copy_and_wait(uf2, size, d[0])
        time.sleep(0.5)
    sys.exit("no BOOTSEL volume appeared - put the board in BOOTSEL by "
             "hand and run this again")


def copy_and_wait(uf2, size, drive):
    dst = os.path.join(drive, os.path.basename(uf2))
    print("writing %s (%d bytes) to %s" % (os.path.basename(uf2), size,
                                           drive))
    with open(uf2, "rb") as src:
        data = src.read()
    with open(dst, "wb") as out:
        out.write(data)
        out.flush()
        os.fsync(out.fileno())
    # The board reboots as the last block lands, so the volume going
    # away IS the confirmation that it took.
    end = time.time() + 30
    while time.time() < end:
        if drive not in drives_with_uf2():
            print("written; the board is rebooting")
            break
        time.sleep(0.5)
    else:
        print("the BOOTSEL volume is still there - the copy may not "
              "have taken")

    port = fzport.port_name()
    print("waiting for the console on %s" % port)
    try:
        ser = fzport.open_port(timeout=0.2, wait=60)
    except Exception as e:
        sys.exit("the console did not come back on %s: %s" % (port, e))

    read_until(ser, ["login:", "# "], 60)
    if not login(ser):
        print("\n*** the board is up but did not reach a shell prompt.")
        print("*** It is probably at login: or Password: - clear it with")
        print("*** devtools/ctrl.py 0d before sending it any command, or")
        print("*** the command goes in as a username.")
        ser.close()
        sys.exit(1)
    print("\n-- up --")
    ser.close()


if __name__ == "__main__":
    main()

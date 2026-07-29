"""Compile a sample ON THE PC3 and diff its output against the gcc
reference captured on the host.

  python hwbuild.py sieve strs rpn ...

The sources go over with uusend.py, then the board's own cc runs the
whole chain and bcrun executes the result. This tests the compiler
running on the machine it targets, which is a different claim from
cross compiling and running the object there.
"""
import subprocess, sys, os, time, serial, difflib

PORT, BAUD = "COM11", 115200
DEV = r"\\wsl.localhost\Ubuntu\home\peter\src\FUZIX\Kernel\platform\platform-rpipico\devtools"
SAMP = r"\\wsl.localhost\Ubuntu\home\peter\src\FUZIX\Applications\CC\hosttest\samples"
REF = r"\\wsl.localhost\Ubuntu\home\peter\src\FUZIX\Applications\CC\hwtest"


def send_file(local, remote):
    r = subprocess.run([sys.executable, os.path.join(DEV, "uusend.py"),
                        local, remote, "0"], capture_output=True, text=True)
    return r.returncode == 0


def talk(cmds, limit=240):
    out = []
    with serial.Serial(PORT, BAUD, timeout=0.3) as ser:
        time.sleep(0.2)
        ser.reset_input_buffer()
        for c in cmds:
            for ch in c:
                ser.write(ch.encode()); ser.flush(); time.sleep(0.004)
            ser.write(b"\r"); ser.flush()
            buf = b""; last = t0 = time.time()
            while time.time() - t0 < limit:
                n = ser.in_waiting
                if n:
                    buf += ser.read(n); last = time.time()
                elif time.time() - last > 2.5:
                    break
                else:
                    time.sleep(0.05)
            out.append(buf.decode("latin-1", "replace"))
    return out


def clean(text, cmd):
    """Strip the echoed command and the trailing prompt."""
    lines = text.replace("\r", "").split("\n")
    out = [l for l in lines if l.strip() not in ("", "#", cmd.strip())]
    return "\n".join(out)


fails = 0
for name in sys.argv[1:]:
    src = os.path.join(SAMP, name + ".c")
    refp = os.path.join(REF, name + ".ref.out")
    if not os.path.exists(refp):
        print("%-10s no reference (%s)" % (name, refp)); fails += 1; continue
    if not send_file(src, name + ".c"):
        print("%-10s transfer failed" % name); fails += 1; continue

    # Delete the object first. Without this a cc that fails leaves the
    # previous .bc in place and bcrun happily runs it, so the suite
    # reports a pass for a build that did not happen - which is exactly
    # what it once did, against a stale cross compiled object.
    # bcrun from the PATH, not ./bcrun: the interpreter belongs in
    # /usr/bin next to cc, and a copy in the working directory is one
    # more thing to go stale or be lost.
    cmds = ["rm -f %s.bc" % name, "cc %s.c" % name, "bcrun %s.bc" % name]
    res = talk(cmds)
    build, run = res[1], res[2]
    got = clean(run, cmds[2])
    want = open(refp).read().replace("\r", "").rstrip("\n")

    if got.strip() == want.strip():
        print("%-10s PASS  compiled on the PC3, output matches gcc" % name)
    else:
        fails += 1
        print("%-10s FAIL" % name)
        if build.strip():
            print("   build said:", clean(build, cmds[1])[:400])
        for l in difflib.unified_diff(want.split("\n"), got.split("\n"),
                                      "gcc", "pc3", lineterm="", n=1):
            print("   " + l)

sys.exit(1 if fails else 0)

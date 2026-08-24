"""Transfer a BASIC program to a real MMBasic over the console and RUN it.

  set MMPORT=COM14
  python mmbrun.py prog.bas

AUTOSAVE is the transfer: it reads the console into program memory until
Ctrl-Z (FileIO.c:6205, which also takes F1 and F2).  MMBasic has no
line-numbered command-line editor, so this is the way in without XMODEM.
20 ms between lines - the console keeps up, the tokeniser between lines
does not always.
"""
import os, re, sys, time, serial

PORT = os.environ.get("MMPORT", "COM14")
GAP = float(os.environ.get("MMGAP", "0.020"))

prog = open(sys.argv[1]).read().splitlines()
ser = serial.Serial(PORT, 115200, timeout=0.2)


def wait(limit, until=b">"):
    buf = b""
    t0 = time.time()
    while time.time() - t0 < limit:
        n = ser.in_waiting
        if n:
            buf += ser.read(n)
            t0 = time.time()
            if buf.rstrip().endswith(until):
                break
        else:
            time.sleep(0.01)
    return buf.decode("latin-1")


ser.write(b"\x03")
ser.flush()
time.sleep(0.5)
ser.reset_input_buffer()

ser.write(b"AUTOSAVE\r")
ser.flush()
time.sleep(0.5)
ser.reset_input_buffer()

for l in prog:
    ser.write(l.rstrip().encode() + b"\r")
    ser.flush()
    time.sleep(GAP)

ser.write(b"\x1a")
ser.flush()
out = wait(10.0)
# "Error :" / "Error in line", not the bare word - AUTOSAVE echoes
# the program back, and a program that tests its own error handling
# has ON ERROR in it.
if re.search(r"Error\s*(:|in line)", out):
    sys.stderr.write("AUTOSAVE failed:\n%s\n" % out)
    sys.exit(1)

ser.reset_input_buffer()
ser.write(b"RUN\r")
ser.flush()
out = wait(60.0)

lines = out.replace("\r", "").split("\n")
lines = [x for x in lines if x.strip() not in ("", ">", "RUN")]
print("\n".join(lines))

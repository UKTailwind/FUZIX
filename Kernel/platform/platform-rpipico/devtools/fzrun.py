"""Send one command to the Fuzix console and read until it finishes.

  python fzrun.py "cmd" [--until=STR] [--max=SECS] [--delay=MS] [--port=COMn]

--port picks the board: COM11 is the PC3, COM14 the PC2 (whose SD is
bit-banged rather than driven by the PC3's SPI, which makes it the
control for anything that smells like an SD driver fault).

fzsh.py drains until 0.6s of silence, so a command that thinks before it
speaks returns with only its echo; listen.py then always burns its full
duration.  Between them that means sitting and waiting for output that
arrived long ago.  This waits for the machine to be ready for the NEXT
command instead - a prompt at the end of the buffer - and returns the
moment it sees one.

--until overrides the prompt set with a string of your own, for a
command whose completion the prompt does not mark (a program that leaves
you inside its own reader, say).
"""
import sys, time, serial

PORT, BAUD = "COM11", 115200

# What the machine says when it is ready for another command.  Trailing
# space matters: "# " is a prompt, "#" could be anything.
PROMPTS = ("# ", "$ ", "bootdev: ", "login: ", "assword: ", "Continue? ",
           "free list? ")


def main():
    cmd = sys.argv[1]
    until = None
    limit = 600.0
    delay = 0.012
    port = PORT
    for a in sys.argv[2:]:
        if a.startswith("--until="):
            until = a[8:]
        elif a.startswith("--max="):
            limit = float(a[6:])
        elif a.startswith("--delay="):
            delay = float(a[8:]) / 1000.0
        elif a.startswith("--port="):
            port = a[7:]

    with serial.Serial(port, BAUD, timeout=0.2) as ser:
        time.sleep(0.2)
        ser.reset_input_buffer()
        for ch in cmd:
            ser.write(ch.encode())
            ser.flush()
            time.sleep(delay)
        ser.write(b"\r")
        ser.flush()

        buf = ""
        t0 = time.time()
        last = t0
        while time.time() - t0 < limit:
            n = ser.in_waiting
            if n:
                buf += ser.read(n).decode("latin-1")
                last = time.time()
                continue
            time.sleep(0.05)
            if time.time() - last < 0.3:
                continue
            # Quiet.  Is it quiet because it is waiting for us?
            if until is not None:
                if until in buf:
                    break
            else:
                # rstrip() not rstrip("\r\n"): every prompt here ends in
                # a space, so a tail keeping it can never end with one
                tail = buf.rstrip()
                # not the echo of what we just sent
                if len(buf) > len(cmd) + 2 and \
                        any(tail.endswith(p.rstrip()) for p in PROMPTS):
                    break
        else:
            buf += "\n[fzrun: no prompt after %gs]" % limit
        sys.stdout.write(buf)
        sys.stdout.flush()


main()

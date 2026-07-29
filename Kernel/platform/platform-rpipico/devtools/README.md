# Host-side tools for driving the PC3

Windows host, `pyserial`, console on COM11 at 115200.

| script | what it does |
|--------|--------------|
| `fzsh.py`  | run shell commands: `python fzsh.py 25 "df" "ls /bin"` |
| `fz2.py`   | run BBC BASIC lines |
| `fz.py`    | probe / shell / basic, faster but less careful |
| `uusend.py`| **send a file**: uuencodes it, types it into `cat`, runs `uud` |
| `hwdiff.py`| run a bytecode program on the board and diff against a reference |
| `xsend.py` | XMODEM sender. Kept for reference only - see below |

## Sending files

`uusend.py local remote 0` is the way. It transfers 14K in about four
seconds now the console is interrupt driven.

**XMODEM does not work on this machine and `xsend.py` will not help.**
`rx` polls the console for a keypress to cancel the transfer, and here
the console *is* the serial link, so the first byte sent reads as a
cancel. The tools assume a separate port - `rx file < /dev/tty2` would
work if a second adapter were ever wired to GP0/GP1.

## Keeping the board's binaries current

`bcrun` on the card goes stale. A `.bc` built by a newer cc2 then fails
with `bad opcode at pc N`, which reads like a code generator bug and is
not one - it happened with the 64-bit opcodes. If a program that passes
on the host fails on the board with a bad opcode, resend `bcrun` before
investigating anything else:

    make -f Makefile.armm0 FUZIX_ROOT=$PWD/../.. USERCPU=armm0 bcrun
    arm-none-eabi-strip bcrun -o bcrun.stripped
    python uusend.py <path>/bcrun.stripped bcrun 0
    python fzsh.py 25 "chmod +x bcrun"

Strip everything before sending. cc0 is 74K unstripped and 10K stripped.

## Pacing

Before the console became interrupt driven, everything had to be sent
character by character with a ~25 ms delay, because `tty_interrupt()`
drained the 32-byte FIFO only on the 200 Hz tick and lost about 40% of
anything faster. That is fixed, and `uusend.py` sends flat out. The
delay argument on the other scripts is now belt and braces.

## Reflashing without touching the board

    python fzsh.py 25 "sync" "remount -n / ro" "sync"
    python fzsh.py 25 "picoctl flash"
    # board appears as drive F:
    cp fuzix.uf2 /f/

**Always remount read-only first** or the card needs an fsck on every
boot. If the kernel has panicked, `picoctl` cannot run and the board
needs BOOTSEL by hand.

After any reset the board waits at `bootdev:`. A bare CR does not
re-prompt, so silence there does **not** mean it is dead - send `hdb2`,
then `root`.

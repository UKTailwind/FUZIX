# Host-side tools for driving the PC3

Windows host, `pyserial`, console on COM11 at 115200.

| script | what it does |
|--------|--------------|
| `reflash.py` | **flash a kernel**: sync, remount read-only, BOOTSEL, copy, wait for the prompt |
| `fzsh.py`  | run shell commands: `python fzsh.py 25 "df" "ls /bin"` |
| `fz2.py`   | run BBC BASIC lines |
| `fz.py`    | probe / shell / basic, faster but less careful |
| `uusend.py`| **send a file**: uuencodes it, types it into `cat`, runs `uud` |
| `hwdiff.py`| run a bytecode program on the board and diff against a reference |
| `mmbrun.py`| **run a .bas on a real MMBasic** (`MMPORT`, default COM14) and print what it printed |
| `ab.py`    | run it there and diff against a blessed `.expected` - exit 1 on any difference |
| `xsend.py` | XMODEM sender. Kept for reference only - see below |

## A/B against the interpreter

`mmbrun.py` is the other half of every "is this what MMBasic does?"
question: it transfers a `.bas` to a PicoMite over the console and runs
it, so a translated program's output can be diffed against the
interpreter's own.

    set MMPORT=COM14
    python mmbrun.py ../../../../Applications/mmb2c/tests/order.bas

**AUTOSAVE is the transfer, not a line editor.** MMBasic has no
command-line editor that takes `10 PRINT X` - typing that is an
expression syntax error. `AUTOSAVE` reads the console straight into
program memory until Ctrl-Z (`misc/FileIO.c:6205`, which also accepts F1
and F2), and that is what this sends. 20 ms between lines: the console
keeps up with more, the tokeniser between lines does not always.

`ab.py` is the same thing as a check rather than a report:

    python ab.py ../../../../Applications/mmb2c/tests/order.bas                  ../../../../Applications/mmb2c/tests/order.expected

It strips the terminal's escape sequences and the prompt, compares line
by line, and exits non-zero on any difference - so a `.expected` blessed
this way stays blessed.

The interpreter is the authority on behaviour, but **not blindly**: it
has bugs of its own, and `tests/order.bas` found one. Thirteen of its
fourteen lines agreed on the first run; the fourteenth said MMBasic's
`REDIM PRESERVE` preserved nothing at all, which turned out to be true
and was fixed in the reference. When a line disagrees, the reference is
where to look second, not where to stop.

## Flashing a kernel

    set FZPORT=COM17
    python reflash.py ../build/fuzix.uf2

It syncs, remounts the root READ-ONLY, resets into BOOTSEL, finds the
volume by its `INFO_UF2.TXT` (the drive letter moves - never assume
one), copies, and waits for the console to come back.

**The read-only step is the whole point.** `picoctl flash` calls the
SDK's `reset_usb_boot()`, which is an immediate reset: the kernel
unmounts nothing on the way out, so a root that was mounted read-write
comes back dirty and the next boot spends several minutes in `fsck`.
Cards older than the `mount -n -r` support cannot do it; the script
says so and asks, and `--dirty-ok` answers for a script.

## One command at a time on the console

Anything sent to the board through `fzsh.py` must be **one plain
command with no pipe, no quotes and no `$`**. PowerShell strips quotes
on the way to a native exe and expands `$` before python sees them, and
the shell on the far end reads what is left. A `grep -E "shut|halt"`
once reached the board as a pipeline whose second stage was `halt`, and
halted it. Put anything with structure in a file and `uusend` it.

## Sending files

`uusend.py local remote 0` is the way. It transfers 14K in about four
seconds now the console is interrupt driven.

**XMODEM does not work on this machine and `xsend.py` will not help.**
`rx` polls the console for a keypress to cancel the transfer, and here
the console *is* the serial link, so the first byte sent reads as a
cancel. The tools assume a separate port - `rx file < /dev/tty2` would
work if a second adapter were ever wired to GP0/GP1.

## Keeping the board's binaries current

`mmbc` goes stale the same way `bcrun` does, and for a translator change
that is the whole point of the exercise - the board has its own copy and
it is the one a program compiled ON the board goes through:

    cd Applications/CC
    make -f Makefile.armm0 FUZIX_ROOT=$PWD/../.. USERCPU=armm0 mmbc
    arm-none-eabi-strip mmbc -o mmbc.stripped
    python uusend.py <path>/mmbc.stripped mmbc.new 0
    python fzsh.py 25 "mv mmbc.new /usr/bin/mmbc"
    python fzsh.py 25 "chmod +x /usr/bin/mmbc"

165K takes about 140 seconds. The toolchain is in **`/usr/bin`**, not
`/bin`.

**And the headers go with it.** The `mmb_*.h` families are compiled INTO
the program, so a new member in one of them is not in the translator at
all - the board's copy lives in **`/usr/lib/cc/include/`**:

    python uusend.py <path>/mmb_math.h mm.h 0
    python fzsh.py 25 "mv mm.h /usr/lib/cc/include/mmb_math.h"

Forget this and the symptom points the wrong way: `cc` compiles happily
and `bcrun` says **`no runtime function "mmg_shift"`**, which reads like
a missing entry in bcrun's table and is really a stale header - the
function was never found to inline, so it was left as an external.


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

After it reboots it stops at `bootdev:` and then at `login:`, and
`fzsh.py` can answer neither - it waits for `# `. `fzboot.py` sends one
line to whatever prompt is there:

    python fzboot.py hdb2 ; python fzboot.py root

From BOOTSEL the board appears as a drive labelled `RP2350`
(`INFO_UF2.TXT` confirms it). **The letter is not fixed** - it has been
F: and H: on the same machine, so find it rather than assuming:

    Get-Volume | Where-Object { $_.FileSystemLabel -like "RP2*" }

Copy the uf2 and it reboots itself:

    Copy-Item <path>\Kernel\platform\platform-rpipico\build\fuzix.uf2 F:\

The drive disappearing is the success signal. Flash the image the board
was already running unless there is a reason not to - the build tree
one, not `Images/rpipico/fuzix.uf2`, which is the v0.1 release.

## Do not send a command that can block on the console

The console *is* the only link, so anything left reading stdin eats
every command sent afterwards and the board looks dead while being
perfectly healthy. `sh -c 'cat 1<> /tmp/z'` did exactly that here - the
Fuzix shell has no `1<>`, so `cat` ran with stdin on the console.

Recovery is a raw `\x03`, which is why it is worth having a script that
can send control characters rather than only whole lines.

After any reset the board waits at `bootdev:`. A bare CR does not
re-prompt, so silence there does **not** mean it is dead - send `hda2`,
then `root`.

# Migrating retic.bas from the WebMite to the PC3

This directory is Geoff Graham's reticulation controller V1.3 — a
real, shipped WebMite product — ported to the Pico Computer 3 and
compiled with mmbc/cc.  It is the acceptance application for the WEB
family (PLAN-web.md §12), and this file is the worked example behind
the user manual's "Migrating a WebMite program" section: **a compiler
is not an interpreter** — minor source changes are acceptable where
the compiled world genuinely differs, provided every one is written
down.  Every changed line in retic.bas is marked `'PC3:`.

## 1. Machine setup replaces saved options

The WebMite stores options in flash; the PC3 is a Unix machine and
the same facts live in files and statements:

| WebMite | PC3 |
|---|---|
| `OPTION WIFI "ssid", "password"` | `/etc/wifi.conf`, joined at boot by wifi(8) |
| `OPTION TCP SERVER PORT 80` (saved) | `WEB TCP SERVER PORT 80` — one statement added at the top of the program |
| `OPTION TELNET CONSOLE ON` | not carried; the serial console is the console |
| `Option Autorun On` | accepted and ignored; autorun is an rc line |

## 2. I/O pins

The PC3's I/O header brings out GP0–GP7, GP26 and GP34–GP46
(everything else belongs to the board).  The port keeps the
program's structure and remaps:

| Function | WebMite | PC3 |
|---|---|---|
| Valves 1–8 | GP20,GP19,GP5,GP4,GP3,GP2,GP1,GP0 | GP0–GP7 in order |
| Master valve/pump | GP21 | GP34 |
| Status LED | GP28 | GP35 |
| Run LED | GP27 | GP36 |
| Abort button | GP22 | GP37 |
| Rain sensor | GP26 | GP26 (unchanged) |
| Flow meter | GP18 (counting) | — not available, see below |

## 3. Flow metering is disabled

MMBasic's `Option Count` pins count edges in hardware and
`Pin(Flow)` reads **and zeroes** the count.  The PC3 kernel does not
have a counting-input facility yet (PLAN-web.md §12.3), so the flow
sampling block in RunValve is commented out along with
`Option Count` and `SETPIN Flow, CIN`.  The application degrades
gracefully: leave the "Flow detection" checkbox OFF on the setup
page (the page now says so) and none of the surrounding code runs.

## 4. Email goes through Gmail

SendGrid is unusable and SMTP2GO's free tier is gone, so MailSend's
two provider branches collapse to one proven Gmail sequence:
implicit TLS on port 465 (587/STARTTLS exists on neither firmware),
`EHLO` sent directly — the 220 greeting is discarded by the first
REQUEST's drop-before-write, on both firmwares — then `AUTH LOGIN`,
base64 of the full Gmail address, base64 of a 16-character App
Password (2-Step Verification on the Google account, then
myaccount.google.com/apppasswords, spaces removed).

Details that mattered against the real server:
- `MAIL FROM:<addr>` / `RCPT TO:<addr>` with angle brackets.
- Gmail's acceptance line is `250 2.0.0 OK …` — neither of the old
  providers' `250 Ok`/`250 OK` spellings matches it.
- `WEB TLS CA "/etc/ca.pem"` before the OPEN makes the session
  verified (Gmail chains to GTS Root R1, in the shipped bundle).
- The setup page's "username" field is now the Gmail address and
  the "password" field the App Password (the variables and the
  settings.dat format are unchanged); the SendGrid API-key field is
  gone.

## 5. One line for a function no reference holds

`ConnData()` (in the 404 path) appears in no WebMite source or
manual we hold — retic V1.3 targets a newer firmware.  The request
text is already in `b()` at that point, so the one line asks `b()`
instead.

## 6. Paths

`A:/settings.dat` works **unchanged** — the runtime maps drive
prefixes (`A:/x`, `A:\x`, `A:x`) onto the plain name, resolved
against the directory the program runs from.  The only edit is the
page names: `"/index.html"` became `"index.html"`, because a BARE
leading slash is a real absolute path on this machine
(`/etc/ca.pem` above depends on exactly that) — on the WebMite it
meant the drive root.

## 7. Statements with changed meanings (both deliberate)

- `WATCHDOG n` is accepted and **does nothing**, with a
  translate-time warning saying so.  The RP2350 watchdog belongs to
  the kernel on a multi-process machine; wedge recovery is a
  restart loop in an rc script.
- `CPU RESTART` re-executes the program (argv[0]) instead of
  rebooting the processor — which is what the statement is *for*
  where retic uses it: start over because the network never came
  up.  It re-execs by the path the shell used, so run the program
  from its own directory.

## 8. Source fixes a compiler requires

MMBasic's expression evaluator silently ignores trailing text after
a complete expression, and its pre-scan tolerated a missing
`End Sub`.  V1.3 ships with four stray `)` (lines marked in the
source) and `GetDateTime` ending in `Exit Sub` with no `End Sub`.
MMBasic ran it; a compiler refuses it.  All five are one-character
fixes, marked `'PC3:`.

## 9. Building and running

The pages live next to the program (the `{expr}` substitutions are
compiled INTO the program at translate time, matched by text at run
time, so a page edited on the card keeps working as long as its
expressions all appear somewhere in the compiled set):

    cd /root/retic          # retic.bas + the three .html pages
    mmbc retic.bas -o retic.c
    cc -o retic retic.c
    ./retic

Both translators produce byte-identical C for the whole 49 KB
program, three pages and ~90 substitutions included.

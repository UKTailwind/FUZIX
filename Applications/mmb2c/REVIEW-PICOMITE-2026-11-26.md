# PicoMite changes since the 11-13 review: what is missing

This review covers PicoMite `388ad66..0dc2553` (2026-11-14 to 2026-11-26, V7.00.00 and V8.00.00b1 to b3): 89 commits,
69 of them touching sources. They were checked against FUZIX `pc3` at `8bbdd2b2b` and pc3host `3eafc28`. The earlier
reviews' open rows were checked again, and `fcc/mkstatus.py` was re-run against today's `AllCommands.h`. The format
follows `REVIEW-PICOMITE-2026-11-13.md`, and the decisions in both earlier files still stand.

**Targets:**
- **SHARED**: FUZIX code that both the board and pc3host compile (mmb2c.py + mmbc, the runtime, mmbedit, the utils).
- **BOARD**: the kernel or hardware only.
- **HOST**: pc3host only.

Every translator change goes into both mmb2c.py and mmbc, and cgate must stay byte-identical.

**Scope rule** (Peter, 2026-10-24): mmb2c translates programs that already run under MMBasic. Rows cover what a
correct program sees: a wrong result, a crash, or valid code refused. Error messages and error order are out of scope.

**Paths:** **M** = `Applications/mmb2c`, **K** = `Kernel/platform/platform-rpipico`.

**How each row was checked:** **R** = translated (and where it says so, run) on pc3host; **r** = by reading the code.

## Done on 11-26 (FUZIX c9993b448, 8bbdd2b2b; pc3host 3eafc28)

- **cb59e5e, 125d09a: FRAMEBUFFER WARPH, WARPV, AFFINE and SPAN.** In both translators and the runtime, the pixels in
  K/display_warp.c, run by pc3d. The PicoMite's warp_test.bas and affine_test.bas pass unmodified on Linux and Windows.
- **f9fc53c: BLIT RESIZE**, with b3's exact stepping (it was not translated at all).
- **The VGA and HDMI framebuffers on a PC:** CREATE 2, LAYER TOP, live L and T, MERGE of 2 over F (Peter: both models).
  This closes **10-21 #23** (CREATE 2, a bare CLOSE closing all, CLOSE 2 and T, WRITE L kept on CLOSE F, MODE clearing
  the layer) and **#24** for the firmware's model (MERGE ORs in MODE 1, colour 0 by default), and the 10-21 ADD row for
  buffer 2 in BLIT and TILEMAP DRAW (R).
- **MATH CLAMP** (10-21 ADD), and MATH SCALE and ADD between float and integer arrays.
- **PIXEL() under MAP:** fixed colours for each index, as ReadBuffer16's colours[]; SAVE IMAGE keeps the screen's.
- **MAP SET and MAP RESET wait for the frame on a PC**, as on the firmware and the board.
- **4b7647d: Bas/warp_demo.bas runs unchanged** in all eight scenes, on Linux and Windows.

## Decisions wanted

| Item | Question | Recommendation |
|---|---|---|
| **SQL and SQL(** (96ba215) | SQLite with the CMM2's SQL command and function, on the PicoMitePC3 since b3. Bring it in? On pc3host it is SQLite in bcrun (about 1 MB more). On the board, SQLite in every bcrun does not fit; it would be a helper program, as loadimage is, with SQL( answered over a pipe. **L** either way. | pc3host first, if at all. Category 3 until decided. |
| **MM.DEVICE$ on pc3host** | It is "Fuzix" on both machines (M/mmb_runtime.c:3165), so that every "is this VGA or HDMI?" test answers no. pc3host now has the VGA and HDMI framebuffers and live layers, so on a PC those paths would work: picofrog's and robots' HDMI branches, clip_test.bas's per-mode run. Answer "PicoMitePC3" on a PC? The board's answer must stay no. | Your call. A PC-only answer splits the machines; keeping "Fuzix" costs only the HDMI paths. |
| **MM.VER** | 7.000059 (RC9's, M/mmb_runtime.h:1048). The firmware is 8.000003 and pc3host has V8's commands, so a program testing `MM.VER >= 8` before WARPH skips it. | 8.000003 now (**S**); keep it with the firmware's betas. |
| **COM ports** | `OPEN "COM1:..." AS #n` is refused (M/mmb2c.py:10720). On the board they would be the UART ttys; on a PC, /dev/ttyUSB* and COMn. **M**. | Worth having on pc3host for real I/O, with phase 8. |
| **USER and USER(, RUN "x.app"** (96ab930, 1a12349 and the loader family) | The PicoMitePC3's way to run C. FUZIX runs C natively through cc, so there is nothing to bring. | Category 5 (not applicable). |

## SHARED: translator and runtime

| # | PicoMite | What changed | Ours | Change, size |
|---|---|---|---|---|
| 1 R | ffb1727 | FORMAT$ gives what C's printf gives: correctly rounded to any precision, ties to even, `inf`/`nan`. | **Wrong results** on correct programs (R, against printf): `FORMAT$(100000,"%g")` = `1e+05` (wants `100000`); `FORMAT$(0.375,"%.2f")` = `0.37` (wants `0.38`, under the old firmware too); `FORMAT$(1E22,"%f")` = `-9223372036854775808.0000`; `%.17g` stops at 15 digits (`0.1`, wants `0.10000000000000001`); ties round up (`0.125` → `0.13`, wants `0.12`); infinity is `INFe-2147483647`. M/mmb_runtime.c's mm_format uses mm_float_to_str, the old FloatToStr. | Port format_digits (MM_Misc.c). It uses newlib's big-number routines, which FUZIX does not have, so a small bignum of our own (about 150 lines). **M**. Highest value: FORMAT$ is common. |
| 2 R | 264ccde, 9424dcf, c2d8f2c | PLAY MODSAMPLE gains a rate (150-500000), a pan (-100 to 100) and LOOP; PLAY MODSAMPLE CHANGE channel [, vol] [, rate] [, pan]; PLAY MODSAMPLE STOP [channel]. Effects mixed after the music's stereo separation. | Only `sample, channel [, volume]` (mmbc_stmt.c:1795, M/mmb_play.h:296); every new form is refused. Bas/doppler.bas does not translate. | Translators, a longer record to playmod, and playmod's hxcmod effect slots (rate, loop, change, stop, pan). **M** |
| 3 R | (FUZIX #45, open) | `DIM AS INTEGER a, b` (the type before the names), and LOCAL and STATIC the same. | Refused; under OPTION DEFAULT FLOAT the issue reports a wrong answer. | As the issue says. **S** |
| 4 R | 9749c83 (10-21 ADD) | A whole structure assigned into a nested member: `r.a = q`. | Refused, saying "the firmware overruns memory here", which 9749c83 fixed. Passing `r.a` as an argument works (R). | Copy the member's bytes, as a whole-structure assignment does. **S** |
| 5 r | 37e7af5 (10-21 #25) | BLIT FRAMEBUFFER and FLASH with w or h below 1 draw nothing. | Refused (M/mmb_blit.h:923-925). A computed width of 0 stops the program. | Return quietly. **S** |
| 6 R | 6f263f7 (Sept; missed by both reviews) | MM.INFO(BBC BUFFER [channel]): a channel's free note slots, or the fewest of the four (b9da068). | Not translated. A program that paces its BBC sound queue does not translate. | The kernel's ADVAL has the counts (PICOIOC_ADVAL); pc3d's dispatch.c needs the same. **S** |
| 7 R | b2ded8c (10-21 ADD) | BASE$(base, n [, digits]). | Not translated ("BASE$ is not translated"). mm_base exists (M/mmb_runtime.c). | A BUILTINS row each. **S** |
| 8 R | 94c7a0f (10-21 ADD) | MM.INFO(TELNET). | Not translated. | Constant 0. **S** |
| 9 R | 194a843, c271835 (10-21 ADD) | PLAY WAV/FLAC/MP3 on a directory; PLAY NEXT and PLAY PREVIOUS. | PLAY NEXT/PREVIOUS refused; a directory fails in the background. | **M** |
| 10 R | dba7552 (10-21 ADD) | TILEMAP LOAD file$ (a `.map`). | "unknown TILEMAP form". | **S-M** |
| 11 r | 9749c83 (10-21 #12) | JSON$'s new path walker: a leading `[n]`, a trailing `[n]`, the final key case-insensitive. | The old walker (M/mmb_json.h:302-410). | Port it and re-bless the JSON tests. **M** |
| 12 r | 4d91e6c (10-21 #31) | LOAD PNG crops an image larger than the screen, takes `ximage, yimage`, and decodes streaming. | "Image too large"; the arguments are passed but not used. | Port the streaming upng and LoadPNG's crop. **M** |
| 13 r | (11-13, sprites) | #91 edgecollision, #89 swap, #93 LOADBMP after a failed decode. | Unchanged since 11-13 (M/mmb_sprite.h:271-320, :996-999, :1536-1550). The first can write past `coll[]`. | As the 11-13 file says. **M** |
| 14 R | 043f914, 1179345 (10-21 ADD) | #INCLUDE and #DEFINE. | Refused by name (#COMMENT and MMDEBUG are done). | **M** per translator |
| 15 R | 698a5da, bd1a965 (10-21 ADD) | WEB SCAN. | "not implemented yet". Board: NETIOC_SCAN; pc3host: the OS's scan, or an empty list. | **M** |

**The coverage scan.** `fcc/mkstatus.py` against today's AllCommands.h finds five names `fcc/catmap.py` does not
classify. Proposed: **SQL, SQL(** to category 3 (above); **User, User(** to category 5; **EofLocLof(** to category 4,
with base$( and SChange$( (b2 folded EOF(, LOC( and LOF( into one token; all three are still translated, but now
drop out of the "translated" count, as Ray( did into sprite(). Regenerate COVERAGE-STATUS.md once these are in.

## BOARD

| # | PicoMite | What changed | Ours | Change, size |
|---|---|---|---|---|
| 16 r | 83ca493 | USB host: an RX timeout stops EPX only when EPX has a transaction on the wire; hub over-current and disabled-port recovery (a new hub.patch); bus error counts. A key held as its keyboard goes no longer repeats. | K/usb/hcd_rp2040.patch handles RX_TIMEOUT its own way (a grace window, TinyUSB #3533), and there is no hub.patch. The key repeat is already stopped (K/usbkbd.c:826). | Port hub.patch; compare the two RX_TIMEOUT treatments on hardware (the firmware's report is not reproduced). **M** |
| 17 r | 7bc8ba4, aa0bda9, 544be7b, 7ffee2a | The WebMite #153-#160 classes. | **FUZIX #46-#52, filed 11-15, all still open.** #51 is SHARED (mmb2c's SIGPIPE and 5 s send limit); the rest are the kernel's lwIP glue. | As filed. |
| 18 r | (11-26) | — | The board's kernel does not dispatch GFXIOC_WARP or GFXIOC_RESIZE, nor GETPIXEL's FIXED flag: a program there is told "FRAMEBUFFER WARPH is not available on this machine", and PIXEL() still reads through the live palette. | misc.c: read the arrays from user memory; RESIZE needs scratch for an overlap; pass `(v >> 24) & 1` to display_gfx_pixel_read. **S-M** |

## Already matches (checked)

1cc4135 PAUSE beyond 36 minutes: a 64-bit microsecond deadline (r) · 6a4161d WEB TCP INTERRUPT 0 switches it off
(int_target, r) · cd31f0b shapes off the screen: clip_test.bas, 227 checks in each of MODE 1, 2 and 3, 0 wrong (R) ·
7e33bee: the web server's request path never touches the error state (r) · 8486f06: altcp_output after each write
(K/net_cyw43.c:1010) and no per-chunk wait in mmg_webs_file (r) · 9f20b28: tcp_died clears the pcb for every error,
ERR_ABRT included (K/net_cyw43.c:727, r) · eebcb7b: a lost RTC time is caught at boot (OSF and a year before 2025),
and the hourly read takes only the seconds (K/ds3231.c:163-196, :311-325, r) · 52ab0ce: mmbedit's exit restores no
saved cursor (r) · 7a5a63a: LIBRARY LOAD is translated into the program, so MM.CMDLINE$ is unaffected (r).

## N/A

- **The loader and its apps:** 74f51e3, 96ab930, 214d17f, 5382abc, 5785089, 1a12349, 0b53311, d7b695b, e0dd818,
  de37772, a818225, 7770b0d, a8149ed; NES, Doom, MicroPython, ScummVM (4da0064), the BBC Micro (de551e9), sql.app
  (4badc9f).
- **Serial, which mmb2c does not translate** (decision row above): 9a0afdd, db2c2d7, f44a7cf, 1b58adb.
- **TURTLE, not translated** (category 3, decided "not imminent"): 5cc8dda.
- **The firmware's own machinery:** 7384d21 and 02b4e7c (tokens and library hashes), 20d3a78 and d68c9e6 (slots and
  CHAIN), a57f800 and aaa8567 (flash timing), f1dd113 (576p, CPU limits), ac48ce1 (GUI cursor), 1751e3c (a USB
  diagnostic), 420dc65 and 0dc2553 (flash offsets), fac0cfa (handlers outliving a program: a program here is a process).
- **mmb2csub only:** ad8da29.
- **Error paths:** 7378c06.
- **Documentation, release notes and merges:** the rest.

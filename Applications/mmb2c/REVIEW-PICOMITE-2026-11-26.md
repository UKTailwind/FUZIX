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
| 1 R | ffb1727 | FORMAT$ gives what C's printf gives: correctly rounded to any precision, ties to even, `inf`/`nan`. The default precision stays 4, not C's 6, and a precision past 30 is 30. | **Done 11-26.** It gave wrong results on correct programs (R): `FORMAT$(0.375,"%.2f")` = `0.37` (wants `0.38`; the old firmware said `0.37` too, by the same FloatToStr arithmetic); `FORMAT$(1E22,"%f")` = `-9223372036854775808.0000` (wants `10000000000000000000000.0000`); `%.17g` stopped at 15 digits (`0.1`, wants `0.10000000000000001`); ties rounded up (`0.125` gave `0.13`, wants `0.12`); infinity was `INFe-2147483647`. Corrected from the first version of this row: `FORMAT$(100000,"%g")` = `1e+05` is right, because the default precision is 4. | M/mmb_runtime.c's mm_format now works the digits out exactly (mm_fmt_digits, a port of format_digits on a small bignum of its own, as FUZIX has no newlib). Checked against glibc's printf on 864,000 random cases, 0 wrong (the old code: 86,799 wrong of 288,000); 8 of 8 planted faults caught. M/tests/format8.bas. |
| 2 R | 264ccde, 9424dcf, c2d8f2c | PLAY MODSAMPLE gains a rate (150-500000), a pan (-100 to 100) and LOOP; PLAY MODSAMPLE CHANGE channel [, vol] [, rate] [, pan]; PLAY MODSAMPLE STOP [channel]. Effects mixed after the music's stereo separation. | **Done 11-26.** Every new form was refused, and Bas/doppler.bas did not translate. Now both translators take them all (empty arguments, LOOP after any argument from the channel on); mmb_play.h sends them in version 2 of the player-control record (LOOP a bit of the channel, CHANGE and STOP records of their own); playmod makes ModSampleCommand's hxcmod calls, and utils/hxcmod.c and .h are the firmware's third_party_mod copies, unchanged. pc3host's e2e test tests/e2e/modsample.bas checks playmod's calls for 15 statements against the firmware's, on Linux and Windows (R); M/tests/modsamp8.bas the translation. | Board: playmod builds; not heard on the board yet. |
| 3 R | (FUZIX #45) | `DIM AS INTEGER a, b` (the type before the names), and LOCAL and STATIC the same. | **Done 11-26.** It was refused, or under OPTION DEFAULT FLOAT a wrong answer. Both translators step over the AS as cmd_dim does, and take a TYPE's name as the list's type too (`Dim point a, b`, `Dim AS point a, b`), as CheckIfTypeSpecified does. The issue's three programs give MMBasic's output (M/tests/dimas45a-c.bas). | Close #45. |
| 4 R | 9749c83 (10-21 ADD) | A whole structure assigned into a nested member: `r.a = q`. | **Done 11-26.** Refused both ways, into a nested member and out of one (`q = r.a`), for an overrun 9749c83 fixed. Each is now a copy at the member's own TYPE, arrays of nested structures and structure parameters included (M/tests/nestset.bas); the PicoMite's StructTest.bas passes its tests 74A and 74B now. | |
| 5 r | 37e7af5 (10-21 #25) | BLIT FRAMEBUFFER and FLASH with w or h below 1 draw nothing. | **Done 11-26.** A width worked out as 0 stopped the program with "Invalid coordinates". It now draws nothing; a source off the buffer or the image is still refused (M/tests/blitw0.bas). | |
| 6 R | 6f263f7 (Sept; missed by both reviews) | MM.INFO(BBC BUFFER [channel]): a channel's free note slots, or the fewest of the four (b9da068); the queue deepened from 8 notes to 16. | **Done 11-26.** mm_bbc_free asks the kernel's ADVAL(-5) to (-8); the kernel's queues are 16 notes now (K/sound.c), so the counts are the firmware's; pc3d answers ADVAL's queue selectors too. pc3host's play.bas reads 14, 14 and 16 with two notes waiting, and 16 after PLAY STOP (R). | Board: the kernel change needs a new fuzix.uf2; not run on the board. |
| 7 R | b2ded8c (10-21 ADD) | BASE$(base, n [, digits]). | **Done 11-26.** mm_base_s, as fun_base (M/tests/base8.bas). | |
| 8 R | 94c7a0f (10-21 ADD) | MM.INFO(TELNET). | **Done 11-26.** 0: there is no telnet console. | |
| 9 R | 194a843, c271835 (10-21 ADD) | PLAY WAV/FLAC/MP3 on a directory; PLAY NEXT and PLAY PREVIOUS. | PLAY NEXT/PREVIOUS refused; a directory fails in the background. | **M** |
| 10 R | dba7552 (10-21 ADD) | TILEMAP LOAD file$ (a `.map`). | **Done 11-26.** mmt_load reads the file as tilemap_file_value does and installs the map as CREATE does, only once the whole file is read, with the firmware's messages. The PicoMite's Testfiles/TilemapLoadTest.bas passes all 24 checks unmodified on pc3host (tests/e2e/tilemap_load.bas, Linux and Windows); M/tests/tmload.bas headless. | |
| 11 R | 9749c83 (10-21 #12) | JSON$'s new path walker: a leading `[n]`, a trailing `[n]`, the final key case-insensitive. | **Done 11-26.** M/mmb_json.h walks the path as fun_json does now: keys and indexes in any order, an index read with atoi, only the last key matched regardless of case, a key over 63 characters or an index over 7 "Invalid JSON path". M/tests/jsonpath8.bas checks 25 paths against a model of the firmware's walker; jsonpath.expected changes in one place, `arr[0]` now 10 (it was empty). | |
| 12 R | 4d91e6c (10-21 #31) | LOAD PNG crops an image larger than the screen, takes `ximage, yimage`, and decodes streaming. | **Done 11-26**, but for the streaming. loadpng crops to the screen from the image offsets, adds .png, and SPRITE LOADPNG takes x, y, w, h (loadpng -s); an offset or part outside the picture is "Coordinates". The firmware streams so that a PLAY in the interpreter is not starved; here the decoder is a process of its own and the sound the kernel's, so the whole-image decode stays. Found on the way: a blank argument to any LOAD form (`LOAD IMAGE f$,,,4`) was refused by both translators; it is passed as an empty string now, which loadimage already took and loadpng and loadjpg now take as the default. pc3host's tests/e2e/pngcrop.bas checks the screen and a sprite against a 400 x 300 picture (R). | |
| 13 r | (11-13, sprites) | #91 edgecollision, #89 swap, #93 LOADBMP after a failed decode. | Unchanged since 11-13 (M/mmb_sprite.h:271-320, :996-999, :1536-1550). The first can write past `coll[]`. | As the 11-13 file says. **M** |
| 14 R | 043f914, 1179345 (10-21 ADD) | #INCLUDE and #DEFINE. | **Done 11-26.** Both translators run loader_directives before any pass: #INCLUDE puts the .inc file's lines in its place (beside the program, no nesting), and each #DEFINE changes every later line as the loader's massage() does - STR_REPLACE's whole-word, case-blind match outside quotes and comments, on the line with its blanks collapsed, the latest first, in capitals. The port was checked against the firmware's own STR_REPLACE and blank collapsing, compiled from core/MMBasic.c, on 50,000 random lines (0 differ; 4 of 4 planted faults caught), and mmbc against mmb2c.py on 300 random programs (0 differ). M/tests/incdef.bas and defspace.bas; tests/refuse/include.bas is now a missing file. | Arguments are string literals (the firmware evaluates them). Board mmbc builds; not run there. |
| 15 R | 698a5da, bd1a965 (10-21 ADD) | WEB SCAN. | "not implemented yet". Board: NETIOC_SCAN; pc3host: the OS's scan, or an empty list. | **M** |

**The coverage scan (done 11-26).** `fcc/mkstatus.py` against today's AllCommands.h finds five names `fcc/catmap.py` does not
classify. Proposed: **SQL, SQL(** to category 3 (above); **User, User(** to category 5; **EofLocLof(** to category 4,
with base$( and SChange$( (b2 folded EOF(, LOC( and LOF( into one token; all three are still translated, but now
drop out of the "translated" count, as Ray( did into sprite(). Regenerate COVERAGE-STATUS.md once these are in.
Done as proposed; base$( is translated now (row 7) and left catmap, and COVERAGE-STATUS.md is regenerated.

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

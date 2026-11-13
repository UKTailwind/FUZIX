# PicoMite changes since the 10-21 review: what to apply

This review covers PicoMite `dc2c870..388ad66` (2026-10-21 to 2026-11-13, V7.00.00RC3+ to RC9): 224 commits, 138 of them touching sources. They were checked against FUZIX `pc3` at `1282afac4` and pc3host `67dae9f`. The format follows `REVIEW-PICOMITE-2026-10-21.md`, and that file's decisions still stand.

**Targets:**
- **SHARED**: FUZIX code that both the board and pc3host compile (mmb2c.py + mmbc, the runtime, mmbedit, the utils loaders, kbd_decode.c).
- **BOARD**: the kernel or hardware only.
- **HOST**: pc3host only.

Every translator change goes into both mmb2c.py and mmbc, and cgate must stay byte-identical.

**Scope rule** (Peter, 2026-10-24, commit e4f5f8032): mmb2c translates programs that already run under MMBasic. Matching the interpreter's error paths is out of scope: message texts, which check fires first, and what a failed operation leaves behind. Rows here cover what a correct program sees, plus leaks that affect one afterwards.

**Paths:**
- **M** = `Applications/mmb2c`
- **K** = `Kernel/platform/platform-rpipico`
- **E** = `Applications/mmbedit/editor.c`

**How each row was checked:**
- **R** = verified by translating and running a test program on the host
- **r** = verified by reading the code

## Decisions (Peter, 2026-11-13)

| Item | Decision |
|---|---|
| PSRAM timing (K/psram.c) | **Make it correct**, using PicoMite's chip table (13b081e). AP Memory: 109 MHz, tCEM 8 us, tCPM 18 ns. At 378 MHz this gives divider 4 (94.5 MHz, where ours runs 126 MHz), deselect 7 (ours is 5) and rxdelay 5, so M1_TIMING = 0x61DE7504. **DONE** FUZIX 10ed2d176, read back on the board. |
| Olimex RP2350pc | **Support it, with PicoMite's detection** (225bff0). A 32 kHz clock on GP27 means a PC3. Otherwise PSRAM answering on GP47 means a PC2, and PSRAM on GP8 means the Olimex. **DONE** FUZIX 10ed2d176; boots on Peter's Olimex. |

## Fixed today

- **953a4f4 (#147), FOR or DO after a single-line IF's THEN or ELSE.** The translator side is FUZIX #44, in 947dce897 and 2e5b1b245. Both agree on FOR/DO in THEN and ELSE, nested in an outer FOR or DO, and on `IF 1 THEN IF a=2 THEN FOR…NEXT` (R).

## Status (2026-11-13, evening)

**Done and how each was checked:**
- **Rows 1-5 and 10-13**, the programs that failed to translate. Six new
  tests: cmdnamevar, optcfg, timeset, lengthexpr, restoreexpr and
  mminfodrive. They passed in make check, cgate, the PC3's own cc, and
  pc3host on Linux and Windows.
- **Rows 6-9, 15, 17 and 22, and the 10-21 carry-overs #26, #32 and #33**,
  the wrong results:
  - **#7** has a new test, draw3dhide.
  - **#8** has tests/draw3dharness.c, a model of the reference's per-face
    light. The old engine fails it with exactly the old rule's colours.
  - **#9**: websharness now has a 207-character page expression, which
    the old 160-byte buffer cut short.
  - **#15, #17 and #26**: each new PNG and BMP kind is drawn and must
    match its RGBA or 24-bit twin. This ran on pc3host's display server
    (Linux and Windows) and on the PC3 with its own utilities.
  - **#32**: the kernel's sound.c, built for the host, holds each noise
    level for exactly floor(f) samples; HEAD's held about f x 0.093.
  - **#22 and #33** were checked on the PC3. The radio comes up at
    37.8 MHz (firmware loaded, MAC read, a join answered). MODSAMPLE at
    volume 0 runs over a playing module.
- **Found on the way:** LOAD PNG had never worked in pc3host's Linux
  build. upng_pc3.c's `psram_req` used unsigned longs, 16 bytes on
  LP64, so the arena's base was never returned. It is now uint32_t, as
  in pico_ioctl.h.

**Still open:**
- The features: #16 (RLE SAVE IMAGE), #18 (SAVE PNG and Print Screen) and
  #19 (Ctrl-\, waiting on the VQUIT decision).
- #14 and #21.
- The sprites section.
- The 10-21 leftovers #12, #23-25 and #31.

## SHARED: translator and runtime

| # | PicoMite | What changed | Ours | Change, size |
|---|---|---|---|---|
| 1 R **DONE** | 1615889 | `OPTION PICOMITE` (with anything after it) is accepted and ignored. CMM2 programs carry it. | **Fatal**: "OPTION PICOMITE is not translated", so the whole program fails, even in lenient mode. OPT_CONFIG has PICO but not PICOMITE (M/mmb2c.py:5817-5827, mmbc_decl.c:1042). | Skip the rest of the statement silently. **S** |
| 2 R **DONE** | 07baeff (#149) | A command's name followed by `=` at the start of a statement is a variable: `flash = 1`, `line = 1`, `font = 3`. TIMER, DATE$, TIME$, FOR, ELSE, GOTO, GOSUB, WHILE, FLAGS, CASE, REM, RUN and DATA are excluded. | **Refused**: flash, font, colour, box, play, pixel, sort, pause, memory, cls, circle, sprite, line and mode are all commented out. Reading such a name already works. The keyword dispatch runs first (M/mmb2c.py:6853, mmbc_stmt.c:275). The comment at M/mmb2c.py:174-177 is now wrong. | Before the dispatch: a name in FW_COMMANDS or KEYWORDS (not excluded), followed by `=`, goes to do_assign (table at mmbc_tab.c:121). Add a test. **S** |
| 3 R **DONE** | 30b1503 (#103) | RESTORE takes an expression: a string names a label, a number a line. | **Refused** for `RESTORE l$`, an array element or a function (M/mmb2c.py:9605-9624, mmbc_stmt.c:4705). It worked on PicoMite before this commit too. A bare label still wins over a variable (R). | Evaluate it, then look it up in an emitted {label/line → DATA index} table: `mm_restore_name`/`_line` in mmb_data.h, plus a bcrun_mm.c entry. **M** |
| 4 R **DONE** | c016053 (#104) | DIM's LENGTH takes an expression. | **Refused**: "LENGTH takes a literal integer", even `CONST W = 8: DIM a$(3) LENGTH W`. The array is then undeclared and every later use fails (M/mmb2c.py:6114-6118, :5134-5136; mmbc_type.c:553, mmbc_decl.c:1401). | Scalar: any expression, since LENGTH is ignored for scalars by decision. Array: any translate-time constant (CONST); refuse only a run-time value. **S-M** |
| 5 R **DONE** | 05882fa, 4a506b0 | `TIME$ = n` moves the clock by n seconds. Setting the time zeroes OPTION MILLISECONDS. | `TIME$ = n`: "TIME$ = needs a string" (M/mmb2c.py:9008, mmbc_stmt.c:2857). `OPTION MILLISECONDS` is **fatal** ("not translated"). The clock is whole seconds (M/mmb_datetime.h:35-38). | `TIME$ = n` → `mm_clock_off += n`: **S**. OPTION MILLISECONDS: at least accept and ignore it (**S**); a real sub-second clock is **M**, low priority. |
| 6 r **DONE** | 4e078e5 | MM.VER of a release reads 7.000099, above its betas and RCs. | M/mmb_runtime.h:989-991 says the release "will be 7.0". MM_VERSION is 7.000055 (RC5); PicoMite is at RC9 (7.000059). | Bump to 7.000059 now and fix the comment; 7.000099 at the release. **S** |
| 7 R **DONE** | caee048 (#132) | `DRAW3D HIDE a, b` skips an object that is already hidden and goes on to b. | `mmg3d_hide` returns 1 for an already-hidden object (M/mmb_draw3d.h:971-974), and `d3_each` chains with `\|\|` (M/mmb2c.py:9372-9381), so b stays shown. `HIDE 1 : HIDE 1, 2` leaves XMIN 2 at 360; PicoMite gives 32767. | `return 0` at :974; fix the comments there and at mmb2c.py:9373. **S** |
| 8 r **DONE** | caee048 (#133) | DRAW3D LIGHT lights each face from that face's own vertex. | Every face uses the last face's direction (M/mmb_draw3d.h:451-454, :664-671). | Compute the light vector per face from `face_x_vert[facestart[si]+1]`. **S** |
| 9 r **DONE** | d3e952d | WEB TRANSMIT PAGE takes an `{expression}` of up to 253 characters. | `mm_webpg_next` truncates at `ex[160]` (M/mmb_webs.h:487, :509), while the translator keys on the full text. A long expression raises "page expression not compiled in". | `ex[256]`, and `msg[300]` with it (:528). **S** |
| 10 R **DONE** | 1a7980f (+ long-standing) | `EXISTS DIR "B:/"` and `"A:/"` give 1. | They give 0. `mm_fname_in` turns `"A:/"` into `""` (M/mmb_runtime.c:1585-1607). `CHDIR "A:/"` fails, and `DIR$("A:/*")` returns `""`. | Map an empty remainder to `"."`, and pass DIR$/FILES specs through `mm_fname`. **S** |
| 11 R **DONE** | 2abbb53 | SAVE COMPRESSED IMAGE is a synonym for SAVE IMAGE. | Syntax error (M/mmb2c.py:7304). | Accept it. **S** |
| 12 R **DONE** | 147a93d, 81f75c1 | MM.INFO$(USBDRIVE) is new. MM.INFO$(SDCARD) and MM.INFO(FONT) already existed. | None of the three translates. | USBDRIVE → "Not Configured", SDCARD → "Ready", FONT → `mm_gfont`. **S** |
| 13 r **DONE** | 0684c45 | OPTION PRINTSCREEN FORMAT. | Not in OPT_CONFIG, so a program carrying it does not translate. | Add it to OPT_CONFIG now (**S**). The behaviour comes with Print Screen (row 18). |
| 14 r | 641d043 | ON KEY k counts only a key k that arrives while ON KEY is armed. | We peek the buffer on every poll (M/mmb_int.h:512-528), so a k typed before arming fires the handler. | **M**, low priority. |

## SHARED: utilities, keyboard, editor

| # | PicoMite | What changed | Ours | Change, size |
|---|---|---|---|---|
| 15 R **DONE** | 3687a42 | LOAD PNG accepts 8-bit RGB, grey and grey+alpha, which is what PicoMite's own SAVE PNG writes. | "Invalid format, must be RGBA8888 or indexed PNG" (K/utils/loadpng.c:336, upng.c ~1380-1420). | Expand RGB8, L8 and LA8 to RGBA8 as the indexed path does. **S** (~25 lines). Porting the streaming decoder (10-21 #31) would bring this in anyway. |
| 16 r | 2abbb53 (+ cd36394, 9bdcae7) | SAVE IMAGE writes RLE4/RLE8 BMPs, with a palette of only the colours used. | Always 24-bit (K/utils/saveimage.c:186-192): 230 KB for MODE 2 and 921 KB for MODE 3. Every PC3 screen has 256 colours or fewer, and loadimage already reads RLE (loadimage.c:295-341). | Port SavePalIndex, SaveRleRow and SaveImageRLE (PicoMite misc/FileIO.c:3145-3313) over the band reader. **M**. Worth it for SD time and space. |
| 17 r **DONE** | (long-standing) | SAVE IMAGE adds `.bmp` when the name has no extension. | saveimage.c:173 uses the name as given, but loadimage adds `.bmp`, so `SAVE IMAGE "pic" : LOAD IMAGE "pic"` fails. | Copy `defext` into saveimage. **S** |
| 18 r | ec6fd02, bf5c7c0, 0684c45 | SAVE PNG (graphics/PngEncoder.c, 292 lines). Print Screen names its file `SCREEN_yyyymmdd_hhmmss`. | Absent. Print Screen is still the 10-21 ADD item. | After rows 16-17: SAVE PNG as `saveimage -p` plus a translator branch (**M**), then Print Screen with the date-time name. |
| 19 r | 5d0da8a | Ctrl-\ gives 0x1C from every keyboard; it is Print Screen's second key. | `kbd_map_code` applies Ctrl to letters only (K/kbd_decode.c:130-132). | Map Ctrl-\ to 0x1C (3 lines). This **must land with 10-21 #34** (VQUIT): until then Ctrl-\ is SIGQUIT and kills the program. **S** |
| 20 r **DONE** 8a3035697, board and pc3host `editor-pgup` | d059604 | Editor: PgUp scrolls a full screen only when `edy >= VHeight`. | E:2702 has `edy >= VHeight - 1`, so PgUp at the end of a file one line short of whole screens blanks the top row. Mark mode (E:1272) is already right. | `VHeight - 1` → `VHeight`. **S**, no risk. |
| 21 r | c6c912a | Every built-in font starts on a word boundary. | Ours are word-aligned by GCC's default (checked with nm), but nothing guarantees it, and our PEEK(WORD) raises an error on a misaligned address (M/mmb_peek.h:89-94). | `__attribute__((aligned(4)))` on K/fonts/*.h and console_font.h. **S**, insurance. |

## BOARD: kernel and USB

FUZIX's TinyUSB is 0.21.0 with three patches applied at build time (K/CMakeLists.txt:296-315). PicoMite's patches are against TinyUSB master and need porting by hand.

| # | PicoMite | What changed | Ours | Change, size, risk |
|---|---|---|---|---|
| 22 r **DONE** | d81f472 | The CYW43 gSPI is held to 40 MHz or less (a radio failed init at 50 MHz). | K/net_cyw43.c:227 divides by 100 MHz, giving divider 4 = 47.25 MHz at 378 MHz. | `(khz+79999)/80000` → divider 5, 37.8 MHz; fix the comment at :213-216. **S**, low risk. |
| 23 r **DONE** 75fc7332e | e0eba35 | Wait 100 us before re-arming each control packet to a low-speed device behind a hub. Without it, a low-speed mouse in the Pi keyboard's hub locked every USB device until a restart. | Absent: rp2040_usb.c:427-430 re-arms at once. 0.21 reads strings length-first, which means many 8-byte packets. | A hunk in usb/rp2040_usb.patch: `if (is_host && ep->need_pre && epnum==0) busy_wait_us_32(100);`. **S**. Test: plug a mouse into the Pi keyboard's hub. |
| 24 r **DONE** 75fc7332e (24 entries) | f157f93 | The endpoint pool goes from 16 to 32 entries. A full pool reports EP_FULL instead of asserting. | `ep_pool[USB_MAX_ENDPOINTS]` (hcd_rp2040.c:56): 15 usable, and every device's EP0 takes one. Two hubs plus keyboard, mouse, pad and touch come to about 16. The `assert(int_idx < 15)` at :563 is live in this Debug build. | Raise to 24-32 (+160 to +320 B of kernel RAM, which is tight: see BUILDING-PC3 §2), and give the entry back instead of asserting. **S** |
| 25 r **DONE** 75fc7332e | bb53afa / upstream #4177 (open since 10-21) | A freed endpoint keeps its `interrupt_num`. | `hcd_device_close` (:489-506) leaves it set, so a reused entry routes EP0 down the interrupt path and enumeration stops for good. buf_status (:289-299) also matches freed entries. | Port the three hunks. **S**. Matters more once #24 is in. |
| — **DONE** 75fc7332e | da2c306 / 0e7581e (open since 10-21) | The two "buf_ctrl already available" `panic()`s. | Still `fpanic` through pc3_usb_panic (usbkbd.c:474-500). | As in the 10-21 review. |

**Already right on the board:**
- fff5243 (HOST_RESUME masked, usbkbd.c:702-703).
- d2a0b65 (handler called from a thread with the IRQ off, usbkbd.c:453-455).
- **225bff0's DS3231 trap.** FUZIX is ahead here: ds3231.c:341-354 turns EN32kHz back on before detection, and :411-412 refuses a write that clears it. That is the fix PicoMite's own 11-11 note proposes.
- 0408633 (the RTC never overrides an NTP time; ds3231.c:158-192 only trims drift).

## SHARED, sprites (deferred, as in 10-21)

mmb_sprite.h is a transcription of the pre-fix Sprite.c. When the rework settles:
- Port #91's `edgecollision` first: `mms_checklimits` (M/mmb_sprite.h:271-320) runs after the pair scan and can write past `coll[MMS_MAXCOLL+1]`.
- `mms_swap` (:996-999) is pre-#89.
- `mms_loadbmp` keeps `sb->img` after a failed decode (:1536-1550). This is #93's sprite part: the next LOADBMP says "Buffer already in use".

## Carried over from the 10-21 review

The 10-21 file's status marks were not kept up to date. Checked against `git log` and the code today:
- **Done since:** #7 (659f8a5b0), #16 (81daedb39), #17 (6f4df6138), #18 (27dbe6dc3), #19 (a00262ce6, and PicoMite's #101 now agrees).
- **Out of scope** (error paths): #10, and the messages in #25.
- **Still open, affecting correct programs:**
  - **#26 DONE**: K/utils/loadimage.c:200 seeks past the whole header before reading BITFIELDS masks, so a V4/V5 BMP (GIMP's RGB565) gets its masks from the wrong place. Read them at 14+40.
  - **#32 DONE**: noise dwell is still `phinc >> 12` (K/sound.c:572).
  - **#33 DONE**: MODSAMPLE still refuses volume 0 (M/mmb_play.h:302).
  - **#34**: Ctrl-\ still quits; see row 19.
  - **#12** (JSON$ paths), **#23-24** (FRAMEBUFFER CREATE 2 / CLOSE, 1bpp MERGE), **#25** (BLIT w or h < 1 draws nothing), **#31** (LOAD PNG crop and new arguments): untouched since.

## Already matches (checked)

bc6f1d7 IRETURN restores ON ERROR (R) · ad6d52f `-2^2` = -4, `2^3^2` = 64 (R) · 2643d10 seeder 69069 · ad5caaf `=` after the bracket for MID$, BYTE, BIT, FLAG, LMID, MAP, PIN, PORT (R) · a5ca360 REDIM bounds evaluated once (R) · 7431fcf struct member indexes (R) · 4848185 graphics coordinates and comms data that are calls (R) · 0976786 GPS as a whole word only (R) · de2bde3, 7ab8d84 option words and `'` comments (R) · 3bc65b5 ERROR text with `%` and `@` (R) · 57f8016/1789f74 `/*` rules (R) · mmcsub.h's a760deb, 79d5a67, 47483d8, 328eb4e/e16ac93, 4e259d8: our runtime already had them · 8368271 GCC 14 pointer types (R) · 917e63c TCP reply order · 981174d BMP failure cleanup · c87a650 PLAY TONE rate · 605f58d, aef1c7d, 50549dc fonts and FILES · a125e17 MODE 1 transparent text · saving and loading MAP'd colours and RLE BMPs.

## N/A

- **Interactive, or state carried between runs** (a program here is a fresh process): fabae0a, d437fb3, 09c5a17, 3ddbf07, 4a4bb49, c5a8e22, 641d043's carry-over.
- **Not translated:** CHAIN and EXECUTE (80f289f, d159403, 919c411, 94d4da1, c6c912a); MEMORY PACK (a24f75f); MOUSE/GAMEPAD/Wii interrupts; GUI other than BITMAP; STRUCT SAVE/LOAD; #INCLUDE (decision #22: when added, `"/x.inc"` is from the root, 2cb4a6e).
- **Decided NEVER or deferred:** RAY (6911f72, 175d5f2, ea0623a); STAR/ASTRO (1180c82); TILE (fd8ba3d); MODE 1 BLIT cell colours (c160484); MODE 5 and HDMI MAP (9bdcae7, f90b1aa, adb9009); HDMI pool slots (64ae19d, 8313704).
- **Hardware FUZIX does not have:**
  - CDC host and USB serial (1dc9572, a758061, 2ceea4a, 78a6e9c).
  - Bulk-IN timing (ad0975b) and EPX switching (30f5a7d).
  - Touch, gamepads and MM.INFO(USB) (6896eed, 7b8133e, 34b44d5, d34fd21, c5b3890, 8a1280a, 1cd96be, 0a0d89f).
  - C: drive (0b66358, dd3107e).
  - Stepper, GPS, PS/2 mouse, on-screen keyboard, file manager, LIST to #n, OPTION RESOLUTION.
  - A runtime clock change (13b081e's re-timing half).
  - ADC DMA (b9a33ea), VGA boot (91f2f88), the MMBasic heap (b61ed31, 96575ef).
  - SETPIN COMn (7b3aa5a), OPTION LOGGING (141a6da), MM.INFO$(COMPILE) (48c7d27).
- **mmb2csub only:** e7a7d30, d95cb05, 4ae068f, 9a30321, 5f4a021, 0912633, ab34e06.
- **pc3host-only:** nothing. pc3host has no USB host, and its Print Screen key arrives as HID 0x46 (server/keyboard.c:156). It gets every SHARED row through ext/FUZIX.

## Out of scope (error paths)

54c2de4, f81b78e, 4502031, the error variants of 4848185/ad5caaf/30b1503, de2bde3's invalid PLAY letter, caee048 #134-#136, the RAY messages, d3e952d's page errors, 981174d's 0×0 header and FLASH LOAD IMAGE slot, the argument checks in 2abbb53/ec6fd02, the C: drive error texts, 81f75c1's trailing text, DEFINEFONT numbering, the MODE 1 refusals, 3687a42's wording.

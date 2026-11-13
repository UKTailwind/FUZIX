# PicoMite changes since pc3host was created: what to apply

Review of PicoMite `6b88df2` (2026-09-04, V6.03.02b2) .. `dc2c870` (2026-10-21,
V7.00.00RC3+), 919 commits, against FUZIX `pc3` at `8f8a92af0` and pc3host 0.8.1.
Sources:
- both release-notes files;
- `docs/Main_Bug_Report_2026-09.html`;
- a sweep of the 298 source commits for anything the notes miss;
- the translator.

The sprite engine is excluded (being reworked).

**Target:**
- **SHARED**: a FUZIX file that the board and pc3host both compile (mmb2c.py + mmbc, runtime, mmbedit, kernel display.c/sound.c/fonts.c/kbd_decode.c, utils loaders).
- **BOARD**: the Fuzix kernel or hardware only.
- **HOST**: pc3host only.

Every translator change goes into **both** mmb2c.py and mmbc (cgate.sh byte-identical). A new `mm_*` entry point also needs bcrun_mm.c.

Paths:
- **M** = `Applications/mmb2c`
- **K** = `Kernel/platform/platform-rpipico`
- **E** = `Applications/mmbedit/editor.c`
- **S** = `Applications/mmbedit/shim.c`

Line numbers are as of 2026-10-21. Marked ✔ = re-checked against both sources by hand, or run on the host build.

## Decisions (Peter, 2026-10-21)

| Item | Decision |
|---|---|
| P0 #5 subscript check, P0 #6 float→integer check | **Always on**, REVISED for #5 after measurement: **gated on checks_on()**, like the arithmetic checks. Every access cost ~55 bytes a subscript; the eclipse was +6.4% size and +4.9% time on the board, and robots +8.6%, which no longer started at THUMB_BUDGET=140000 (77 of 74 blocks). #6 is still to decide in the light of this |
| P1 #8 integer `^` | **Match PicoMite** (2026-10-22): squaring, wrap on overflow; a run-time negative exponent gives the float where a float is wanted. DONE FUZIX 7ffb12002 |
| P1 #21 MAX/MIN | Once each, in order; **integer when every argument is** (2026-10-22, after robots grew a block on the float version). DONE FUZIX 903473b6b + c95f6ee3f |
| BBC synth parity | **Yes**: queue 16, noise block, 44100; ADVAL and sound_seam.c:205 follow |
| MATH RANDOMIZE seeder | **Keep 69069; PicoMite will change.** The other #69 parts (seed 0 → 4357, /2^32, range, no-argument reseed) still apply |
| Scalar `LENGTH n` | Enforce only where the storage is LENGTH-sized (arrays and members, already done); scalars unchanged |
| `NOT 0 + 1` | **PicoMite is correct** (2): NOT and INV bind to the next value. PicoMite's manual precedence table (help.txt:9404-9407) says otherwise and needs correcting |
| VAL | Integer when the left side of the assignment is an integer |
| RGB() range and LITEGRAY, font 7 `%`, BOUND() errors | **Fix** |
| WEB PING | **Leave as is** |
| `-2^2` | **Ours stays -4** (MATLAB). The CMM2 is being fixed to -4, and PicoMite follows it, so no change here |
| `2^3^2` | **Follow MATLAB and PicoMite: 64**, equal precedence left to right. Ours is 512 and changes (user, 2026-10-21) |

## P0: crashes, hangs and memory safety

| # | Item | PicoMite | Ours | Target | Change |
|---|---|---|---|---|---|
| 1 ✔ **DONE** FUZIX f6b43576a, pc3host 1f8102c, board-proven (`snd 6`) | BBC ENVELOPE with PN1-3 all 0 and T bit 7 clear loops forever **in the DMA IRQ** (board hard-locks; pc3d's audio thread hangs). PicoMite's fix comment says the PC3 kernel original has the same hazard | e8fbb78 (io/AudioBBC.c env_step) | K/sound.c:117-127 | SHARED | :121 `else if (!(e[0]&0x80) && (e[4]\|e[5]\|e[6]))` |
| 2 ✔ **DONE** FUZIX cbb40e686, board-proven | FORMAT$ overruns its stack buffer. The literal text before `%` can be up to 254 chars and the width up to 250, but `out[]` is 319 bytes. A result over 255 is truncated silently | 02120e4 (#71): 512-byte build, then "String too long" | M/mmb_runtime.c:1178, :1261 | SHARED | Buffer ≥ 512 bytes. Raise "String too long" when the result is over 255 |
| 3 **DONE** FUZIX 806ec7a88 (+ pngtest gate), pc3host 47bfaf5, board-proven | upng inflate: `blcount[MAX_BIT_LENGTH]` is written past its end by a 15-bit code length. A back-reference before the start of the data is not checked | 4d91e6c | K/utils/upng.c:216, :622-623 | SHARED | `blcount[MAX_BIT_LENGTH+1]`. Error if `distance > start`. (Full streaming-decoder port: see #31) |
| 4 **DONE** FUZIX befa4f90b, board-proven | WEB server `mm_webs_fd[]` is zero-filled. Before WEB TCP SERVER PORT has run, CLOSE n closes **stdin** and SEND/TRANSMIT write HTTP to fd 0 | (PicoMite: "Server not open") | M/mmb_webs.h:52, :100-104, :230, :263, :280, :430 | SHARED | Initialise to -1. Raise "Server not open" |
| 5 ✔ **DONE (gated)**, see the decisions table; the STRUCT COPY/CLEAR/SWAP path is still unchecked | **No subscript range check at all.** `DIM a(3): a(4)=7` writes past the array silently. Separately, a 64-bit index is cut to 32 bits | 8f617d8 ("Index out of bounds") | M/mmb2c.py:1762-1812 `(int)(…)`; mmbc_expr.c:489, mmbc_type.c:207, 224, 234, 667, 671, mmbc_stmt.c:5104 | SHARED | Check the 64-bit index against base..ub before narrowing. **DECISION:** always on (it is C UB, like `\` and MOD) or under checks_on(). Measure the size and speed cost |
| 6 ✔ **DONE** FUZIX 4b98944f9 (always on) | Float→integer of NaN, ±INF or ≥2^63 is C UB, so the board and the PC give different answers. `CINT(2^63)` gives -9223372036854775808 here. INT and FIX are plain casts. Above 2^52 we still add 0.5 (1 too high) | a40112b (FloatToInt64/32) | M/mmb_runtime.c:755-759 mm_toint; M/mmb2c.py:2348-2355, mmbc_builtin.c:167-176 | SHARED | Copy FloatToInt64 verbatim (core/MMBasic.c:7978-7992), giving "Number too large". Route INT and FIX through it. **DECISION:** always on, recommended because this is UB |

## P1: silent divergences in common code (SHARED unless marked)

| # | Item | PicoMite | Ours | Change |
|---|---|---|---|---|
| 7 ✔ | PRINT comma moves the column to 14-column zones; PicoMite now uses 8-column stops (the byte written is still a tab). PRINT #n's TAB() and POS use the console's column | 753ace5 (#37, #38) | M/mmb_runtime.c:616, :629; mm_tab :731-751 | 14 → 8 in both places. Give each file number its own column (1 at OPEN); emit a file-aware TAB/POS inside PRINT #n. Older: the graphics-mode tab pads to OPTION TAB in PicoMite (Draw.c:6778); TAB() outside 1..255 is an error there, a clamp here |
| 8 ✔ **DONE 7ffb12002** | `int ^ int` with exponent ≥ 0 is an INTEGER in MMBasic (by squaring, unsigned wrap). Ours is always float: `10^6` prints 1e+06, and `2^62` and `3^39` lose digits | c04dc39; core/Operators.c:66-95 | M/mmb2c.py:1597-1603, mmbc_expr.c:313-318, mm_pow mmb_runtime.c:810 | TY_I via a new `mm_ipow` when both operands are TY_I. A negative literal exponent stays float. **DECISION:** a run-time negative exponent can't change the static type; suggest a loud error |
| 9 ✔ **DONE 586de0b98** (under checks_on; mm_fdiv always) | +INF from `+ - * / ^` raises "Overflow" (only +INF, as the reference) | adb769f, b9530d9 | M/mmb2c.py:1540-1578 (bare C); mm_fdiv :804-808; mm_pow :810 | Checked forms under checks_on() |
| 10 | ON ERROR SKIP n: a FUNCTION called by the protected statement uses up the count. A SUB's statements still count | 74f8f23 (#34) | M/mmb2c.py:9914-9920, :5020-5028; mmbc_stmt.c:6205, mmbc_walk.c:212; mmb_runtime.c:2651-2664 | Runtime call depth. Decrement only at depth ≤ the recorded depth. Save and restore it with the interrupt error state |
| 11 ✔ **DONE 08d398531** | SUB MM.END never runs: not at END, and not when the program runs off its end | 8a9e3b6 (#66) | M/mmb2c.py:9842-9869 do_end, :10666; mmbc_stmt.c:6111 | If defined, call it at END (not END NOEND) and before main returns; not on an error exit |
| 12 | JSON$ path walker replaced: leading `[n]`, a trailing `[n]` returns the element, case-insensitive final key only, "Invalid JSON path" rules, "String too long", depth 128 | 9749c83 (#12), Custom.c ~3232-3290 | M/mmb_json.h:302-410 | Port the new walker; re-bless the JSON tests |
| 13 ✔ **DONE c80eeb8a7** | A FUNCTION called as a statement, or through CALL, is accepted and its result discarded | edc7999 | M/mmb2c.py:8463-8468, :8491-8512; mmbc_stmt.c:5373-5420 | Translate-time error. **DONE as a run-time error + warning** (the reference raises when the line runs): "Unknown command" / "Invalid syntax" / "Unknown user subroutine"; dispatcher fallback now "Unknown user subroutine/function" |
| 14 ✔ **DONE e89a57131** | A variable with a SUB or FUNCTION's name gives "A sub/fun has the same name: x" | 48b4680 | M/mmb2c.py:1216-1290 declare/reference; mmbc_sym.c:371-396 | Error for DIM, LOCAL, STATIC, parameters and implied variables; not for a FUNCTION's own result name |
| 15 ✔ **DONE 389b96902** (+ GOTO/GOSUB/RESTORE to no label: run-time "Cannot find label") | A label with a SUB's name: `Here:` beside `SUB Here` is translated as a CALL (rc=0). MMBasic: a name with an adjacent `:` is always a label | 2d5307e; tokeniser core/MMBasic.c ~2288 | M/mmb2c.py:3813-3832; mmbc_decl.c:225-253 | The lexer keeps colon adjacency and decides on that |
| 16 | Duplicate label gives "Duplicate label" (was silently ignored) | 825ade6 | M/mmb2c.py:3787-3792; mmbc_decl.c:181, :251 | Error in the decl pass |
| 17 | Names longer than 31: "Variable name too long", "Label too long", "Function name too long". For a STATIC, the SUB name plus the variable name must be ≤ 31 | 2c747af | no check | Add the three checks, counting without the type suffix |
| 18 | Program line over 255 characters gives "Line is too long" | b3c8155 | accepted | Refuse it |
| 19 | `a(i = 1) = 5` and `FOR i = k = 2 TO 3` are errors; we translate them as comparisons | 22cd802, 112ce55 (#84) | M/mmb2c.py:8556, :8895-8912; mmbc_stmt.c:5481, :5729 | Translate-time errors |
| 20 ✔ **DONE 6e075aa1d** (members written by READ/INPUT/INC/CAT/MID$/bus lists/byref; string single bus destination and FOR refused) | A structure member as an I2C/SPI/ONEWIRE READ destination, or as a by-reference argument, silently becomes an implied global FLOAT named `p.x` | 85a90a6; 9749c83 (#9) | reference() M/mmb2c.py:1250-1289, reached from comms_rx :9672, lvalue_from_here :8547, arg_item :1965 | Route dotted names through struct_head/member_path. At minimum, refuse a dotted name whose head is a struct |
| 21 ✔ **DONE 903473b6b, c95f6ee3f** | MAX/MIN repeat each argument's text in nested ternaries, so an argument is evaluated up to 2^(n-1) times (`MAX(RND,0.5)` can be < 0.5). The result type should be float | 50c9aa0 area; core/Functions.c:1906-1928 | M/mmb2c.py:2397-2408; mmbc_builtin.c:238-256 | Evaluate each argument once (temporaries or mm_max/mm_min); TY_F |
| 22 ✔ **DONE b14d395b4** (#COMMENT/MMDEBUG done as the loader; #DEFINE/#INCLUDE/#MMDEBUG ON refused) | Preprocessor (#INCLUDE, #DEFINE, #COMMENT START/END, MMDEBUG). Lenient mode comments each one out with a warning and rc=0, so #DEFINE substitutions vanish and code inside #COMMENT is compiled | 043f914, 1179345 | M/mmb2c.py lenient path | Now: make the directives fatal in lenient mode too. Later: implement them (see ADD) |
| 23 | FRAMEBUFFER CREATE 2 silently creates F (non-strict). A bare CLOSE closes F only; CLOSE T or CLOSE 2 become CLOSE F. mm_fb_close(F) resets WRITE L to N; MODE never clears mm_fb_made_l | f3e811a, 4bb23ea | M/mmb2c.py:5382-5427; mmbc_stmt.c:571-578; mmb_runtime.c:6437-6479 | As FrameBuffer.c:1287, :1751. Map "2" to DISP_FB_L (see ADD) |
| 24 | FRAMEBUFFER MERGE: in 1bpp it ignores the colour and ORs (ours ANDs for 1 and fails for 2-15). Default colour 0. Buffers are checked before the colour | 4bb23ea | K/display.c:261, :324-336; mmb_runtime.c:6558 | As FrameBuffer.c:1826-1880 (medium confidence; verify on a PicoMite) |
| 25 | BLIT FRAMEBUFFER/FLASH: "Number out of bounds" (ours: "Invalid coordinates"); w or h < 1 draws nothing (ours refuses); "Same framebuffer"; hs/vs < 1 is invalid in BLIT FLASH | 37e7af5 | M/mmb_blit.h:923-925, :995-999 | As Blit.c:692-950 |
| 26 | LOAD IMAGE/BMP: BI_BITFIELDS masks in a V4/V5 header (GIMP's RGB565) are read from the wrong place | 884269c (#24) | K/utils/loadimage.c:195, :207-213 | Read the masks at 14+40 whenever compression is 3 |
| 27 ✔ **DONE 6654d456c** | SETTICK with a negative period silently turns the tick off; PicoMite gives a range error | ed4ddbc | M/mmb_int.h:391-395, :424-426 | getint wording, 0..2147483647; tick number 1..4 |
| 28 ✔ **DONE 942943f53** | TRIM$ where$: lower case is ignored and a bad letter passes | 1ee2feb (#76) | M/mmb_misc.h:224-228; mmb2c.py:2705, mmbc_builtin.c:672 | Upper-case it; anything else is "Invalid syntax" |
| 29 ✔ **DONE d567fec01** | LINSTR with start < 1 searches from 1; PicoMite returns 0 | MM_Misc.c:1461 | M/mmb_lstring.h:244 | Return 0 |
| 30 ✔ **DONE d567fec01** (MID's explicit negative count still reads as omitted) | LONGSTRING LOAD/LEFT/RIGHT/MID clamp a negative count where PicoMite errors | 8d44510 (#72) | M/mmb_lstring.h:59, :86, :97, :111 | getint 0..0x7FFFFFFF wording (our other error texts differ too) |
| 31 | LOAD PNG: crops an image bigger than the screen ("Image too large" here); new `ximage, yimage` arguments; new error texts; `.png` default extension; the streaming decoder (~40 KB at any size, no arena) | 4d91e6c | K/utils/loadpng.c:276-332; upng_pc3.c:48 | Port the streaming upng.c behind shims in upng_pc3.h, then LoadPNG's crop and offset logic. The translator already passes the arguments through |
| 32 | PLAY SOUND type N: the dwell should be f samples; ours scales it like a pitch (≈ f×0.093) | 3ef90a4, 884269c (#27) | K/sound.c:567-575, :716-728; K/utils/playsnd.c:270-284 | Dwell = floor(f); drop the reset at :727-728 |
| 33 | PLAY MODSAMPLE volume 0 is refused | 884269c (#28) | M/mmb_play.h:287; K/utils/playmod.c:225 | Accept 0 (`p1 ? p1-1 : 0`), with the reference's check order |
| 34 | Ctrl-\ (VQUIT) kills a running program and discards an mmbedit edit. PicoMite makes 0x1C a key (Print Screen, or CHR$(28) to INKEY$) | cd8582c | M/mmb_runtime.c:3057-3060 mm_tty_raw; S:120, :137 | `c_cc[VQUIT] = c_cc[VINTR]` in both (Fuzix has no _POSIX_VDISABLE, so not 0) |
| 35 ✔ **DONE cce5f5478** | CHOICE between two strings returns the variable's own storage | a121e4e (#84) | M/mmb2c.py:2659-2661; mmbc_builtin.c ~605 | mm_scopy, sequenced before any sibling call (low-medium) |
| 36 | Scalar `LENGTH n` is ignored by design. PicoMite enforces it on DIM init, LOCAL init and READ count | 9749c83 (#11), 2bfd69f, 96ab611 (#59) | M/mmb2c.py:4552-4584; mmb_comms.h:198-202 | **DECISION:** keep 256-byte storage but enforce the cap |

### MATH family (SHARED)

| Item | PicoMite | Ours | Change |
|---|---|---|---|
| ✔ **DONE 35f9ff714** SCALE of an int array by a fractional scale rounds the scale first (5×0.3 gives 0 here, 2 there) | 9749c83 MATHS.c:1599 | M/mmb2c.py:7887-7891; mmbc_stmt.c:4629-4636; mmb_array.h:155-161 | Float-scale variant `mm_toint((MMFLOAT)in[i]*v)`; keep exact int×int |
| ✔ **DONE 35f9ff714** POWER into an int array rounds the exponent (16^0.5 gives 16) | 9749c83 MATHS.c:2610 | M/mmb_math.h:355 | `pow` and then round; re-bless tests/matha.bas:47 |
| ✔ **DONE 35f9ff714** WINDOW: a constant input gives NaN; integer outputs and min/max truncate; a CONST is accepted as min/max | 9749c83 :2708, d4deef9 :2689 | M/mmb_math.h:1189, :1221, :1253; mmb2c.py:8025-8031, :3306-3317 | Constant input gives the output minimum; round with mm_toint; "Cannot change a constant" |
| ✔ **DONE 35f9ff714** MATH(CROSSING) returns `i + OPTION BASE` | ec27921 | M/mmb_math.h:1099, ~:1127; mmb2c.py:3114; mmbc_builtin.c:1386 | Pass the base; the comment at mmb2c.py:3095 is stale |
| ✔ **DONE 35f9ff714** MATH(MAX/MIN a(), idx%) gives a 0-based index under BASE 1 (our own bug) | MATHS.c:3636, :3700 | M/mmb_array.h:237-271; mmb2c.py:3160-3176 | `k + base` |
| ✔ **DONE 35f9ff714** RANDOMIZE 0 makes RAND 0 for ever; RAND can be 1.0; seed range unchecked; RANDOMIZE with no argument does nothing | 4861a1a (#69) | M/mmb_mt.h:73, :135; mmb2c.py:8303-8308 | Seed 0 becomes 4357, divide by 4294967296.0, range check, reseed from the clock. **DECISION:** we seed with 69069 on the ruling that MMBasic would change; PicoMite still has 6069 |
| ✔ **DONE 35f9ff714** MATH(BASE64) into a short-LENGTH string: wrong size tested, wrong message | 51a1a57 (#73) | M/mmb_math.h:134-135, :195-196 | Test the real size against LENGTH: "Too many elements for string output" |
| ✔ **DONE 35f9ff714** Integer C_DIV by zero not trapped; Q_CREATE size error names argument 4 | 9749c83 :1892, 3ae5b9d | M/mmb_math.h:62, :914 | "Divide by zero"; argument 5 |

### mmbedit (SHARED: E and S are built for both)

| Item | PicoMite | Ours | Change |
|---|---|---|---|
| A paste loses all but its first line: keys arriving during a redraw are dropped | c5528a6 (#51) EdDropHeldRepeats | E:616-617, :905-906, :922-924 | Drop the queued keys only if all equal the current one, otherwise push them back. Needs a multi-byte pushback in S (~60 lines) |
| Phantom empty last line; a last line typed there saves with no newline | c5528a6 (#52) | S:507-554 file_load, :591-608 file_save | Strip one final `\n` on load; append one on save |
| Esc can't discard the changes while a line is over 255 | c5528a6 (#50) | E:2718-2728 | Check the length only when `quit != ESC` |
| "LINE n TOO LONG" names the wrong line when it is the last | bea36e9 (#53) | E:964-968 | `*linein = line + 1` |
| Esc, F3 or Shift-F3 after typed text at a prompt is appended; a search match past the right edge stays off screen | 7191fbd (#57) | E:860-865; E:2799-2803 | `len = 0`; port the scroll block |
| F9 import marks the text changed when nothing went in; a full buffer is not reported; the mark-mode export message is overwritten | dfffe2a (#56) | E:2827-2838, :2898-2916 | As Editor.c:7668, :8259 |
| Colour coding applied to non-.bas files; the .bas test is case-sensitive (older) | Editor.c:811-812 | E:62, :2187 | `Option.ColourCode = is_bas`, case-blind |

### Keyboard (SHARED, carried by kbdsync.sh)

- ✔ Shift+Left gives 0xA2 and Shift+Up gives 0xA4 (f85e906): `K/keyboard_maps.h` :85, :87 and the same rows at 189/191, 292/294, 395/397, 498/500, 601/603.
- Older gap: INKEY$ never returns &HA1-&HA4, because kbd_decode.c:231-234 sends a plain CSI for shifted arrows. Shifted F-keys may not decode either (mmb_runtime.c:3204-3235). Medium confidence.

### Error-text alignment (SHARED, small)

- **93600cc:** TILEMAP "No image in slot " (M/mmb_tilemap.h:274). FLASH LOAD IMAGE "Invalid BMP file" and "Image too large for slot", and getint wording for slots (M/mmb_flash.h:70-87, :187-193).
- **2201213:** "Not enough memory for % bytes" via one `mm_nomem(n)` (mmb_blit.h:347, :431; mmb_tilemap.h:229; mmb_flash.h:82; mmb_runtime.c:4214, :4274; bcrun_mm.c:529, :545).
- **1e6daf9:** SEEK with pos < 1 is an error; we clamp it (mmb_runtime.c:1528).
- **aec020d:** "Warning: No free connections" (mmb_webs.h:145-157).
- **2c0e6f3:** PLAY SOUND wordings (mmb_play.h:156-161).

## BOARD only

| Item | PicoMite | Ours | Change |
|---|---|---|---|
| TinyUSB hcd: a freed endpoint slot keeps a stale `interrupt_num`, so an unplug can steal the keyboard's reports or break the next device's enumeration | bb53afa (tinyusb-patches/hcd_rp2040.patch); upstream #4177 | K/tinyusb/.../hcd_rp2040.c:291, :480-510, :550-555; our usb/hcd_rp2040.patch lacks it | Add the three hunks |
| Recoverable USB faults panic | da2c306, 0e7581e | rp2040_usb.c:121, :139; hcd_rp2040.c:399 | Clear the stale latch and continue (medium) |
| WiFi join: retry LINK_FAIL up to 3 times, 1 s apart | 5f128d2 (net/WiFi.c:257-275) | K/utils/wifi.c:208-235 waitup() | Retry only `st.wifi == -1` |
| PSRAM bring-up waits are unbounded (silent hang before the console) | c85805d | K/psram.c:181-272 (6 waits) | Bound them, so "PSRAM not found" can be printed (low) |
| TinyUSB master: enumeration retry, hub port status | 833ee0f, 295236d, 9c3012c, 0a50dfe | 0.21.0 + 3 patches | At the next bump, keeping the EP0 RX-timeout grace; board-test with 4 devices on the hub |

PicoMitePC3 hardware handling checked against the kernel. Everything matches:
- radio GP23/24/25/29 on PIO2;
- I2S GP10/11/22;
- SD GP30/31/28/33;
- I2C0 GP20/21;
- PSRAM CS GP47;
- console GP8/9.

## BBC synth parity (SHARED): DECISION

PicoMite's AudioBBC.c was derived from our sound.c and has moved on (e8fbb78, 6f263f7):
- queue depth 16 (ours is 8);
- the full noise block: periodic and white, settings 3/7 clocked by channel 1, reseed 0x4000;
- the engine at 44100 rather than 22050, which also cures an octave-high note after a PLAY SOUND session (sound.c:598-604).

Both ADVAL in Fuzix BBC BASIC and pc3host's tests/seam/sound_seam.c:205 (which asserts 8) change with it.

## Long-standing divergences found in passing (not new in the window)

Verify on a real PicoMite before changing; each one changes existing programs' results.

| Item | Evidence | Ours |
|---|---|---|
| ✔ **DONE a0c223006** NOT and INV apply only to the next value (`NOT 0 + 1` = 2); a minus takes the powers after it (NegPowers #96, `-2^2` = -4 in both now); `=` is below the other comparisons | core/MMBasic.c:4110-4200 getvalue | fixed |
| ✔ **DONE a0c223006** Equal precedence evaluates left to right: `2^3^2` = 64 | core/MMBasic.c:4031 doexpr `<=` | fixed |
| ✔ **DONE b05e85cd8** VAL is an integer for an integer string; StrVal's parse exactly | core/Functions.c:1367-1409 | integer wherever taken as one (as_int) |
| CHOICE takes the type of the branch it picks | beta-5 notes | promoted to float |
| RGB(r,g,b) outside 0-255 is an error; RGB(LITEGRAY) is "Invalid colour" | graphics/Draw.c:3815-3869 | `& 0xFF` (M/mmb2c.py:2774-2776); LITEGRAY in RGBNAMES :154 |
| Font 7 `%` glyph mirrored | 2e87b5a fonts/Font_8x6.h:18 | K/fonts/Font_8x6.h:18 |
| BOUND() of a missing dimension gives "Dimensions"; of a non-array, "Expected an array" | d2a22dc, edf62f7 | returns 0 (M/mmb2c.py:3398-3409) |
| WEB PING is in-process, with `[,timeout_ms][,avgvar]` | 6900106 net/MMping.c | spawns ping(8) (mmb_net.h:552-560); count 0 pings for ever |
| MATH SCALE/ADD/SLICE/INSERT/POWER accept mixed int and float arrays | 9749c83 | refused (honest gap) |

Stale docs:
- `DIM a(0)` and `DIM a(1)` are legal on PicoMite since 2026-08-24, but COVERAGE-STATUS.md:193-200, NEXT.md:238-257 and mmb_math.h:678-715 still say PicoMite refuses them.
- All three CRC divergences are now PicoMite's own behaviour, so the comment at mmb_crc.h:33-56 is stale and the tests can be blessed.
- The comment at mmb_lstring.h:128-136 is stale.

## ADD: new features worth having (SHARED unless marked)

| Item | PicoMite | Size |
|---|---|---|
| ✔ **DONE dd528b15e** OPTION SYMBOLS OFF accepted as a no-op at the top of a program. V7 programs will carry it, and today it is "not translated" | 50ada44 | ~10 lines per translator |
| Preprocessor #INCLUDE / #DEFINE / #COMMENT / MMDEBUG | 043f914, 1179345 | 150-250 per translator |
| FRAMEBUFFER CREATE 2 / WRITE 2 / buffer "2" in BLIT and TILEMAP DRAW | 4bb23ea, 846b6bb, 066849a, c087268 | ~20 per translator |
| PLAY BBC SOUND / PLAY BBC ENVELOPE / MM.INFO(BBC BUFFER) (after P0 #1 and the parity decision; the host needs ADVAL in server/dispatch.c) | e8fbb78, 6f263f7 | ~250 + kernel 30 |
| PLAY WAV/FLAC/MP3 on a directory, PLAY NEXT / PREVIOUS. Today a directory fails in the background while the program carries on | 194a843, c271835 | 150-250 |
| TILEMAP LOAD file$ (`.map`) | dba7552 | ~120 + 12 per translator |
| MATH CLAMP | 2310038 | ~40 + 15 per translator |
| BASE$(base, n [, chars]) | b2ded8c (mm_base already exists, mmb_runtime.c:1113) | ~10 + BUILTINS |
| RAM image slots 4-8 | 93600cc | ~30 |
| Print Screen + OPTION PRINTSCREEN (the key already arrives as 0x9D through kbd_decode and pc3d) | dfffe2a, cd8582c | 150-200 editor, ~120 runtime |
| Nested member assigned whole; a nested member as a struct argument (our refusal text "the firmware overruns" is no longer true) | 9749c83 (#9) | 10-20 |
| WEB SCAN (BOARD NETIOC_SCAN + runtime) | 698a5da, bd1a965 | ~230 |
| MM.INFO(TELNET), constant 0 | 94c7a0f | trivial |
| Bluetooth (BOARD; btstack, flash, RAM) | b7/b8/RC | large, low priority |

## Housekeeping

- **cgate is red.** 8f8a92af0 added `note_touch` to mmb2c.py arg_item and not to mmbc. Add `note_touch(canon, s);` before `cv.i += 1;` at mmbc/mmbc_expr.c:851 (bare-scalar path) and :904 (array-element path, before `index_of(s)`). Proven on a scratch build: 0 C diff lines and 0 stdout diff lines over all 390 runs. Only the report comment changes.
- **pc3host:**
  - The `ext/FUZIX` pin (f67b22e17) is 10 commits behind; bump it with the next release (make-zip builds the pin unless FUZIX_ROOT is set).
  - The installed .deb in this WSL is 0.7.4.
  - Dropbox holds a stray `pc3host_0.8.0_amd64.zip`.
- **pc3host gate weaknesses found while doing item 4:**
  - `tests/sweep/run-corpus.sh` waits 2.5 s for its headless pc3d and then runs the whole corpus WITHOUT a display, saying nothing. Only blit and flash noticed, as "Buffer not in use". It should fail when the socket never appears.
  - pc3d's audio start-up can hang for ever: miniaudio blocked on WSLg's PulseAudio while an orphan pc3d held it. `--audio auto` needs a timeout that falls back to null, as the exit path's alarm(2) does.
  - The orphan was the INSTALLED 0.7.4 .deb's windowed pc3d, autostarted at 14:39 when something reached `/usr/bin/<helper>` by bare name. This is the gcc-check trap again, probably from qemutests. Upgrade or remove the 0.7.4 package in this WSL.
  - The staged `build/lib/cc/include` headers dated from 5 September. Refresh them with `rm build/bin/cc` and a rebuild of ccbc after any header change.
- **The translator has no fork.** PicoMite's mmb2csub loads this mmb2c.py, and its zips carry byte-identical copies (RC3 = FUZIX HEAD). The CMM2 uses the same mmb2csub (`--target cmm2`). PicoMite's Route B compiler (core/Stream.c) is unrelated.
  - Every mmb2c.py change here reaches both mmb2csubs.
  - A change that makes the translator emit a NEW runtime name (mm_ipow, the bounds check, the RGB range check, an integer VAL) needs a matching entry in PicoMite's `user-tools/mmcsub.h`. Run mmb2csub on its examples after each such change.
  - Filed: PicoMite #95, mmcsub.h's `mm_ssetm` drops the LENGTH cap.

## N/A (for the record)

- **Interpreter internals and the RP2350 compiler:** ~106 commits. Symbols and the name index, VREC, Route B / OPTION COMPILE / SHADOW, OPTION SYMBOLS memory, trace cache, DO fast path, tknbuf, LIST, EVAL/EXECUTE, stack warning, flash and heap layout, build.
- **Hardware the PC3 lacks or Fuzix doesn't drive:** USB mouse and gamepads, USB CDC host, VS1053, cameras, PS/2, SPI panels, GPS, PicoCalc, I2C slave, ONESHOT, stepper, PIO interrupts and DMA, HDMI scanout layer, RP2040-only fixes.
- **Fuzix has its own userland:** ZMODEM, the file manager and track list, B: drive, LIBRARY, HELP, AUTOSAVE, UPDATE FIRMWARE.
- **Sprites:** excluded as instructed.

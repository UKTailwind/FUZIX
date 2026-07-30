# Upstreaming to Codeberg — state

2026-07-30. Upstream is **codeberg.org/EtchedPixels/FUZIX** (the GitHub
EtchedPixels repo has been archived since 2025-12-23). Our fork is
**codeberg.org/UKTailwind/FUZIX**, created for this; remote `cbfork`.
Remote `codeberg` is upstream itself, fetch-only in practice.

Auth is a Codeberg access token in `~/.netrc` (scopes: repository
read+write, issue read+write, user read). If it has been revoked, the
API and pushes stop working with 401; a 403 names the missing scope,
which is how the first, unscoped token was diagnosed.

## Open pull requests

| PR | Branch | What |
|---|---|---|
| [1261](https://codeberg.org/EtchedPixels/FUZIX/pulls/1261) | `cb-fcc-trim-constant` | `trim_constant` never trimmed anything — [commented](https://codeberg.org/EtchedPixels/FUZIX/pulls/1261#issuecomment-20300521) that the fix already exists in the compiler kit and a vendored-copy resync may be preferred |
| [1262](https://codeberg.org/EtchedPixels/FUZIX/pulls/1262) | `cb-filesys-nfree-overrun` | `sizeof(int)` overrun onto `s_ninode` |
| [1263](https://codeberg.org/EtchedPixels/FUZIX/pulls/1263) | `cb-devio-lru-wrap` | LRU wrap panics with the pool free |
| [1264](https://codeberg.org/EtchedPixels/FUZIX/pulls/1264) | `cb-libc-decisecond-units` | `usleep()` <100ms hangs; `clock_gettime` sub-seconds |

All four reported mergeable at creation. Deliberately staggered: nine
PRs at once from a new account is a lot to land on a maintainer, and
1262 is the most consequential (silent filesystem corruption) so it
wanted to arrive while attention was fresh.

## Pushed to the fork, NOT yet proposed

`cb-exec-stack-align` · `cb-libc-strchr-char` · `cb-armm0-rules` ·
`cb-init-baud` · `cb-ucp-dirent`

## Built locally, NOT pushed

`cb-pico-r4-abi` (`75af5dd76`) — held on request. `plt_switchout` and
`dofork` use r4 as scratch and never save it, so the caller's r4 does
not survive a context switch; AAPCS requires it. Latent (depends on
whether gcc keeps anything live in r4 across the call) and **not**
RP2350-specific: the default subtarget is `pico`, ie RP2040.

Every `cb-*` branch is exactly one commit off `codeberg/master` and was
built by editing a fresh checkout, never by cherry-picking, so no PC3
specifics leak in.

## Still upstreamable, not yet branched

Shared FCC front end, all verified still broken upstream. These affect
every FCC target, not just ours:

* hex escapes in cc0 - `(unhex(c2) << 4) | unhex(c)` has the nibbles
  swapped, and it accepts exactly two digits where C89 allows any
  number
* `sizeof expr` parses with `hier0` not `hier10`, so `sizeof 0 < 2`
  becomes `sizeof (0 < 2)`
* identifier length has three different numbers - hardcoded `14`,
  `NAMELEN 16`, `symstr[16]` - so two long identifiers silently become
  one with no diagnostic
* cast to void rejected
* `int (*fp)()` matches no prototype; `&func` gets incremented to
  pointer-to-pointer by `typeconv`

Found 2026-07-30 by the mmb2c phase 0 work (each bisected to a minimal
repro with gcc as oracle, each fixed on `pc3`, none yet branched):

* **constant comparison folding** — `T_GT` and `T_GTEQ` in tree.c's
  folder compute `<`, a copy-paste of the `T_LT` case.  `4 >= 5` folds
  to 1.  Shared front end, affects every target.  Check the kit too.
* **cast of a sub-array** — `(char *)two[1]` with `char two[5][257]`
  loads the row's first four bytes as the pointer value.  make_rval
  leaves a sub-array flagged LVAL and the cast treats it as an object.
  Fixed by clearing LVAL, same as the whole-array case.
* **pointer-to-array parameter** — `type_canonical` decays
  `char (*a)[257]` as if it were an array, so the row size is lost and
  every `a[i]` addresses row 0.  Guarded with the PTR-vs-dimensions
  test make_rval already uses.  (Follow-on: passing an uncast 2-D
  array where `char (*)[N]` is expected now wants an explicit cast;
  type_pointerconv doesn't know the decay equivalence.)

bcrun (ours, but the vendored FUZIX copy shares it): `lib_eqop` never
handled the size-8 forms — `x /= 16` on a long long read and wrote 32
bits.  Plus capacity limits raised for real programs: NUM_NODES,
MAXLABEL (16 was too small for GOSUB return switches), CODEMAX,
DATAMAX, MAXSYM, STRMAX.

Optional, needs a judgement call: the `i_open`/`i_alloc` diagnostics
and free-list validation from `27a82379f`. The *diagnostic* half is
clearly worth it - the bare "i_open: bad disk inode" becomes ENFILE,
"File table overflow", which sends you to the wrong subsystem entirely.
The validation half costs a read per inode allocation and arguably
papers over corruption, so offer it separately and let Alan decide.

## Checked and ruled out - do not re-investigate

* **`timer_expired()`** - already fixed upstream in `2696c528a`, using
  `(timecmp_t)(timer_val - ticks.h.low) < 0`. Equivalent to ours. We
  did not have it because we are behind, not because it was missing.
* **The comma operator** - upstream's `backend.c` handles `T_COMMA`.
  That gap was in *our* new bytecode backend. Not upstream's bug.
* **`freeblk` memory corruption** (`9cfa24370`) - only the
  `CONFIG_BLKBUF_EXTERNAL` variant, which we do not compile. Ours uses
  `blkptr()` and is correct.
* **`swapvictim`** (`fc8292c51`) - only with `CONFIG_SWAPPER`, which we
  do not define, so `PFL_SWAPIN` is never set. Latent for us: the
  `continue` skips the `c++` in a do-while, so it would spin forever if
  ever enabled. Worth taking as insurance if we turn the swapper on.
* **`.thumb_func` on `switchin`** - upstream fixed it independently in
  `4fdd98d4b`. We already have the essential part.
* **libc fixes we cherry-picked on 2026-07-27** (strtol x4, fread,
  mntent, getpass, curses x4, cfmakeraw) are present by *content*.
  They still show in `HEAD..codeberg/master` because cherry-picks get
  new hashes - check content, not commit presence.

## Pre-emption: not proposed, and why

Lives entirely in `platform-rpipico/` - `isr_pendsv` and
`preempt_user` in `tricks.S`, `preempt_init`/`preempt_handler` in
`misc.c`. **No shared kernel file was changed for it.**

Technically upstreamable, since it is upstream's own platform
directory and the framework already anticipates it via `need_resched`.
Not sent because: it is a feature rather than a bug fix and wants
Alan's buy-in first; the ICI/IT guard is ARMv8-M only (an M0+ has no
ICI bits and abandons the instruction without base writeback, so on
RP2040 the guard is unnecessary or subtly wrong, untested); and it
references PC3 pieces (`usb_pump_stacked`, six `dbg_*` counters) that
would need stripping. If it comes up, open an issue describing the
approach first - do not surprise him with 150 lines of assembler.

## The real shared-code task is a rebase, not a PR

Our divergence from upstream in shared code is mostly **us being
behind**, not us having changed things. We have zero commits touching
`Kernel/process.c` or `Kernel/tty.c`; the diffs there are upstream
moving on - `b3de5ddc8` whitespace, `4dd953502` redesigning the swapper
(`swaptask`/`swapproc` where we still have the older `swapper`/
`swap_in`), `64241da98` removing a `used(flag)` we still carry.

`pc3` is ~179 commits ahead and ~354 behind. Worth rebasing before it
grows. The four older `cb-build-fixes` / `cb-libc-fixes` /
`cb-kernel-fixes` / `cb-userland-fixes` branches are **not** on current
master either and would need rebasing before they are proposable.

## Also outstanding, ours not theirs

Upstream added `-std=c99` to `Target/rules.armm0` and
`Library/libs/Makefile.armm0` (`0ab376d66`) because **gcc 15 defaults
to C23**, where `bool`/`true`/`false` become keywords. We have no
`-std=` at all and are on gcc 14.2 (C17), so we are fine today and
break on the next toolchain bump. Adapt rather than cherry-pick - our
rules target `cortex-m33`, not `Cortex-M0plus`. Needs testing: bbcbasic
uses global register variables and strict `c99` may not accept them.

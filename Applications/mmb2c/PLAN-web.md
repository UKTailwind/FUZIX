# PLAN-web: MMBasic's WEB commands under mmb2c and cc

Status: **DESIGN, for review.  No code has been changed.**

This is the piece of work the v0.19 release notes promised: "BASIC
cannot reach the network yet.  `mmbc` does not translate MMBasic's
`WEB` commands in this release; that is the next piece of work."

The reference implementation is the WebMite's, read in full for this
design: `PicoMite/net/` (WiFi.c the dispatcher, MMtcpserver.c the
server and the page substitution, MMTCPclient.c the client and TLS,
MMudp.c, MMMqtt.c, MMntp.c) plus `JSON$` in misc/Custom.c and the
`MM.INFO` network entries in core/MM_Misc.c.

---

## 1. The ground we build on

Almost everything below the BASIC surface already exists, and was
built with this design in mind.  `utils/tlsget.c` says it in its own
comment: *"the whole of the TLS-specific API is two lines ... which is
the point, and what lets mmbc emit the same calls for WEB OPEN TCP
CLIENT and WEB OPEN TLS CLIENT."*

What the kernel gives userland today (v0.19):

| Facility | How |
|---|---|
| TCP stream sockets | `socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)`, then ordinary connect/read/write/close on an fd |
| TLS | `IPPROTO_TLS` (254) instead of TCP, plus `ioctl(fd, SIOCTLSHOST, name)` before connect for SNI + certificate matching.  After that it is a plain fd |
| UDP | `SOCK_DGRAM`, sendto/recvfrom |
| Raw (ping) | `netlw_raw_*`, used by ping(8) |
| Server side | bind/listen/accept — httpd(8) is the proof |
| Non-blocking | `O_NDELAY` on the fd: accept/read/recv return −1/EAGAIN instead of sleeping (`run_sockfunc`, syscall_net.c:131).  This is the polling primitive; no select needed |
| CA bundle | `ioctl(/dev/sys, NETIOC_TLSCA, {buf,len})` — tlsca(8) |
| Wi-Fi join/status | `NETIOC_UP` / `NETIOC_STATUS` / `NETIOC_DOWN` on /dev/sys; `net_status` carries ip, mask, gw, dns[2], rssi, mac, link |
| DNS | userland: `Applications/netd/gethostbyname.c`, a ~250-line UDP resolver reading /etc/resolv.conf (which wifi(8) writes from the DHCP lease) |
| Clock | ntpdate(8) sets it; TLS cert validity reads it |

Machine-wide limit: **`NETLW_NSOCKET` = 8 sockets**, shared by every
process.  The WebMite's `MaxPcb` is also 8, which is a happy accident
we should not lean on — see §9.

What bcrun gives a compiled program: the `lib_fast` libcall table
(bcrun.c:2110) already has `open/read/write/close/lseek`, and the
mm_* table has `mm_errno`, `mm_run_pipe` (spawn a binary and read its
output), and the whole file/LONGSTRING runtime.  **It has no
socket/connect/bind/listen/accept/sendto/recvfrom/ioctl/fcntl.**
That short list is the only thing a compiled program cannot do today,
and it is the only thing this design adds to bcrun.

---

## 2. Architecture: where each piece lives

The rule is the one every family since the slimming has followed:
**bcrun gets only what must cross the process boundary; everything
with logic in it is `MMG_FN` statics in a program-side header**,
compiled into only the programs that use it, dropped by cc1 when
unused.

### 2.1 bcrun additions — nine one-line syscall wrappers

```c
static void lc_socket(void)   { A = socket(I(0), I(1), I(2)); }
static void lc_connect(void)  { A = connect(I(0), (void *)Pa(1), I(2)); }
static void lc_bind(void)     { A = bind(I(0), (void *)Pa(1), I(2)); }
static void lc_listen(void)   { A = listen(I(0), I(1)); }
static void lc_accept(void)   { A = accept(I(0), (void *)Pa(1), (void *)Pa(2)); }
static void lc_sendto(void)   { A = sendto(I(0), (void *)Pa(1), I(2), I(3), (void *)Pa(4), I(5)); }
static void lc_recvfrom(void) { A = recvfrom(I(0), (void *)Pa(1), I(2), I(3), (void *)Pa(4), (void *)Pa(5)); }
static void lc_ioctl(void)    { A = ioctl(I(0), I(1), (void *)Pa(2)); }
static void lc_fcntl(void)    { A = fcntl(I(0), I(1), I(2)); }
```

Nine `lib_fast` rows, ~60 lines, an estimated 400–500 bytes of text.
`read/write/close` for the data path already exist.  Program memory is
machine-addressable on this port, so `Pa()` hands the kernel a pointer
`valaddr` can check, exactly as the i2c wrappers do.  Errno is already
visible through `mm_errno`.

Version skew is the one-directional rule we already live by: an old
program imports none of these names and runs on the new bcrun; a new
program on an old bcrun is refused by name at load.

**A deliberate side effect worth its own release-note line:** these
libcalls give every on-board `cc` program sockets, not just BASIC.
The C manual gains a short sockets section (with the constants —
there is no fcntl.h or sys/socket.h program-side; the values go in
the manual and in mmb_net.h, the same way every other mmb header
carries its own constants).

Nothing else goes in bcrun.  No DNS, no HTTP, no page substitution,
no server state.  All of that is per-program logic.

### 2.2 The header families and their flags

Following the per-family granularity (a client-only program must not
carry the server):

| Header | Flag | Contents | Est. source |
|---|---|---|---|
| `mmb_net.h` | (pulled by the others) | constants (AF_INET, SOCK_*, IPPROTO_TCP/UDP/TLS, O_NDELAY, SIOCTLSHOST, NETIOC_*), sockaddr_in building, dotted-quad parse, the DNS resolver (gethostbyname.c re-expressed as MMG_FN statics), `/dev/sys` ioctl helpers, MM.INFO's net answers via NETIOC_STATUS | ~350 lines |
| `mmb_webc.h` | `uses_webclient` | OPEN TCP/TLS CLIENT/STREAM, CLIENT REQUEST/READ/WRITE/STREAM, CLOSE, TLS CA/NOVERIFY, the stream ring buffer + its poll hook | ~300 lines |
| `mmb_webs.h` | `uses_webserver` | the listener, 8 connection slots, the accept/read poll hook, TCP READ/SEND/CLOSE, TRANSMIT FILE/CODE, TRANSMIT PAGE + substitution engine | ~400 lines |
| `mmb_udp.h` | `uses_udp` | UDP SEND, the server port, the recvfrom poll hook, MM.ADDRESS$/MM.MESSAGE$ buffers | ~120 lines |
| `mmb_json.h` | `uses_json` | `JSON$()` as a streaming path-walker (§7) | ~250 lines |
| `mmb_mqtt.h` | `uses_mqtt` | deferred — design sketch in §8 | ~350 lines |

Include order: after `mmb_gpio.h`, **before `mmb_int.h`** — the poll
hooks hang off `mm_int_poll()` behind the headers' own include guards,
the established mmb_sprite.h pattern.  `mmb_net.h` first, then webc,
webs, udp, mqtt; `mmb_json.h` is independent and can sit with the
other pure families.  Any armed WEB interrupt sets `uses_interrupts`.

Both translators get the flags and the emission sites, mmb2c.py and
mmbc/ together, cgate at 0 diff lines — the six-family recipe from the
slimming, once more.

### 2.3 Interrupts are polls, here as everywhere

The WebMite fires its three network interrupts from check_interrupt
(MM_Misc.c:10015-10029): one-shot flags — `TCPreceived`,
`UDPreceive`, `MQTTComplete` — set by lwIP callbacks, tested after
every statement.  That is precisely the `mm_int_poll` model.

Here the poll hook does the receiving itself, O_NDELAY and decimated
(the console-key pattern, ~5 ms):

- **server hook**: accept into a free slot; per slot with no pending
  request, try a read; data read sets the slot's `inttrig` and the
  one-shot `web_rx` flag → fire the armed handler once.
- **stream hook**: drain the client socket into the ring buffer,
  advancing the write index, discarding oldest on overrun — the
  WebMite's exact ring semantics (MMTCPclient.c:179-187).
- **udp hook**: recvfrom into MM.MESSAGE$/MM.ADDRESS$ buffers, set
  `udp_rx` → fire.

Blocking waits inside commands (REQUEST's response wait, OPEN's
connect timeout) are serviced waits: O_NDELAY + `mm_int_poll` +
deadline on `MMI_US()`, the mmb_wait.h pattern — remembering usleep's
decisecond floor, so pacing is by poll-loop, not by sleeping.

---

## 3. Command surface and fidelity

The dialect rule stands: replicate MMBasic exactly, and where we
cannot, an honest documented gap that errors loudly beats a silent
divergence.

### 3.1 Client — exact replication

| WebMite | PC3 translation |
|---|---|
| `WEB OPEN TCP CLIENT host$, port [,timeout]` | resolve (3-dots-and-parses = literal, else DNS), `socket(STREAM, IPPROTO_TCP)`, connect with timeout |
| `WEB OPEN TLS CLIENT host$, port [,timeout]` | same with `IPPROTO_TLS`; `SIOCTLSHOST` before connect **only when host$ is not a dotted quad** — the WebMite passes SNI only for names (MMTCPclient.c:461), and so do we |
| `WEB OPEN TCP STREAM` / `TLS STREAM` | same opens, ring-buffer receive mode |
| `WEB TCP CLIENT REQUEST req$, a%() [,timeout]` | **discard any unread socket data first**, then write req$, wait for first data (timeout), then keep draining for a further 500 ms — the WebMite's exact two-phase timing (MMTCPclient.c:586-594).  The discard replicates a load-bearing WebMite behaviour: data arriving when no buffer is armed is dropped (tcp_client_recv:149-155), so between REQUESTs the server's unsolicited output vanishes.  On the PC3 the kernel would faithfully buffer it — and every SMTP response would then be off by one (the 220 greeting would come back as the answer to EHLO).  retic.bas's MailSend depends on the drop |
| `WEB TCP CLIENT READ a%() [,timeout]` | the no-transmit form (the SMTP-greeting reader) |
| `WEB TCP CLIENT WRITE ls%() [,timeout]` | chunked write loop.  The WebMite's 1.5K in-flight throttle exists because lwIP pbufs come off the WebMite's tiny C heap; on the PC3 the kernel paces the writer, so a plain write loop is correct — same observable behaviour, different reason |
| `WEB TCP CLIENT STREAM req$, a%(), rd, wr` | write req$, arm the ring (rd/wr are BYREF integer indexes, as there) |
| `WEB CLOSE TCP CLIENT` | close the fd |
| `WEB TLS CA file$` | read the file, `NETIOC_TLSCA` — tlsca(8)'s exact recipe including the +1 NUL for PEM |
| `WEB TLS NOVERIFY` | `NETIOC_TLSCA` with {NULL, 0} |

Buffer convention throughout: integer arrays with the byte count in
element 0 and payload from element 1 — the LONGSTRING convention the
runtime already speaks.

One connection at a time, `TCP_CLIENT` replaced on re-open — the
WebMite's model, kept.

### 3.2 Server — exact replication, one spelling divergence

The WebMite starts its server from a **persistent option** (`OPTION
TCP SERVER PORT n`, saved to flash, server started at Wi-Fi connect).
The PC3 has no per-program persistent option store, and a compiled
program must own its own sockets.  **Divergence (for review):** the
listener is opened by an explicit statement:

    WEB TCP SERVER PORT n        ' opens the listener, up to 8 connections
    WEB UDP SERVER PORT n        ' likewise for UDP

The translator additionally accepts `OPTION TCP SERVER PORT n` /
`OPTION UDP SERVER PORT n` in a program as the same statement, so
WebMite listings move across unedited.  Everything downstream is
byte-faithful:

| WebMite | PC3 translation |
|---|---|
| `WEB TCP INTERRUPT sub` | arm the handler (one-shot `web_rx` flag, §2.3) |
| `WEB TCP READ n, a%()` | copy slot n's buffered request into a%(), count in [0]; **zero the array and return if nothing pending** (inttrig semantics, MMtcpserver.c:842-846); "array too small" checked the same way |
| `WEB TCP SEND n, a%()` | write the payload to slot n's fd |
| `WEB TCP CLOSE n` | close slot n |
| `WEB TRANSMIT FILE n, f$, mime$` | `HTTP/1.1 200 OK\r\nServer:CPi\r\nConnection:close\r\nContent-type:<mime>\r\nContent-Length:<sz>\r\n\r\n` then the file, chunked; 404 (`HTTP/1.0 404\r\n\r\n`) when absent; close |
| `WEB TRANSMIT CODE n, code` | the WebMite's literal trick reproduced: the 3-digit code written over "404" in `HTTP/1.0 404\r\n\r\n`; close |
| `WEB TRANSMIT PAGE n, f$ [,bufsize]` | §4 |

Faithful details worth naming so review can veto them:

- **Null bytes in a received request are replaced with spaces**
  (MMtcpserver.c:212-214) — observable, so replicated.
- **TRANSMIT PAGE ends the body with two CRLF pairs** inside the
  counted length.  The reference emitted them reversed (10,13,10,13,
  MMtcpserver.c:711-714) for years; that is a bug, now fixed in the
  PicoMite source rather than replicated — the PC3 emits `CR LF CR
  LF`, and the side-by-side test runs against a WebMite carrying the
  fix.
- Connection numbers are 1..8, `MM.INFO(MAX CONNECTIONS)` = 8.
- Stale connections: the WebMite reaps a slot after
  `Option.ServerResponceTime` ms.  We reap in the poll hook after a
  fixed 5000 ms (their default), no option.

### 3.3 The machine-level commands — mapped, not duplicated

These act on the machine, not the program, and the machine side
already exists as proven binaries and ioctls.

| WebMite | PC3 design |
|---|---|
| `WEB CONNECT` (no args) | `NETIOC_STATUS`; if link is down, error "WIFI not connected".  (The WebMite reconnects from saved options; our persistent join lives in /etc/wifi.conf + wifi(8) at boot) |
| `WEB CONNECT ssid$, pass$ ...` | `NETIOC_UP` join, **not persisted** — a program joins for its run; /etc/wifi.conf stays the owner of the boot-time answer.  Divergence, documented |
| `WEB NTP [offset [,server$]]` | spawn `ntpdate -s` via the existing `mm_run_pipe` machinery; apply offset by setting the clock to UTC+offset afterwards, because MMBasic's clock *is* local time.  No new code beyond argv building |
| `WEB PING addr$ [,count]` | spawn ping(8) via mm_run_pipe, print its output |
| `WEB SCAN [a%()]` | **deferred**: needs a new `NETIOC_SCAN` kernel ioctl (cyw43_wifi_scan + result buffering).  Modest, but kernel work with no dependent below it — its own later item |
| `MM.INFO(IP ADDRESS)` | NETIOC_STATUS → dotted quad (mmb_net.h) |
| `MM.INFO(TCP PORT / UDP PORT / MAX CONNECTIONS / WIFI STATUS)` | program-side state / NETIOC_STATUS |

### 3.4 UDP

`WEB UDP SEND ip$, port, msg$` — resolve, one sendto, exactly as
MMudp.c (which creates and destroys a send pcb per call; ours reuses
one fd).  Receive side per §2.3; `MM.ADDRESS$` and `MM.MESSAGE$` are
program-side buffers filled by the poll hook, MMudp.c's
length-clamped copy semantics included.

---

## 4. WEB TRANSMIT PAGE — variable substitution

The WebMite algorithm (MMtcpserver.c:609-757), which is the spec:

1. Stream the file char by char, dropping 0x1A (xmodem padding).
2. `{` opens an expression; `{{` emits a literal `{`.
3. The text up to `}` is handed to the **tokeniser and expression
   evaluator** — any MMBasic expression, evaluated with
   `OptionExplicit` forced off.
4. The value is appended: float via `FloatToStr(v, 0,
   STR_AUTO_PRECISION, ' ')`, integer via `IntToStr`, string raw.
5. Two CRLF pairs appended (see §3.2 — the reference's reversed
   LF CR bytes were a bug, fixed upstream, not replicated);
   `Content-Length` counts the finished body; headers as §3.2; send;
   close.

Steps 1, 2, 4, 5 replicate mechanically — the runtime already owns
byte-identical `mm_float_to_str`/`mm_int_to_str`.  Step 3 is the
design problem: **a compiled program has no tokeniser and no
evaluator.**

An earlier draft of this section proposed a runtime table of global
scalar names as stage 1.  **The acceptance application killed it**
(§12): retic.bas's pages substitute locals of the calling sub
(`{Status(3)}`, `{ErrorMsg1}`), sub parameters inside subscripts
(`{Title(pnbr)}`, `{RunDOW(0,pnbr)}`), a `Const` (`{Ver}`), a builtin
(`{mm.ver}`), and a genuine expression (`{str$(pnbr + 1)}`).  No
table of global addresses can see a caller's stack locals.  What can
— naturally, and with full fidelity — is code compiled **at the call
site**, because MMBasic's evaluator runs in exactly that scope: the
variables visible to `{...}` are the variables visible where WEB
TRANSMIT PAGE was called.

**The design: per-call-site inline compilation.**  When the filename
is a string literal, the translator opens the page at translate time,
extracts every `{...}`, normalises each (whitespace stripped outside
string literals, case folded — index.html pads its braces with
spaces for source alignment), dedupes, and compiles each expression
through its normal expression pipeline **inline in the emitted
statement**, where the enclosing sub's locals and parameters are
simply in scope:

```c
{   /* WEB TRANSMIT PAGE conn, "/config.html" */
    static const char *const mm_websub_3[] = { "TITLE(PNBR)",
        "STR$(PNBR+1)", "SW(0)", ... };
    struct mm_webpg pg;
    int i;
    mm_webpg_start(&pg, conn, "config.html");
    while ((i = mm_webpg_next(&pg, mm_websub_3,
                              sizeof mm_websub_3/sizeof *mm_websub_3)) >= 0)
        switch (i) {
        case 0: mm_webpg_put_s(&pg, title[(int)pnbr]); break;
        case 1: mm_webpg_put_s(&pg, mm_str_i(pnbr + 1)); break;
        case 2: mm_webpg_put_s(&pg, sw[0]); break;
        /* each case is the ordinary compiled form of the expression,
           routed through the step-4 formatter for its type */
        }
    mm_webpg_send(&pg);
}
```

The engine (`mm_webpg_*`, MMG_FN statics in mmb_webs.h) streams the
file **at run time**, copying text and handling `{{`/0x1A itself; at
each `{...}` it normalises the text the same way and returns its
index in the table.  Matching by text rather than by position means a
page reorganised on the board after compilation keeps working, as
long as every expression it uses appears somewhere in the compiled
table.  Per-call-site tables are what make scoping exact: the same
page served from two subs compiles twice, each against its own scope,
which is precisely MMBasic's semantics.

Consequences, all acceptable and all reviewable:

- **The filename must be a string literal** (stage 1): a computed
  name cannot be pre-scanned and is a translate-time error.  Both
  retic.bas call sites and every published example use literals.
- **The page must exist at translate time**, next to the program
  (after §12's path mapping): mmb2c.py reads it on the host, mmbc
  reads it from the card.  Both must normalise and order the table
  identically — cgate covers it, and a fixture page goes into the
  gate suite.
- **A page edited on the board with a *new* expression** fails its
  lookup at run time.  Open question, a divergence either way: the
  WebMite (OPTION EXPLICIT forced off) silently renders `0` for an
  unknown name; the draft choice is **error naming the expression** —
  a typo rendering 0 into a served page is the silent-divergence
  shape the triage rule exists for — but review should own it.

Costs: the pre-scan and emission implemented in both translators
(byte-identical, cgate-gated), and the table strings in the program
image — for retic.bas's three pages, ~94 expressions, ~1.5 KB.

---

## 5. DNS, and what "resolve" means everywhere

Every `host$` argument accepts a dotted quad (the WebMite's
three-dots-and-parses test, kept) or a name.  Names go through the
resolver in mmb_net.h: netd's gethostbyname.c re-expressed as MMG_FN
statics — build the query, sendto port 53 of the nameserver read from
/etc/resolv.conf, recvfrom with timeout/retry, parse the first A
record.  ~200 lines, compiled only into programs that name a host.
No copy of it enters bcrun.

---

## 6. MM.MESSAGE$, MM.ADDRESS$, MM.TOPIC$

Program-side buffers in mmb_udp.h / mmb_mqtt.h, returned through the
scratch-temp convention every string function already uses.  Length
byte first, clamped copies, exactly as `messagebuff`/`addressbuff`/
`topicbuff` behave.

---

## 7. JSON$()

The WebMite builds a full cJSON tree per call, walks a
`field.field[idx].field` path, formats the leaf (number via
IntToStr-if-integral else FloatToStr; bool → "true"/"false"; string
raw; object/missing → error "Not an item"), frees the tree.

Carrying cJSON program-side is ~40K of source for one query shape, so
mmb_json.h implements the same *observable* function as a **streaming
path-walker**: scan the JSON text once per call, descending into the
named object members and array indexes as they stream past, no tree.
Same path grammar (32-char field limit and all), same leaf
formatting, same errors.  ~250 lines.  This is an implementation
divergence with identical surface — flagged here so review can
insist on cJSON instead if byte-fidelity of error behaviour on
*malformed* JSON matters ("Invalid JSON data" arises from a different
detector).

---

## 8. MQTT — designed now, built later

Deferred by request, but the shape is fixed by the rest of the
design.  **Userland, program-side, over the existing sockets** — the
lwIP MQTT app the WebMite wraps is kernel code we do not want; MQTT
3.1.1 client packets are simple to build by hand.

- `WEB MQTT CONNECT ip$, port, user$, pass$ [,int]` — TCP (or
  IPPROTO_TLS when port = 8883, the WebMite's rule) + CONNECT packet;
  empty user/pass omitted entirely (their mosquitto lesson,
  MMMqtt.c:278-288).  Client id `"PC3-"` + MM.INFO(ID) equivalent.
- `WEB MQTT PUBLISH topic$, msg$ [,qos [,retain]]` (defaults 1,1 as
  there), `SUBSCRIBE topic$ [,qos]`, `UNSUBSCRIBE topic$`, `CLOSE`.
- The poll hook parses inbound packets: PUBLISH → topicbuff/
  messagebuff + the one-shot flag → interrupt; PINGREQ keepalive on
  the poll clock; QoS 1 PUBACK inline, QoS 2's handshake in the hook.
- ~350 lines in mmb_mqtt.h, **zero bytes anywhere else** — which is
  the point of settling the architecture first.

---

## 9. Risks and traps, named in advance

- **Eight sockets machine-wide, not per-program.**  A BASIC server
  holding 8 slots starves every other process (and itself, of a
  client socket).  MM.INFO(MAX CONNECTIONS) stays 8 to match the
  WebMite, but the manual must say the pool is the machine's.
  Raising NETLW_NSOCKET costs kernel RAM per slot; not proposed now.
- **fcc dialect** (the known list): no anonymous-struct arrays, cc2
  node-pool parity, no system headers program-side — mmb_net.h
  defines every constant itself and the structs stay flat.
- **Size cliffs**: the server + substitution headers land in
  programs; keep bodies out of `main` (TMAX cliff) and watch the
  1.98× native expansion on the first board build.
- **Host gates can lie about the board** (glibc vs Fuzix libc, gcc -E
  vs the board cpp).  The gates below include a board pass on
  everything, per the side-by-side rule.
- **usleep's decisecond floor** — every wait is a poll loop with a
  deadline, never a sleep.
- **The one-writer FIFO/daemon lessons don't apply** — sockets are
  fds with kernel state, closed by exit; no per-session C-module
  state survives into the next program (the root-pointer lesson is
  moot here, but the slots table still initialises itself on first
  use, not at load).

## 10. Testing

1. `make check` — host harnesses: loopback client/server over real
   Linux sockets (the generated C's socket calls are glibc's on the
   host), substitution engine against fixture pages including `{{`,
   0x1A, all three types, the CRLF CRLF tail, byte-compared.
2. `mmbc/cgate.sh` — 0 diff lines, both translators, every new flag.
3. `fcc/fcctests.sh` + `fcc/qemutests.sh` — load/refusal of the new
   libcall names, dialect-compiles of every new header.
4. Board acceptance (the authority): BASIC client fetch from a local
   python server; TLS fetch to a real site with the CA loaded (the
   v0.19 ladder exists for when it fails); BASIC server driven by a
   python client from the PC — request read via interrupt, TRANSMIT
   PAGE response captured and byte-compared against a WebMite serving
   the same page and variables (side-by-side is the authority); UDP
   between two PC3 processes; two programs each owning sockets at
   once (the per-process win the WebMite cannot test).
5. Skew: a pre-web .bc on the new bcrun runs; a web .bc on the old
   bcrun is refused by name.

## 11. Staged implementation

Two PC3s are available for the campaign, on COM14 and COM17, so
every network stage can be proven **board-to-board** — each board is
the other's test peer, no PC-side server required (though one is
used where it makes a failure easier to read).  Start small: each
stage is both translators + host gates + cgate + a board pass, landed
before the next begins — one change at a time.

**Stage 0 — bcrun: the nine libcalls** (§2.1).  **DONE 2026-08-21**
(+1,120 bytes on the board binary).  One host wrinkle: Fuzix libc has
no send(), so lc_sendto's no-address form is write().

**Stage 1 — UDP, two boards talking.  DONE, BOARD-PROVEN
2026-08-21**: loopback byte-perfect on both PC3s, then 5/5 pings and
5/5 acks between them with MM.ADDRESS$/MM.MESSAGE$ correct at both
ends.  What the stage flushed out, kept here because later stages
inherit all of it:

- **The kernel never disabled radio powersave.**  The CYW43 default
  dozes between beacons; the AP drops unicast to a dozing client.
  Measured: 2 of 5 datagrams lost board-to-board, ping jitter
  3-300 ms (avg 140).  TCP had been hiding it behind retransmits
  since v0.19.  Fixed where the WebMite fixes it (WiFi.c:258):
  `cyw43_wifi_pm(NO_POWERSAVE)` at radio-up in net_cyw43.c.  After:
  avg 1 ms, and the loss gone.  Kernel change, both boards reflashed.
- **The first datagram to a new peer can vanish in the ARP window**
  (lwIP queues one packet during resolution; ours didn't survive).
  Same stack, same defaults, same behaviour as the WebMite; warm-ARP
  runs are 5/5.  Documented, not chased.
- **Hosted-native Timer counted CPU time** (clock()), so a
  Timer-bounded PAUSE loop never timed out under make check — wall
  clock now, gates only; bcrun paths (board and host) always used
  the kernel/wall clock via MM_HOSTED.
- **On the board Timer counts from kernel boot** until `Timer = 0`;
  on the host, from process start.  MMBasic idiom always resets
  first, but a test that forgets exits its bounded loop instantly on
  a long-up board.
- .expected files are CRLF (bcrun output raw); make check strips \r,
  fcctests does not.
- Gates at landing: make check all-ok, cgate 0 diff lines, fcctests
  64/0, qemutests 65/0 (webudp runs as native Thumb under qemu, so
  the libcall ABI translation is proven in both execution modes).  The smallest end-to-end
proof of the whole architecture: mmb_net.h's core (constants,
sockaddr building, dotted-quad parse, byte order, O_NDELAY helpers)
+ mmb_udp.h + `WEB UDP SEND` / `WEB UDP INTERRUPT` / `WEB UDP SERVER
PORT` + `MM.ADDRESS$` / `MM.MESSAGE$`, and the first mm_int_poll
hook.  Dotted-quad addresses only — the resolver waits for stage 2.
Board test: board A sends, board B's interrupt handler echoes with
MM.ADDRESS$/MM.MESSAGE$, then both run the same duplex program; a
python peer on the PC cross-checks framing.  This stage exercises
the libcalls, a header family, both translators' emission, the poll
hook, and the interrupt machinery — every structural risk, at
datagram simplicity.

**Stage 2 — TCP client.  DONE, BOARD-PROVEN 2026-08-21.**  The DNS
resolver into mmb_net.h (gethostbyname.c as statics: resolv.conf
cached, A-queries, resend at 1.5 s, bounds-checked reply walk, and
wrap-safe deadline macros over the 31-bit mm_us); mmb_webc.h with
OPEN TCP CLIENT / CLIENT REQUEST (drop-before-write, then the
reference's first-data timeout + fixed 500 ms drain) / READ / WRITE /
CLOSE; WEB UDP SEND resolves names through the same door.  Board:
BASIC GET from the other board's httpd(8) (200 OK, body verified);
example.com fetched by name; then retic.bas's own OWM geocoding
request verbatim — reversed LF CR terminator included — 2,650 bytes
of JSON collected across segments.  Stage-1 webudp.bc (old headers)
still byte-perfect under the new bcrun: the skew promise, observed.

What stage 2 settled, inherited by the rest:

- **No BASIC interrupts fire inside client waits** — MMBasic's wait
  loops pump lwIP but never check_interrupt, so bare deadline loops
  are the faithful shape and mmb_webc.h needs nothing from mmb_int.h.
- **bcrun ignores SIGPIPE**: a write on a peer-closed socket comes
  back as an error a program can see, as the WebMite's write
  failures do — not a signal that kills the process.
- **No errno crosses the libcall boundary**, so a refused connection
  costs the open timeout rather than failing fast — much what the
  WebMite's refused open does.  Revisit only if a real program hurts.
- The client's observable semantics are pinned by
  tests/webcharness.c against a real forked server (greeting via
  READ, two-segment drain join, byte-verified 3000-byte WRITE, the
  discard rule); webtcpe.bas compiles the family through the real
  fcc dialect in every gate and checks the timeout error path.
- Gates at landing: make check ok, cgate 0, fcctests 65/0,
  qemutests 66/0.

**Stage 3 — TLS client + email.  CODE DONE, TLS BOARD-PROVEN
2026-08-22; the Gmail send awaits credentials on the card.**  OPEN
TLS CLIENT is the one-flag difference tlsget.c promised (IPPROTO_TLS
+ SIOCTLSHOST when the host was typed as a name); WEB TLS
CA/NOVERIFY is tlsca(8)'s recipe program-side (bundle read, +1 NUL,
NETIOC_TLSCA on /dev/sys, mm_lheap'd and freed after the kernel's
parse) - machine state, as tlsca(8) is.  Board: /etc/ca.pem loaded,
www.google.com over 443 through a VERIFIED handshake (8 KB, 200 OK)
- and the negative that makes "verified" mean something:
expired.badssl.com REFUSED.  samples/gmail.bas is the §12.4 recipe,
reading credentials from /etc/gmail.conf on the card (three lines:
from-address, to-address, 16-char app password), with retic's own
pure-BASIC base64.  PROVEN 2026-08-22: real credentials on COM17,
compiled and run from the card, Gmail answered `250 2.0.0 OK` and
the mail arrived — the §12.4 loose end is closed.

The stage's find, retroactive to stages 1-2: **a trapped raise
returns.**  retic wraps every WEB call in ON ERROR SKIP, and
mm_error comes back when trapping is armed - so every raise in the
three net headers now cleans up first and returns after (the open's
timeout raise would have looped for ever on a dead fd).  Proven on
the board: the expired-cert test traps the raise and continues.
Gates at landing: make check ok, cgate 0, fcctests 66/0, qemutests
67/0.  TLS STREAM stays stage 6.

**Stage 4 — server.  DONE, BOARD-PROVEN 2026-08-22.**  mmb_webs.h:
one listener + 8 slots behind an O_NDELAY poll (accept, per-slot
reads, 5 s idle reaping), hooked into mm_int_poll AHEAD of UDP (the
WebMite's order); READ self-polls so an interrupt-less reader loop
still accepts, the way ProcessWeb always pumps.  TCP
INTERRUPT/READ/SEND/CLOSE, TRANSMIT CODE (digits over "404") and
TRANSMIT FILE (the reference's Server:CPi header byte for byte),
plus MM.INFO(IP ADDRESS) via NETIOC_STATUS and MM.INFO(MAX
CONNECTIONS) — both of which retic's handler uses.  Board: a
compiled server in retic's exact handler shape served the PC's
Invoke-WebRequest (204 root, 200 + exact body for the file) and the
other board's stage-2 BASIC client (200, body verified) — BASIC
serving BASIC.  MM.INFO(IP ADDRESS) answered live on the board.

Named findings:
- A one-process client-REQUEST-to-own-server round trip DEADLOCKS BY
  DESIGN on both firmwares (interrupts fire at statement
  boundaries), so live paths are proven by websharness.c's forked
  client (request delivery, null-to-space, consume-once, the
  TRANSMIT FILE bytes, SEND, slot cycling) and by the two-board run;
  webservz.bas gates the peer-less semantics in all three execution
  modes.
- The uusend cwd trap struck again: the server 404'd its file
  because the session cwd was /root/cc and hello.txt was in /root —
  TRANSMIT FILE resolves relative to the PROGRAM'S cwd, which the
  migration guide must say out loud.
- Gates at landing: make check ok, cgate 0, fcctests 67/0,
  qemutests 68/0.

**Stage 5 — TRANSMIT PAGE.  DONE, BOARD-PROVEN 2026-08-22.**  The §4
call-site substitution, exactly as designed: the translator reads
the page at translate time (next to the program), compiles every
{expression} through the normal pipeline INLINE at the statement -
globals, sub locals, by-ref parameters and function calls all
resolve, proven by the webpage.bas gate emitting v_n / v_loc /
*p_pnbr / mm_str_i(...) - and emits a __mmwebsub_N table of
normalised texts that mm_webpg_next matches braces against at run
time.  Both translators, byte-identical on the FIRST diff; the
engine (mm_webpg_* in mmb_webs.h) carries the reference's whole
observable algorithm: 0x1A dropped, '{{' literal, expression to the
FIRST '}' string-blind, float/int/string formatted exactly, two
CRLF pairs in the counted body (correct order - the fixed one), one
Content-Length'd 200.

Board proof, the user's suggestion: **live BMP180 readings served
into a page** - bmp180web.bas reads the sensor on the QWIIC bus
every 2 s and the page substitutes { Str$(temperature%/10, 4, 1) },
{ Str$(pressureinHpa, 4, 1) }, {UT%}, {UP%}, {nreads%}; two PC
fetches showed 24.1 -> 24.2 C, moving raw values, and the refresh
counter advancing 2 -> 13 - re-evaluated per request, not cached.
The on-board mmbc did the pre-scan from the card itself.

Implementation notes that will matter later:
- mmbc tokenizes fragments through tokenize_frag(), a non-resetting
  lexer entry: tokenize() resets the scratch pool the suspended
  line's token texts live in.
- The emitted loop wraps each substitution in its own
  mm_mark/mm_release - ninety string expressions in one statement
  would otherwise pile scratch temps until the NEXT statement's
  release.
- Truncation: page + bufsize (default 4096) is the capacity, excess
  dropped - kinder than the reference's hard stop at +256
  (MMtcpserver.c:652), noted as a divergence in its favour.
- The side-by-side against a real WebMite serving retic's pages
  moves to stage 8 with the retic run itself.
- Gates at landing: make check ok, cgate 0, fcctests 68/0,
  qemutests 69/0.

**Stage 6 — JSON$.  DONE, BOARD-PROVEN 2026-08-22** (the STREAM ring
forms — which retic never uses — moved to the later list).
mmb_json.h is fun_json's surface as a streaming path-walker: strict
structural validation first ("Invalid JSON data"), then the
reference's exact path state machine — intermediate fields
case-sensitive, the FINAL field case-insensitive, the walk always
ending in a field lookup (their jsontest.bas documents the
path-ends-in-index → "" consequence), [n] as the n'th CHILD (object
or array, GetArrayItem's own tolerance), escapes decoded \uXXXX to
UTF-8 with surrogate pairs, number/bool/string/null/missing leaves
formatted exactly, object leaf the one raise.  jsonpath.bas pins
every vector in all three execution modes AND on the board; the
WebMite's own jsontest.bas ran on the PC3 against live open-meteo —
TLS fetch, header strip, seven paths incl. the UTF-8 degree sign,
ten stress parses at 0.94 ms each, no drift.

What the stage flushed out — a long chase, all keepers:

- **errno now crosses as the `neterr` libcall** (Fuzix numbering;
  hosted bcrun maps EALREADY/EINPROGRESS/ECONNRESET; EAGAIN is 11 on
  both worlds).  The client's polls needed it: a refused connect now
  fails fast, and a reset (or a TLS close-notify racing a reply)
  ends a collect instead of reading as "still waiting" to the
  deadline.  Stage 3's deferral, called due.
- **The client's polls now SLEEP after a 250 ms fast window**
  (mmw_poll_pause): mm_pause under 100 ms spins (usleep's decisecond
  floor), and a measured 6.5 M-poll spin starved the kernel's TLS
  receive where a blocking read succeeded — and 30 s of spin was
  hostile to every other process anyway.
- **An M-string slice fed to mm_atof MUST be NUL-terminated**: the
  board's conversion reads to a terminator, and a stale one made
  every number leaf inherit the PREVIOUS leaf's tail digits (16.0
  parsed as 16.01147, each value chaining the last's suffix) —
  invisible on the host, where the fixture gate stayed green.  The
  clean fixture run on the board plus one live-vs-PC document diff
  is what cornered it.
- Open-meteo intermittently sits on the heavy 16-day × 6-variable
  query for minutes (its 503 arrived once in many tries); the
  samples/jsontest.bas here trims to 3 days × 2 variables, which
  answers reliably.  Not our stack: proven by the same client
  collecting google's 42 KB and every trimmed variant.
- tlsget(8)'s req[256] OVERFLOWS on a ~200-char path - it hung
  where BASIC was blamed.  Worth its own small fix.
- Gates at landing: make check ok, cgate 0, fcctests 69/0,
  qemutests 70/0.

**Stage 7 — the rest of the surface.**  WEB NTP / PING / CONNECT
mappings, MM.INFO entries, and the §12.2 non-WEB gaps: OPTION
ESCAPE, MATH BASE64, MM.INFO(UPTIME), path mapping.

**Stage 8 — retic.bas** (§12): migrate per §12.4, run on a board
with real schedules and the web UI driven from a browser.  The
milestone that says the family is done.

**Stage 9 — manuals**: user manual (networking-from-BASIC chapter +
the Migrating-a-WebMite-program section), C manual (sockets for cc
users), coverage list regeneration.

Later, separately: MQTT (§8), WEB SCAN's kernel ioctl, the
counting-input kernel facility (§12.3), computed page filenames.

---

## 12. The acceptance application: retic.bas

The target that decides whether this design is functionally complete
is Geoff Graham's reticulation controller (`SDBackup/water/`:
retic.bas V1.3, 49 KB, plus index/config/setup.html) — a real,
shipped WebMite product exercising the whole surface at once: the
HTTP server with interrupt-driven dispatch over all 8 connections,
GET and POST parsing in LONGSTRINGs, TRANSMIT PAGE on three pages
with ~94 distinct substitutions (39 index, 37 config, 18 setup),
TRANSMIT CODE, TCP CLOSE, the TCP
client against api.openweathermap.org, `JSON$` with dotted paths
(`sys.sunrise`), SMTP mail through the REQUEST drop-semantics
(§3.1), `WEB NTP tz` retried in a loop, MM.INFO(IP ADDRESS / MAX
CONNECTIONS), and beneath the web: SETTICK, SETPIN, ON ERROR SKIP n
throughout, file I/O for its settings, and heavy DATE/TIME/Epoch
arithmetic in the scheduling loop.

**Verdict from the review: functionally coverable.**  Every WEB use
in the program maps onto §3 as designed — and the program is the
evidence that forced §4 into its call-site form and §3.1's
discard-before-write.  Size is not the blocker either: at 49 KB of
source with no graphics it sits in picofrog's class (41 KB → 225 KB
native .bc), under robots.bas (70 KB, ships and runs); the process
budget that held robots holds this.

### 12.1 What it needs from this plan, verbatim

Server family (§3.2), client family (§3.1) including the discard
rule, page substitution (§4) with locals/parameters/consts/builtins
in scope, `JSON$` (§7), `WEB NTP` with a timezone argument (§3.3),
MM.INFO entries (§3.3), and one added line at the top of the program
— `WEB TCP SERVER PORT 80` — standing in for the WebMite's saved
option (§3.2).

### 12.2 Gaps it exposes OUTSIDE the web family (all small)

| Gap | Used for | Disposition |
|---|---|---|
| `OPTION ESCAPE` | `\q` etc. in nearly every string literal | translator-only: unescape string literals at parse time, both translators.  Required |
| `MATH BASE64 ENCODE` | SMTP AUTH | already on COVERAGE.md's on-demand list; a small mmb_math.h addition.  Required |
| `MM.INFO(UPTIME)` | shown on index.html | trivial (the microsecond clock already exists).  Required |
| `WATCHDOG n` | wedge recovery | accept and **no-op with a translate-time warning**.  The RP2350 watchdog belongs to the kernel on a multi-process machine; restart-on-death is an rc loop here.  Honest divergence, documented — review may prefer an exit(1)-on-expiry timer instead |
| `CPU RESTART` | reboot when WiFi never comes up | re-exec self (argv[0]) — process-level restart is what it means here |
| `MM.INFO(DISK SIZE)` | log rotation only | small; can trail |
| `Option Autorun On` | WebMite autorun | accept-and-ignore; autorun is an rc line on this machine |
| Path forms `A:/settings.dat`, `/index.html` | config + pages | map drive prefixes and a leading `/` to the program's directory — confirm the existing file runtime's drive handling, extend if needed |
| `ConnData()` | scanner filtering in the 404 path | **in no reference we hold** — absent from the PicoMite tree and its manual (retic V1.3 targets a newer WebMite).  The request text is already in `b()` at that point; disposition: implement nothing, patch the one line to use `b()` when porting the app, and revisit if a reference lands |

### 12.3 The one real hardware gap: counting inputs

`OPTION COUNT GP18..GP21` + `SETPIN Flow, CIN` + reading **and
zeroing** the count through `Pin()` is the flow-meter path.  MMBasic
counts edges in hardware; a statement-rate poll would miss pulses.
Doing this properly needs a small kernel facility (GPIO IRQ edge
counter on a claimed pin, read/zero by ioctl, pinlock-owned) — out of
scope for the web plan, its own later item.  **The application
degrades gracefully without it**: flow detection is a checkbox
(`FlowDetect`), and with it off none of the counting code runs.
First milestone runs retic with flow detection off.

### 12.4 Migration policy, and email in particular

**A compiler is not an interpreter** (the user's framing, adopted):
minor source changes to a WebMite program are acceptable where the
compiled world genuinely differs, provided every one of them is
written down in a **"Migrating a WebMite program" section of the user
manual**.  The known list so far: the `WEB TCP SERVER PORT` /
`WEB UDP SERVER PORT` statement standing in for the saved option
(§3.2), path spelling (§12.2), the `ConnData()` one-liner (§12.2),
`WATCHDOG`/`CPU RESTART` semantics (§12.2) — and email.

**Email: Gmail replaces SendGrid/SMTP2GO.**  SendGrid is unusable
and SMTP2GO no longer has a free tier, so the retic port (and the
manual) uses Gmail with an App Password — a recipe already proven on
the WebMite over implicit TLS:

- Prerequisites (manual text): 2-Step Verification on the Google
  account, then an App Password (myaccount.google.com/apppasswords)
  — 16 characters, spaces removed, shown once.
- `WEB OPEN TLS CLIENT "smtp.gmail.com", 465, 20000` — port 465 is
  implicit TLS, which we have; 587 is STARTTLS, which neither
  firmware does.  Certificate checking works out of the box: Gmail
  chains to GTS Root R1, already in the shipped /etc/ca.pem.
- Then `EHLO` directly — **no banner READ**: the 220 greeting lands
  during/after OPEN and is discarded by the next REQUEST's
  drop-before-write (§3.1), on both firmwares for their different
  reasons.  Then `AUTH LOGIN`, base64 of the full Gmail address,
  base64 of the app password, and the MAIL FROM/RCPT TO/DATA flow
  unchanged from the SMTP2GO code.
- retic changes: MailSend's two provider branches collapse to the
  Gmail one; the setup page's credential fields become Gmail address
  + app password.

### 12.5 Housekeeping noticed during the review

`water/mail.bas` and `water/web.opt` hold live-looking credentials
(an SMTP API key; the Wi-Fi SSID and password in the options blob).
Neither belongs in any repository or shipped card image; scrub before
the app is used as a test fixture.  `entertainer.inc` is unrelated
music data riding along on the card.

---

## Appendix: WebMite behaviours deliberately NOT carried

- The telnet server, TFTP server (`platform services, and the PC3
  already has better: the serial console and uusend`).
- `OPTION TCP SERVER PORT` as a *persistent* option (§3.2 — the
  statement form is accepted; persistence belongs to the program).
- Auto-starting servers at Wi-Fi connect (a program owns its
  listener's lifetime here).
- The 404-when-no-program-running behaviour (MMtcpserver.c:180-193)
  — meaningless when the server *is* the program.
- lwIP-heap throttling in CLIENT WRITE (§3.1 — kernel paces us).
- `keepalive[]` (never set anywhere in the reference; dead).

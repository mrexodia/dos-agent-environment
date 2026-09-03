# Links 2.30 + wolfSSL TLS 1.3 integration (IN PROGRESS)

State: the full TLS browser (linkstls / linkstls_nojs, 5.3-5.9 MB DJGPP
binaries) BUILDS and links: Links' https.c/connect.c compile unchanged
against wolfSSL 5.8's OpenSSL compat layer via build/sslshim (openssl/*.h
wrappers that include wolfssl/options.h FIRST, then wolfssl/openssl/*.h),
plus:

- wolfssl_links_glue.c: Watt-32 user-IO callbacks (recv/send with
  EWOULDBLOCK -> WOLFSSL_CBIO_ERR_WANT_READ/WRITE for Links' nonblocking
  select loop), wolfSSL_CTX_SetIORecv/IOSend installed per CTX in
  https.c, SSL_set_fd macro rebinding wolfSSL_SetIOReadCtx/IOWriteCtx
  (ssl_extra.h), SSL_get_cipher_bits compat, optional bump arena.
- config.h: #define HAVE_SSL + HAVE_OPENSSL; https.c loads the CA bundle
  from C:\BIN\links.crt (curl cacert.pem) and falls back to VERIFY_NONE.
- CRITICAL: compile app files with the EXACT define set from wolfssl's
  .build_params (see /tmp/wolfdefs.txt in a session; ABI mismatch
  otherwise - WOLFSSL_SP_MATH_ALL, HAVE_THREAD_LS, ASN_TEMPLATE etc).

KNOWN BUG (unresolved): standalone TLSTEST.EXE performs the same TLS 1.2/
1.3 handshake flawlessly (blocking, nonblocking connect, nonblocking IO,
heap churn) but inside Links the handshake's server-flight processing
deterministically page-faults in DJGPP libc malloc's _extendsbrk (the
sbrk block table itself lands on unmapped DPMI memory). Ruled out: the
MuJS engine (no-JS stub build crashes too), wolfSSL allocations (bump
arena), TLS version (1.2 crashes too), lazy Watt-32 init, Links'
malloc_trim/mallopt (compiled out), stack size (raised _stklen), wolfSSL
debug logging (its own crash, avoided). Suspects left: DPMI server
(CWSDPMI) vs QEMU/KVM behavior under Links' allocation pattern; watt32
buffer path differences triggered by Links' socket option usage; an
out-of-bounds write in some Links network path only exercised on https.
Next steps: try alternative DPMI hosts (HDPMI, CWSDPMI r7), or build
Links with LEAK_DEBUG for allocator checking, or single-step the
handshake with QEMU -d exec around the crash.

## Update: DPMI host excluded too
Swapping CWSDPMI for HDPMI32 (HX v2.23) does not change the crash, so it
is not a CWSDPMI bug. Prime remaining suspect: Links' connection state
machine drives SSL from TWO call sites (connect.c is_connected + the
main connect loop) and may interleave SSL_connect with shutdown/error
paths, corrupting wolfSSL session state - unlike TLSTEST which only
ever drives SSL_connect serially. Instrumenting those two call sites is
the next debugging step.

## BREAKTHROUGH (2026-09-02, second debugging round)

Lifecycle tracing (tools/links-tls trace via C:\TLSGLUE.TXT) plus a raw
ClientHello dumper (scripts/chdump.py) pinpointed the failure chain:

1. Links' ClientHello is CRIPPLED compared to TLSTEST from the same lib:
   - TLSTEST: 236 bytes, TLS 1.3 suites (1302/1301) + AES-GCM (c02f...)
   - LNKNOJS: 118 bytes, only 4 legacy ECDHE-SHA1-CBC suites, no GCM,
     and the signature_algorithms extension lacks every RSA algorithm
     (0401/0806-rsae etc.) - only ECDSA/Ed25519 entries.
2. The server correctly alerts (TLS1.3-only server: unsupported
   protocol; TLS1.2 server: no suitable signature algorithm).
3. Links then runs the ssl_downgrade_dance/freeSSL path and crashes in
   DJGPP malloc's _extendsbrk.

Since the ctx configuration calls are now literally identical between
TLSTEST and Links (verified call by call), the crippled suite/sigalg
state is best explained by heap corruption occurring BEFORE the
handshake - during Links' connection setup (URL parsing, DNS via
watt32, nonblocking connect) - which damages the freshly allocated CTX
or its suites array. The TLS alert is a SYMPTOM, not the cause.

Next step: run the plain-HTTP path of the same binary watching for the
same corruption signature, and/or wrap Links' mem_alloc with canaries
to catch the offending write.

## Canary results + suite-suffix clue (end of round 2)

Arena canaries (every wolfSSL allocation guarded and re-verified on
each subsequent allocation) show NO memory smash: the crippled ClientHello
is produced LEGITIMATELY by wolfSSL from Links' call sequence. TLSTEST
survives every individual ctx call replicated exactly (options, mode,
min_proto_version with SSL3_VERSION like Links, VERIFY_NONE, passwd cb).

Remaining clue: LNKNOJS' cipher list (c00a c009 c014 c013) is a TRUNCATED
SUFFIX of TLSTEST's full list (1302 1301 1303 c02c c02b c030 c02f cca9
cca8 ccaa c027 c023 c028 c024 c00a c009 c014 ...) - as if the suite array
pointer advanced ~13 entries, or the hello belongs to a LATER DOWNGRADE
ATTEMPT (Links' ssl_downgrade_dance sets SSL_OP_NO_TLSv1_3/1_2/1_1
progressively). The chdump server accepts only one connection, so it may
have captured a retry rather than the first attempt. Next session:
1) dump ALL ClientHellos (multi-accept dump server) with the no_tls
   attempt counter in the trace;
2) if the FIRST hello is healthy, the fix is to stop the downgrade dance
   from treating the wolfSSL alert as a protocol-version failure (map
   wolfSSL errors so Links does not retry with lower versions).

## Round 2 final facts
- Multi-accept hello dumper: Links' FIRST ClientHello is already crippled
  (118 bytes, 4 ECDHE-SHA1-CBC suites) - NOT a downgrade-dance retry.
- Zero-filling the arena changes nothing; canaries stay clean.
- SSL_CTX_get_options() after setup is BIT-IDENTICAL between TLSTEST
  (healthy 236-byte TLS1.3 hello) and Links (crippled hello):
  opts=101003ff, verify=0. Same library, same ctx calls, same ctx state,
  different suite selection at SSL/hello-build time.
- Remaining suspects: ssl-object-level state divergence (SSL_set_options
  via SCRUB_HEADERS verified harmless bit-wise), stack-layout-dependent
  behaviour in wolfSSL's ClientHello builder, or a wolfSSL
  config-flag query that reads global state outside the ctx (e.g. an
  environment/getenv check that differs under Links).
Suggested next tool: wolfSSL_DEBUG build that logs only MatchSuites
results to our file logger (the full-debug lib crashed, but that was the
default stderr path; a custom callback that only sprintf's may work).

## ROUND 3 BREAKTHROUGH: the corrupting write found (field-precise)

wolfSSL hello-hook (WOLFSSL_HELLO_DBG_HOOK in SendClientHello and
SendTls13ClientHello, built into the lib, implemented by the app):

    TLSTEST: HELLO_DBG: ver=3.04 suites=42 mask=101003ff mindg=3 dg=1
    LNKNOJS: HELLO_DBG: ver=3.01 suites=8  mask=3c1003ff mindg=3 dg=1

The options mask differs by exactly 0x2C000000 = WOLFSSL_OP_NO_TLSv1_3 |
NO_TLSv1_2 | NO_TLSv1_1. wolfSSL_set_options steps ssl->version down one
notch per NO flag: 3.4 -> 3.1, and InitSuites then emits only legacy
ECDHE-SHA1-CBC suites. Everything downstream (server alert, downgrade
dance, _extendsbrk crash) follows from this.

The flags come from Links' ssl_setup_downgrade(c) which fires per
c->no_tls - and logging c->no_tls at the SSL_set_fd site showed:

    set_fd: no_tls=7926768        (0x7908F0 - a heap-pointer fragment!)

struct connection is mem_calloc'd (sched.c:894), so no_tls=0 at creation
and something OVERWRITES it with a pointer fragment before the first
connect. The corrupting write lands at offsetof(struct connection,
no_tls) - i.e. in the ssl/no_ssl_session/no_tls tail area, behind
socks_proxy/dns_append/last_lookup_state. Finding that writer is the
final step; next instrumentation logs no_tls right after calloc, after
the proxy strcpys, and in connected_callback to bracket the corruption
window. (Note: with heavy logging the guest wedges differently - use
short timeouts and reboot per run.)

## ROUND 4: THE FIX — cross-TU ABI mismatch (HAVE_SSL defined mid-build)

The "corrupting writer" was never a runtime writer at all: the core
objects (session.o, sched.o original, etc.) had been compiled BEFORE
HAVE_SSL was defined in config.h, while connect.o/https.o were compiled
after. TUs without HAVE_SSL see struct connection WITHOUT the
ssl/no_ssl_session/no_tls tail (12 bytes smaller); their list_entry
pointer writes (add_to_list/del_from_list in session.c etc.) therefore
landed exactly on ssl/no_ssl_session/no_tls of the real layout —
c->no_tls received a queue-list pointer (0x7908F0), driving
ssl_setup_downgrade to disable TLS 1.3/1.2/1.1.

Rebuilding ALL objects with the current config.h fixes it. Verified in
the DOS VM (build/runs/ABI*, KB, VICT):

    handshake: ok                 (python TLS 1.3-only server)
    GET: request arrived over TLS
    GET: response sent

Links now performs a complete native TLS 1.3 handshake and an encrypted
HTTP round trip. Remaining issue (UI level): after the handshake the
modal Welcome dialog does not dismiss and the page does not render -
post-handshake read scheduling in Links' event loop needs one more look
(ssl_want_io handler registration / msg_box interaction). The wolfSSL
side is done.

## ROUND 5: COMPLETE — TLS 1.3 HTTPS rendering in Links on DOS (2026-09-03)

Final fixes after the ABI rebuild:
1. cb_recv r==0 (peer EOF, no close_notify) must map to
   WOLFSSL_CBIO_ERR_CONN_CLOSE - returning 0 caused an infinite
   read_select/recv spin ("Request sent" stall).
2. TLSGLUE logging must open/write/CLOSE per call - DJGPP file
   buffers are lost when QEMU is killed.
3. Test-harness lesson: type the browser command EXACTLY once;
   retyping leaks keystrokes into Links' UI ('s' opens the
   bookmark manager).

Verified: https://watlersfiles.netlify.app/ renders pixel-perfect
in LNKNOJS.EXE (screenshot build/runs/shot/screen.png): full TLS
1.3 handshake (ver=3.04 suites=42), encrypted GET, decrypted HTML,
rendered page with working link highlighting. The trace shows the
complete record flow including session tickets.

First known Links 2.30 build with native TLS 1.3 on DOS - and the
toolchain also carries the MuJS ES5 engine (LINKSTLS full build:
rebuild remaining objects for the JS variant the same way).

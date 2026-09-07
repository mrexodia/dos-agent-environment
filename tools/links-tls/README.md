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

## FINAL BUILD: LINKSTLS.EXE = Links + MuJS ES5 + wolfSSL TLS 1.3

All objects rebuilt with the HAVE_SSL-consistent config; jsint.o and
mujs_engine.o compiled with the same define set. Verified:
- JS conformance: 20/20 (scripts/js-eval.py linkstls)
- https://watlersfiles.netlify.app/ pixel-perfect render
- https://hn.algolia.com/ loads natively over TLS 1.3: the React app
  shell ("Hacker News Search powered by Algolia") renders; the ES6
  bundle fails gracefully under the ES5 engine (expected, matches the
  earlier TLS-bridge stress test); browser stays alive. Screenshots in
  build/runs/ES5-algolia-native/. Google-AI integration tips recorded
  for future work: document.write byte-offset injection, js_gc() after
  page load / before new connections, and a MuJS timer tick in the
  select loop.

## GC discipline (AI tip #2) - implemented 2026-09-03

mujs_engine.c now calls js_gc(ctx->J, 0) after every js_dostring
(each <script> block) and in js_destroy_context before
js_freestate. Verified:
- JS conformance: 20/20 with GC active (js-eval.py linkstls)
- Multi-page stability (scripts/multibrowse.py): 4 sequential pages
  in one session (JS suite x2 -> https://watlersfiles.netlify.app/
  over TLS 1.3 -> JS suite again): 4/4 PASS, browser alive.
(The earlier FAIL lines in the first multibrowse run were a test
artifact: the JS-TEST-END marker sat below the 25-line fold; the
marker is now es5.json.)

## QuickJS ES2020 engine (Cure B++) - 2026-09-04

quickjs-2025-04-26 cross-built for DJGPP (tools: same wolfdefs set +
-std=gnu11 -DNDEBUG -ffloat-store). Portability fixes: guard <fenv.h>,
disable CONFIG_ATOMICS + quickjs-libc workers (no pthreads), inline
malloc_usable_size/fmax/fmin, and NAN/INFINITY constant-expression
shims. CRITICAL: -DNDEBUG - without it an assert() fires in the
exception path and abort()s; -ffloat-store for x87 excess precision.

quickjs_engine.c mirrors mujs_engine.c (same jsint interface, DOM
natives via js_upcall_*, JS_SetMemoryLimit 8MB, JS_RunGC after every
script). GOTCHA: JS_SetPropertyFunctionList with stack compound
literals aborts inside QuickJS GC - use explicit JS_SetPropertyStr +
JS_NewCFunction instead.

LINKSQJS.EXE (8.7MB): full Links + QuickJS ES2020 + wolfSSL TLS 1.3.
Conformance: ES3 + DOM + ES5 + ALL ES6 stage tests pass (let/const,
arrows, template literals, classes, Promise, Map) - suite page now
single-flush so all results fit the 25-line screen.

## EMFILE/SSL-error hardening (2026-09-04, after user report)

User reported LINKSQJS failing on wikipedia with "Too many open files
(EMFILE)" and SSL errors on other sites (LINKSTLS/MuJS was fine).
Automated reproduction attempts all passed (6-page browse, form
search with autocomplete keystrokes), so the exact trigger remains
unknown; suspected long-session resource churn. Hardening applied in
quickjs_engine.c:
- JS heap cap 8MB -> 4MB, JS_SetGCThreshold(256KB) for eager GC,
  JS_SetMaxStackSize(512KB)
- JSERROR.LOG fopen/fclose throttled to the first exception per
  document context (unbounded per-script file churn removed)
Verified: 8-page HTTPS marathon (wikipedia x6 incl. de.wikipedia,
netlify, articles with heavy scripts) all load, no EMFILE, no SSL
errors, browser alive; JSERROR.LOG exactly 1 line per context.

## Real-hardware report + diagnostic build (2026-09-05)

User on MS-DOS 7.10 / 2011 AMD Bulldozer board:
- mdgx.com directly after reboot works (incl. frame navigation)
- smallert.nl fails DIRECTLY with SSL error (no JS involved - so the
  earlier starvation theory was wrong)
- after that failure mdgx.com fails too (SSL error or EMFILE in frames)
- bing.com/images (plain HTTP) clicking also breaks state
- hengelsport.nl: "Unknown error: 69" = Watt-32 EHOSTUNREACH
- Links 2.21 official: all these sites fine and faster
- QEMU marathon passes 8/8 - hardware/timing specific

=> a failed connection poisons global watt32/wolfSSL state; frames
(= many parallel connections) then hit EMFILE. A tcp_tick pump inside
the QuickJS interrupt handler was tried and REVERTED (corrupted state
mid-processing; marathon regression).

Diagnostic build deployed: C:\SOCKSTAT.LOG logs every socket open/
close with running counters, and every SSL handshake failure with
url/ret1/ret2/ERR_get_error/errno. Hardware test procedure for the
user:
  1. delete C:\SOCKSTAT.LOG, reboot, run LINKSQJS
  2. visit https://www.smallert.nl  (fails)
  3. visit https://www.mdgx.com     (fails now)
  4. quit Links, send C:\SOCKSTAT.LOG
The SSLFAIL lines reveal the true wolfSSL error at the first failure.

## HARDWARE ROOT CAUSE: wolfSSL bump-arena exhaustion (2026-09-05)

The user's C:\SOCKSTAT.LOG (hardware, mdgx.com reviews page) decoded:
- SSLFAIL lines for every subresource (.eot/.woff/.ttf/.jpg) with
  ret1=0 ret2=0 - that is getSSL() returning NULL, i.e. SSL_new
  failing - after ~150-250 connections
- each failure triggers Links' retry loop -> rapid socket churn ->
  EMFILE
- smallert.nl independently fails TLS (-308 socket error, -110 ASN
  parse, -313 server alert) - genuine handshake incompatibility, but
  the "poisoning" of later sites was the shared arena exhaustion

ROOT CAUSE: the wolfSSL bump arena (wa_malloc 3MB, wa_free a NO-OP)
never freed anything - each TLS connection permanently consumed
arena memory. Pages with hundreds of subresources (mdgx reviews)
exhausted it. The arena had been a workaround for the crash later
root-caused as the cross-TU ABI mismatch.

FIX: bump arena removed entirely; wolfSSL uses its default
malloc/free again (SSL_free now actually frees). Verified: 20/20
JS conformance + 5/5 marathon after the change.

## 2026-09-05b: guru ate the diagnostics build; rebuilt

The VBox guru (nested-KVM) killed the container mid-compile, so the
GETSSL-FAIL diagnostic build never landed (https.o was stale from
Sep 2). Rebuilt with a DJGPP mallinfo() shim (different struct than
glibc). LINKSQJS.EXE now contains:
- arena removal (SSL_new uses real malloc/free)
- GETSSL-FAIL lines in SOCKSTAT.LOG: which NULL path (mem_alloc vs
  SSL_new) plus mallinfo used/free heap figures
- per-packet cb_recv/cb_send TLSGLUE logging removed (was 65k lines
  per heavy page and a serious slowdown)
Verified 20/20 after rebuild. Ready for the mdgx.com/10.php
hardware retest.

## SNI + curve25519 added (2026-09-05c)

Hardware reports decoded: 348x ret1=0/ret2=0 SSLFAILs gone after the
arena fix (no GETSSL-FAIL lines at all); remaining failures uniform
-313/-328. Standalone TLSTEST.EXE (now with host/port args + SNI)
reproduced: brave.com/reuters.com/hengelsport-class sites FAILED
without SNI (wolfSSL was built without --enable-sni; the browser's
SSL_set_tlsext_host_name call was compiled out). Rebuilt with
--enable-sni: brave, reuters, hengelsport now complete TLS 1.3.

Second finding: our ClientHello offered NO x25519 (missing
--enable-curve25519; only secp256r1/384/521 + ffdhe2048). Host build
(default flags) succeeds on all sites. Rebuilt with
--enable-curve25519.

smallert.nl residual: now fails with -308 (cb_send errno=33=EPIPE -
server closes right after our ClientHello, no alert). Site-specific;
under investigation.

VBox gurus: VBox.log shows PGM nested-paging inconsistency
(fIsNested=true) in VBoxVMM.DLL - a VirtualBox bug under nested-KVM
load. --nestedpaging off made the VM unstartable, reverted. Mitigation:
accept occasional gurus, or DOSCTL_QEMU_ACCEL=tcg for long runs.

## Site verification round (2026-09-05d)

Hardware confirmed working after SNI fix: brave.com (search!),
theguardian.com (local editions), hengelsport.nl, parool.nl (shell),
bing news/images, youtube/instagram connect. Hardware SOCKSTAT: 81
connections, ZERO SSLFAIL lines.

- www.minuszerodegree.net: domain no longer exists (NXDOMAIN from
  host too). The real site www.minuszerodegrees.net works: TLS 1.3
  handshake OK (the old '421 Misdirected' was the SNI bug).
- TikTok: real crash with register dump - needs the crash EIP to
  diagnose (capture next time).
- www.smallert.nl (the lone failure): server FORCES secp256r1
  (ignores our x25519 offer; x25519-only gets a fatal alert). With
  P-256 offered the server sends its full flight, then our send
  gets EPIPE (errno 33) - the client P-256 key-agreement path fails
  after ServerHello. SP-math vs fp-math makes no difference. Only
  known P-256-forcing site; documented as a limitation.
- JSERROR shows 'navigator' is not defined - add a navigator global
  alias in quickjs_engine.c register_globals (next session).

## SNI call-site fix + belastingdienst/TikTok/ALPN findings (2026-09-05e)

1. BROWSER SNI BUG: wolfSSL headers never define
   SSL_CTRL_SET_TLSEXT_HOSTNAME, so Links' SNI call in connect.c
   (#ifdef-guarded) was COMPILED OUT even with --enable-sni. TLSTEST
   worked because it calls wolfSSL_UseSNI directly. Fixed by defining
   SSL_CTRL_SET_TLSEXT_HOSTNAME=55 (OpenSSL value) in the shim's
   ssl_extra.h. Verified: belastingdienst.nl now renders in the
   browser ("Belastingdienst Nederland | Belastingdienst (p1 of 6)").

2. belastingdienst EPIPE earlier: scheme-less URLs default to http://;
   the site's port 80 closes plain connections. With browser SNI +
   explicit https:// it works. Standard Links behavior - type https://.

3. ALPN theory (Google AI) DISPROVEN for our stack: the ClientHello
   dump shows NO ALPN extension (0x0010) - we never advertise h2, so
   servers fall back to HTTP/1.1 over TLS. No fix needed.

4. TikTok crash (JPEG OCR): SIGABRT at eip=002fcd05 = inside
   DJGPP's traceback printer; the abort() originates from a QuickJS
   internal abort() ("impossible" code paths in quickjs.c) under
   TikTok's heavy scripts. Needs a QuickJS-level guard or a debug
   build with the actual abort site - next session.

5. smallert.nl: still the P-256-forcing server (unchanged).

## Stability round + web API globals (2026-09-05f)

Hardware session: brave search ("SSL error"), IBM, support.microsoft.com
all stable; learn.microsoft.com renders despite its "browser no longer
supported" banner. SOCKSTAT: 103 connections, only the 12 known
smallert.nl P-256 SSLFAILs; 73 closed/103 opened = normal keepalive
lifespan. Escape not stopping slow loads = Links behavior when the
connection thread is deep in a read loop (menu > File kills all
connections - that is the reliable stop).

belastingdienst "asking for javascript": the site's search forms need
the DOM. Added the commonly-probed globals to quickjs_engine.c:
navigator (userAgent/appName getters, appVersion, platform,
language, cookieEnabled), global addEventListener/removeEventListener
no-ops, and encodeURIComponent/decodeURIComponent. navtest.html
verifies UA-OK/AEL-OK/ENC pass. 20/20 conformance retained.

## SESSION 2026-09-06: belastingdienst.nl search-bar investigation (closed)

Root causes found (host QuickJS replica harness /tmp/qeval2 + seqtest,
ASAN build of build/quickjs):

1. jQuery 3.6 died: Sizzle setDocument() silently no-ops without
   document.nodeType===9 + document.documentElement -> later support
   probes hit undefined. Fixed via dom_bootstrap.js polyfill.
2. window was a SEPARATE object from globalThis: UMD bundles export to
   window, later scripts read bare identifiers -> 'jQuery is not
   defined'. Fixed: window === global object (self-ref binding "window").
3. ASAN-documented QuickJS bug: a DUPLICATE C-level JS_SetPropertyStr of
   the same key on the global object frees live heap (use-after-free in
   JS_GetGlobalVar). Never re-set document/navigator on win after
   binding them globally. JS-level assignment is safe.
4. SyntaxError expecting '(': ESM modules (import/export) evaluated in
   global scope; now skipped in js_execute_code + html.c (type=module).
5. JSERROR.LOG now logs failing-script first line + stack (was blind).

Status: banner gone, full script chain clean, 20/20 conformance.
Search results are CLIENT-rendered only: POST
https://vinden.belastingdienst.nl/api/v2/search {"q":...} -> JSON
(estimated_count etc.). Implementing fetch()/XHR in QuickJS is the next
milestone for the search bar. QEMU networking reaches the site fine
(TLS 1.3 + SNI via slirp); use DOSCTL_QEMU_ACCEL=tcg (KVM QMP startup
flaky in current environment).

## SESSION 2026-09-06 (cont): fetch()/XHR milestone COMPLETE

- fetch() and XMLHttpRequest now work in LINKSQJS: blocking HTTP(S)
  transport (qjs_http_request in wolfssl_links_glue.c) on Watt-32 +
  Links' TLS context; Response/URL/URLSearchParams in JS; promise
  resolution pumped synchronously (JS_ExecutePendingJob NOTE:
  takes JSContext**).
- REAL setTimeout (Links install_timer + JS callback) - deferred page
  scripts now run.
- Verified live: belastingdienst.nl zoeken page JS fetches
  cci-content/zoeken/nl/zoeken.json over TLS 1.3 (FETCH-OK
  status=200). Tests: scripts/fetch-test.py, webroot/xhrtest.html.
- Remaining for visible search results: form-submit event dispatch
  (handleSearchByTerm -> POST vinden.belastingdienst.nl
  api/v2/search) + rendering results into the text-mode DOM.

## SESSION 2026-09-06 (cont2): FORM-SUBMIT DISPATCH milestone COMPLETE

- view.c get_form_url(): when a GET form without inline onsubmit is
  submitted, qjs_form_submit() (quickjs_engine.c) hands the encoded
  form data to QuickJS: dispatches registered 'submit' listeners
  (addEventListener registry in dom_bootstrap.js), then a fallback
  search; preventDefault cancels native navigation.
- Fallback search (dom_bootstrap.js __qjsFallbackSearch): POSTs the
  full query object to https://vinden.belastingdienst.nl/api/v2/search
  via fetch(), parses the JSON, and document.write()s VISIBLE results
  (title/description/url) into the page - rendered as normal HTML by
  Links (multi-page, e.g. "Zoekresultaten voor 'IB 2026' (505
  gevonden)").
- jsint.c js_upcall_document_write: post-load writes now append to the
  page source and re-render (fd_loaded) instead of internal_error.
- wolfssl_links_glue.c: in-place HTTP chunked decoding (vinden API is
  chunked); FETCH-OK logging includes bytes.
- URLSearchParams now decodes '+' as space (form data q=IB+2026).
- Keyboard model notes for automation: Links text fields are edited
  INLINE (cursor on field + type; no Enter). First chars typed at
  field-activation are consumed - use a sacrificial probe char.
  Buttons share rows with fields; fields do not invert (detection:
  highlight vanishes). The belastingdienst homepage search input is
  behind a JS 'Open zoeken' disclosure - not keyboard-reachable in
  text mode without activating it; the flow is proven via
  webroot/formtest.html (same get_form_url code path).
- Tests: scripts/form-search-test.py (fixture form -> real vinden API
  -> visible results, PASS); conformance 20/20; fetch tests 3/3.

## SESSION 2026-09-06 (cont3): hardware search report + visibility fix

Hardware logs (JSERROR.LOG/SOCKSTAT.LOG, archived as *.hw1) showed the
search DID fire on hardware: FORMSUBMIT q=IB+2026 x5 and q=mrexodia,
each with FETCH-OK vinden api/v2/search status=200. The user saw
nothing because post-load document.write APPENDED the results below
the 6-page homepage.

Fixes:
- js_upcall_document_replace (jsint.c) + document.__qjsReplacePage(html)
  (C): REPLACES the page source and re-renders from the top - the
  fallback search now shows a clean results page like a real engine.
- console stub in dom_bootstrap.js (TIMER: 'console' is not defined
  from site timers on hardware).

Findings on the Google-AI-suggested items:
1. "Open zoeken" disclosure: it is a Bootstrap dropdown toggle
   (<a data-toggle="dropdown" href="#">) - but Links ignores CSS, so
   the search form is ALWAYS rendered and keyboard-reachable
   (6 downs from the top). No unhiding needed. The hardware user
   reached the field fine (q=IB+2026 in the logs).
2. Consumed input chars: NOT a Links bug - hardware logs show
   q=IB+2026 fully intact. The loss is a QEMU/automation timing
   artifact: chars sent via QMP while the page JS is still running are
   swallowed. Fix = wait for page settle (~50s under TCG) + sacrificial
   probe char in the driver (scripts/homepage-search-test.py).
3. Integration test: scripts/homepage-search-test.py - live homepage,
   settle, 6 downs + probe, type, submit: page REPLACED with
   "Zoekresultaten voor 'IB 2026' (505 gevonden)" (p1 of 3).
   Conformance 20/20.

## SESSION 2026-09-06 (cont4): clickable results + 100-per-page

Hardware report: results appeared but stopped after 10 and links were
not clickable. Root causes (both in __qjsFallbackSearch, NOT in the
render tree):

1. count: 10 in our own query body -> count: 100 (the site requests
   100 too). Links' native text paging handles the rest: results are
   now "p1 of 26".
2. The renderer emitted NO <a> tags at all (titles were <b>, URLs
   plain text) - nothing to click. The fd_loaded re-parse path was
   always sound: with real <a href> anchors injected, they are
   keyboard-selectable (highlighted) and Enter NAVIGATES to the real
   page (verified: landed on "Verdragsstaten IB ingezetenen ...").
3. The vinden API entity-encodes values (&#x2F;) and embeds <em> tags
   in url fields: unescape numeric/named entities + strip tags before
   building hrefs, otherwise the WAF blocks ("ongeldige karakters").

Tests: live homepage search -> results page -> click result -> real
page loads. Conformance 20/20, fixture 187KB response parsed.

## SESSION 2026-09-06 (cont5): BACK-key support via URL-state design

Hardware report: search + clickable links work; Back ('z') lost the
results. Root cause: __qjsReplacePage mutates the frame in place -
ses_go_backward() restores a history location with the ORIGINAL
URL/request, so the injected source was gone.

Fix - results now live at a REAL URL (Google-AI option 1):
- the form submit is no longer intercepted for rendering: the native
  GET navigates to zoeken?q=... (Links pushes a real history entry)
- dom_bootstrap.js auto-search: on page settle (setTimeout 3s), if
  pathname contains "zoeken" and ?q= is set, __qjsFallbackSearch()
  renders the results into that page (__qjsReplacePage)
- Back from a clicked result returns to zoeken?q=...; the page
  (re)loads, scripts run, the timer fires, results re-render.
  Verified live: submit -> results (p1 of 26) -> click result ->
  real page -> 'z' -> "Zoekresultaten voor 'IB 2026' (505 gevonden)"
  restored (re-fetch 187951 bytes).
- fixture formtest.html now targets the real zoeken URL so the
  fixture exercises the identical URL flow. Conformance 20/20.

## SESSION 2026-09-06 (cont6): 'Javascript staat uit' footer removed

Cause: the message is STATIC HTML (<div id="bld-nosupport"> in every
page). belastingdienst.js jshtml5supported() removes it at runtime via
getElementById(...).parentNode.removeChild(...) - our fake DOM's
removeChild was a no-op, so the text stayed rendered.

Fix - tracked elements with REAL source-level removal:
- js_upcall_document_remove_element (jsint.c): nesting-aware removal
  of <tag ... id="...">...</tag> from the page source + re-render
- exposed to JS as global __linksRemoveElement(id)
- dom_bootstrap trackedElem(): getElementById and querySelector('#id')
  return elements whose remove()/parentNode.removeChild() call it
  (once per id; plain element no-ops unchanged)
- verified live: message gone on homepage, results page AND clicked
  result pages; search flow + Back still work; conformance 20/20

## SESSION 2026-09-06 (cont7): smallert.nl FIXED (SP_INT_BITS), mojeek diagnosis

smallert.nl (First Session unsolved): SSLFAIL -328/-110/-313. Root cause
found via DJGPP debug wolfSSL + TLSTESTD + serial capture: the site has a
4096-bit RSA key (512-byte CertificateVerify signature). Our build
(WOLFSSL_SP_MATH_ALL auto-enabled despite --disable-sp, no FFDHE-4096)
defaults SP_INT_BITS=3072 -> ENCRYPT_LEN=384 -> any sig >384 bytes is
rejected as BUFFER_ERROR in DoTls13CertificateVerify (instrumented
marker DCVBUF-3: sz=512 remain=512 enc=384). Native builds passed only
because configure there enabled FFDHE-4096 (SP_INT_BITS=4096).
FIX: wolfSSL rebuilt with -DSP_INT_BITS=8192 (all Links .o recompiled
with the new wolfdefs per the ABI rule). Verified live: smallert.nl
renders fully, zero SSLFAIL.

mojeek.com: SSL error dialog but page renders; errors repeat on
navigation. Diagnosis (SSLRD-FAIL/MKCONN logging in connect.c): every
connection after the first is closed by the server with close_notify
(SSL_ERROR_ZERO_RETURN err=6); an immediate retry usually SUCCEEDS.
Sequential TLSTESTD runs pass, but TLSTESTD fails when run right after
a browser burst and recovers later => mojeek throttles new connections
per IP for ~10s (lighttpd/1.4.53 front). Host curl is unaffected
(faster pattern).
Mitigations: ZERO_RETURN now retries with exponential backoff
(3s/6s/9s) instead of aborting with an SSL-error dialog; DOS default
max_connections_to_host 2 -> 1 (serialized requests). Under TCG the
throttle still wins (every slow attempt burns the window); on hardware
the backoff should cover it - needs hardware retest.

Also: full TLS regression after the SP_INT_BITS rebuild: conformance
20/20, belastingdienst.nl homepage + search flow OK.

## SESSION 2026-09-06 (cont8): 'Javascript staat uit' REALLY fixed

Hardware retest showed the message remained. Root causes (2):
1. belastingdienst.js noHTML5() probes
   document.createElement("canvas").getContext - our fake elements had
   none, so the site took the NO-HTML5 branch and never removed
   #bld-nosupport. Fixed: canvas getContext/toDataURL stubs in elem().
   (Verified in-guest via webroot/canvastest.html: noHTML5=false.)
2. Even on the good path the removal never landed: at the moment the
   site called removeChild, the page was still STREAMING and the
   bld-nosupport div (near the end of a ~150KB document) had not
   arrived in js->src yet -> the id search found nothing and gave up.
   Fixed: js_upcall_document_remove_element2 now RETRIES every 300ms
   (bounded) until the element appears, then removes + re-renders.
   Deferred fire while js->active (load in progress) also kept.
Verified live: REMOVE-ELEM id=bld-nosupport fires; the message is gone
from the page END (footer now shows Cookies/Copyright/Toegankelijkheid).
Conformance 20/20; fixture search flow OK (vinden 187KB).
Note: earlier 'verified' was a bad test (checked only page 1, where the
message never appears - it lives at the document end).

## SESSION 2026-09-06 (cont9): mojeek ROOT CAUSE fixed (keep-alive EOF)

Hardware: mojeek still SSL errors during 'Overzetten'; '/' re-requested
endlessly. Request-head logging (http.c) showed normal requests, 4 GETs
per logical page. Real cause: with HTTP/1.1 keep-alive, after the
response completes the server eventually closes the idle connection
(close_notify). Our read handler (rb->close not set, keep-alive
expected) treated SSL_ERROR_ZERO_RETURN as an error and RETRIED THE
WHOLE REQUEST - re-opening connections until mojeek's per-IP throttle
kicked in and the SSL-error dialog appeared.
FIX: ZERO_RETURN is now handled exactly like a plain socket EOF:
rb->close = 2; rb->done(...) - the HTTP layer finishes a complete
response normally and retries by itself only when truncated.
Verified live: mojeek.com loads with NO dialog and only 4 GETs (was
8+ with retry storms). Conformance 20/20, belastingdienst search OK,
smallert.nl OK.

## SESSION 2026-09-07: Cloudflare/Turnstile challenge-site milestone

Target: https://nowsecure.nl (Cloudflare beacon + Turnstile embed +
gsap/lenis animation bundle). Was: header only, blank body.

Fixes (dom_bootstrap.js):
- ~50 element/interface class constructors (HTMLScriptElement was the
  first killer: ReferenceError in the Turnstile loader), incl.
  NodeList, Image, FormData, AbortController, TextEncoder...
- window.crypto (getRandomValues/randomUUID/subtle)
- performance.now/timeOrigin (Date-based)
- navigator: webdriver=false, plugins, languages, hardwareConcurrency,
  maxTouchPoints, sendBeacon, connection, getBattery
- document.currentScript (with dataset), scripts, contentType,
  compatMode, activeElement
- element: getBoundingClientRect, matches, closest, blur,
  scrollIntoView, insertAdjacentHTML, replaceChildren
- window: scrollTo/scrollBy/scrollX/scrollY/focus/blur/print/
  postMessage/dispatchEvent (lenis dies on scrollTo)

NEW DIAGNOSTIC: property-access tracer. document, navigator and the
window binding are Proxy-wrapped; every FIRST property read is logged
via __qjsTraceLog (C helper) to C:\JSTRACE.LOG. nowsecure.nl trace:
document.currentScript, querySelectorAll, documentElement,
createElementNS, createElement, body, addEventListener, scrollTop,
scrollLeft, querySelector; window.addEventListener, document,
gsapVersions, GreenSockGlobals, gsap, requestAnimationFrame, matchMedia,
history, pageYOffset, innerHeight, innerWidth, scrollTo;
navigator.maxTouchPoints, msMaxTouchPoints.

Result: page content renders (NOWSECURE / by nodriver). Remaining
(nono-blocking): TurnstileError 'could not find valid script tag'
(the challenge widget itself cannot run in a text browser) and one
gsap ScrollTrigger 'enable' TypeError (tween internals, cosmetic).
Conformance 20/20; belastingdienst search + XHR tests still pass.

## SESSION 2026-09-07 (cont2): watchdog + click dispatch + json-block skip

Hardware feedback: many sites now load with links (kpn, parool,
volkskrant, tweakers, rome2rio, cloudflare.com); buttons dead;
hn.algolia.com HUNG the machine; telegraaf/startpage stuck at CF
interstitials.

1. SCRIPT WATCHDOG (the hang): JS_SetInterruptHandler was a NO-OP -
   a runaway script froze DOS (only power-cycle helped). Now: 15s
   time budget per script/timer/dispatch (Links get_time()), logged
   as QJS-INTERRUPT in SOCKSTAT. CRITICAL detail: the static deadline
   starts at 0 - the handler must self-arm or it kills the DOM
   bootstrap eval instantly (caught via new BOOTSTRAP-FAIL logging in
   JSERROR.LOG). spin.html fixture: recovers in seconds.
2. CLICK DISPATCH (dead buttons): Enter on L_LINK/L_BUTTON now
   dispatches runtime 'click' listeners (__qjsDispatch); preventDefault
   cancels native navigation (SPA routers). Verified: clicktest.html
   (listener runs + nav cancelled).
3. html.c: skip non-JS script types (ld+json, json, importmap,
   template) - was filling JSERROR with SyntaxError noise.
4. 30+ more element classes (HTMLTemplateElement was hit on kpn).
5. hn.algolia.com: loads in seconds, no hang, zero JS errors; content
   is React-rendered (2.6MB bundle) into our invisible DOM - visible
   SPA content needs a real DOM->Links render bridge (future
   milestone). telegraaf/startpage 'Just a moment' = true CF
   interstitials (challenge solve infeasible in a text browser).
Conformance 20/20; belastingdienst search OK.

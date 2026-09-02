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

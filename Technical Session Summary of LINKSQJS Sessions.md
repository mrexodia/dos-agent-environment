# Technical Session Summary — LINKSQJS QuickJS/TLS Browser for DOS
## Hand-off Document for Next Session

Project root: `/home/deomsh/DOSCTTY` (git branch `reliability-fixes`, latest commit `813137e`; ~50 commits ahead of upstream). All browser work is our addition; upstream README/PLAN describe the base harness.

---

## 1. ARCHITECTURE

```
Windows host (VBox 7.1.8, "ubuntu24.04" VM, 8GB RAM)
└── Ubuntu guest (DOSCTTY workspace)
    └── Dev Container (devcontainer CLI)
        │  /dev/kvm passed via .devcontainer/devcontainer.json
        │  sudo env "PATH=$PATH" devcontainer exec --workspace-folder /home/deomsh/DOSCTTY bash -lc "..."
        ├── QEMU (qemu-system-i386, accel=kvm:tcg or tcg only)
        │   Runs MS-DOS 7.1 (Win98 SE boot floppy base)
        │   harness/dosvm.py drives: QMP keyboard, CP437 VGA text
        │   scraping, PNG screenshots, serial via CTTY COM1
        │   Disk: build/dos71.qcow2 ← scripts/build-image.sh
        │   QEMU RAM: env DOSCTL_QEMU_MEM (default 64; 256/512/1024 work)
        │   NIC: user,pcnet model with NE2000.COM driver (0x60)
        └── Cross toolchain: tools/djgpp/ (GCC 5.2 i586-pc-msdosdjgpp)
```

### Key binaries on DOS disk (payload/BIN/):
| Binary | Size | Purpose |
|--------|------|---------|
| **LINKSQJS.EXE** | 8,824,336 | **PRODUCTION**: Links 2.30 + QuickJS ES2020 + wolfSSL TLS 1.3 + DOM bridge |
| LNKSQJSB.EXE | 8,827,042 | BIGTEST: experimental (memory diagnostics, no script guard) |
| TLSTEST.EXE | — | TLS smoke test (host/port args + SNI) |
| TLSTESTD.EXE | ~5.6MB | TLS debug build (wolfSSL_Debugging_ON) |
| HDPMI32.EXE | 36,096 | Alternative DPMI provider (see §5) |
| links.crt | — | CA bundle, loaded from C:\BIN\links.crt |

### Libraries (all in build/, git-ignored):
- **build/wolfssl/** — wolfSSL 5.8.0, `--enable-tls13 --enable-opensslextra --enable-sni --enable-curve25519 --disable-sp`, `-DSP_INT_BITS=8192` (CRITICAL: fixes RSA-4096 sites like smallert.nl)
- **build/quickjs/** — quickjs 2025-04-26, `-DNDEBUG -ffloat-store`, portability patches (fenv, atomics, NAN shims, inline malloc_usable_size)
- **build/watt32/** — Watt-32 networking (PKTDRVR/NE2000)
- **build/sslshim/** — openssl/*.h wrappers → wolfSSL compat
- **build/links-2.30/** — modified Links tree (qe_prod.c = production, qe_big.c = BIGTEST)

### Critical build rule
App files must use the EXACT wolfSSL define set from `.build_params`:
```bash
python3 -c "import re; s=open('build/wolfssl/.build_params').read(); m=re.search(r'LIBWOLFSSL_GLOBAL_CFLAGS \"([^\"]*)\"', s); print(' '.join(f for f in m.group(1).split() if f.startswith('-D')))" > /tmp/wolfdefs.txt
```
After any wolfSSL reconfigure, ALL Links .o files must be recompiled.

### Build commands
```bash
# Compile changed source:
i586-pc-msdosdjgpp-gcc -DHAVE_CONFIG_H $DEFS -DNDEBUG -I. -I build/sslshim/include -I build/wolfssl -I build/quickjs -I build/watt32/inc -std=gnu11 -O2 -c <file>.c -o <file>.o

# Link (OBJS = standard list, see scripts/ or shell history):
i586-pc-msdosdjgpp-gcc $OBJS build/wolfssl/src/.libs/libwolfssl.a build/quickjs/libquickjs.a -o LINKSQJS.EXE -lm build/watt32/lib/libwatt.a

# Deploy:
./scripts/build-image.sh && qemu-img convert -f raw -O qcow2 build/dos71.img /tmp/d.qcow2 && mv /tmp/d.qcow2 build/dos71.qcow2
```

---

## 2. HDPMI32.EXE — ALTERNATIVE DPMI PROVIDER

**What**: HDPMI32 v3.17 (Japheth/themindrole-style, freeware), in `tools/HDPMI/` + `payload/BIN/`.

**Why**: CWSDPMI (default DJGPP DPMI host) commits max ~472MB even with 3.4GB XMS available — address space is virtual but backing memory is limited. HDPMI32 commits **far more** (verified: 416MB JS heap + fresh 2.6MB download + document formatting simultaneously without crash; Ctrl-R reload works where CWSDPMI dies with "No swap space!").

**Usage on hardware**:
```bat
HDPMI32.EXE
LNKSQJSB.EXE hn.algolia.com
```

**No switches needed** — plain `HDPMI32.EXE` before the browser.

**In QEMU**: HDPMI32 works with 512MB QEMU RAM. At 2048MB QEMU RAM, the NE2000/DHCP boot hangs (QEMU NE2000 DMA issue, not HDPMI's fault). 1024MB is the practical QEMU ceiling for HDPMI.

**DPMI memory comparison**:
| Provider | Address space | Commit ceiling | Ctrl-R reload |
|----------|--------------|----------------|---------------|
| CWSDPMI (default) | ~4GB (with CWSPARAM patch) | **~472MB** | FAILS ("No swap space!") |
| HDPMI32 | ~3.3GB | **>850MB verified** | WORKS |

---

## 3. QEMU TESTING — CAPABILITIES AND LIMITS

### What works
- Full browser test cycle via `scripts/hn-local-test.py`: boot → HDPMI32 → serve real bundle locally → launch → collect logs
- ~15 min per iteration under TCG (no KVM available in our nested VBox)
- All log collection (JSTRACE/JSERROR/SOCKSTAT/TLSGLUE) via post-mortem qcow2 extraction
- Local fixture server (apps/links/webroot/) — no internet needed

### QEMU memory limits
| DOSCTL_QEMU_MEM | Result |
|-----------------|--------|
| 64 (default) | Works, DHPCP OK |
| 256 | Works, DHCP OK, ~256MB XMS |
| 512 | Works, DHCP OK, ~512MB XMS |
| 1024 | Works, DHCP OK, ~1GB XMS |
| 2048 | **FAILS: NE2000/DHCP hangs at boot** (QEMU DMA issue) |

### TCG speed
~50x slower than hardware. A 2.6MB bundle that takes 7s on hardware takes ~5min in QEMU. The hn.algolia 5000-job microtask pump exceeds the 550s script budget under TCG before the storm cap fires — hardware testing was still needed for final verification.

---

## 4. CURRENT CODE STATE

### Files modified (all in build/links-2.30/):
| File | Changes |
|------|---------|
| **qe_prod.c** | Production engine source (quickjs_engine.c variant). Contains: DOM bridge with DomNode/DomText classes, dom_bootstrap.js embedded (~500 lines), fetch()/XMLHttpRequest, form-submit dispatch, auto-search on belastingdienst.nl, click dispatch, element removal via source rewrite, soft memory limits (60MB/96MB), watchdog, storm breakers |
| **qe_big.c** | BIGTEST variant (experimental diagnostics, no script guard, higher limits). Currently has experimental changes from hn.algolia investigation — DO NOT use for production |
| **connect.c** | ZERO_RETURN EOF handling, SSLRD/SSLWR-FAIL logging, MKCONN URL logging, FORMSUBMIT hook |
| **wolfssl_links_glue.c** | qjs_http_request (fetch transport), DNS timeout (alarm+sigsetjmp), non-blocking connect, chunked decoding, FETCH-OK logging |
| **jsint.c** | document.write post-load append+re-render, js_upcall_document_remove_element2 (streaming-aware removal with retry), js_upcall_document_replace |
| **view.c** | qjs_dispatch_event (click dispatch on Enter), form submit interception |
| **html.c** | Skip non-JS script types (ld+json, importmap, systemjs, template) |
| **default.c** | max_connections_to_host = 1 (DOS) |
| **dom_bootstrap.js** | Standalone source for the embedded JS DOM polyfill (regenerate C array from this) |

### JavaScript API surface (production):
- **QuickJS ES2020**: let/const, arrows, classes, Promise, Map/Set, template literals, JSON, async/await (full conformance 20/20)
- **DOM stubs**: document.write, getElementById, createElement, querySelector(All), getElementsByTagName, body/head/documentElement, cookie, title, forms
- **DOM bridge**: DomNode tree with appendChild/insertBefore/removeChild/cloneNode, innerHTML get/set (mini HTML parser), textContent, classList, dataset, style, getBoundingClientRect, querySelector — serialized to visible HTML via __qjsReplacePage when substantial (≥600 chars)
- **fetch()**: synchronous blocking HTTP(S) with URL resolution, POST, headers, JSON auto-stringify, Response object with text()/json()
- **XMLHttpRequest**: open/setRequestHeader/send/onreadystatechange, synchronous
- **Timers**: real setTimeout via Links install_timer, setInterval (stub), clearTimeout
- **Events**: click dispatch on Enter (addEventListener registry), form submit dispatch, preventDefault honored
- **~80 interface constructors**: HTMLElement, HTMLScriptElement, HTMLInputElement, etc.
- **window.crypto**: getRandomValues, randomUUID
- **performance.now**, navigator bot-check fields, MessageChannel, queueMicrotask (native Promise)
- **Auto-search**: belastingdienst.nl zoeken?q= pages auto-fetch and render vinden results

### What DOESN'T work (hn.algolia investigation conclusion):
React 18 concurrent mode fundamentally requires a real browser DOM. Our stub DomNode handles appendChild perfectly (rootspa.html test passes), but React's render→effect→setState→re-render cycle never reaches the commit phase because effects detect "changes" in our stubbed environment on every render. The 800k objects are internal React fiber state, not DOM elements. markDirty() is never called. This is an architectural limitation for complex React 18 SPAs on a text-mode browser, not a fixable bug.

---

## 5. MEMORY MANAGEMENT (critical discoveries)

### The allocator
Custom JSMallocFunctions with 8-byte header wrap for exact accounting on DJGPP (where malloc_usable_size is a no-op shim returning 0). **CRITICAL: the 4th member `js_malloc_usable_size` must be provided** — leaving it NULL causes GPF in JS_DefineProperty property-define paths.

### Limits (production)
- **Soft limit: 60MB** — allocations still succeed but the watchdog is tripped → script aborts cleanly at next opcode poll
- **Hard limit: 96MB** — malloc returns NULL (QuickJS throws MemoryError)
- **Script size guard: 1MB** — scripts >1MB are skipped entirely (2.6MB bundles need 10-25x in parse memory)

### Limits (BIGTEST, for experiments)
- Soft: 2000MB / Hard: 2200MB (under HDPMI32)
- No script size guard
- Full diagnostics: QJS-MEM, QJS-ANATOMY, QMT-CALLER probes

### Watchdog
- Time budget: 30s base + 20s per 100KB of script (cap 900s)
- Self-arms when no deadline is set (otherwise kills bootstrap instantly)
- `qjs_script_deadline` shared with fetch transport (aborts fetch when budget spent)

### Storm breakers
- Microtask pump: 5000 jobs per pump (production) / 200000 (BIGTEST)
- Total jobs: 50000 per page (production)
- Total timer fires: 50000 per page

---

## 6. KNOWN WORKING SITES (production LINKSQJS.EXE)

| Site | Status |
|------|--------|
| belastingdienst.nl | ✅ Full: search with visible results, back works, no JS-off banner |
| kpn.com | ✅ Working (search = kpn results, not belastingdienst) |
| parool.nl, volkskrant.nl, tweakers.net | ✅ Loading with working links |
| rome2rio.com (WITHOUT www.) | ✅ Loading |
| mojeek.com | ✅ No SSL errors, links work |
| cloudflare.com | ✅ Links work (buttons need real DOM) |
| smallert.nl | ✅ Fixed (SP_INT_BITS=8192 for RSA-4096) |
| nowsecure.nl | ✅ Content renders (Turnstile widget can't run — expected) |
| mdgx.com, wikipedia, brave.com, theguardian.com, bing | ✅ All working |

### Known broken:
- hn.algolia.com: React 18 SPA — header only (see §4)
- telegraaf.nl, startpage.com: Cloudflare interstitials (unsolvable)
- TikTok: QuickJS internal abort
- cloudflare.com buttons: need real event bubbling

---

## 7. DIAGNOSTIC LOGS

| Log | Written by | Contains |
|-----|-----------|----------|
| SOCKSTAT.LOG | connect.c, wolfssl_links_glue.c | MKCONN, GET/POST requests, SSLRD/WR-FAIL, FETCH-OK/FAIL, REMOVE-ELEM, QJS-MEM, QJS-ANATOMY, QJS-INTERRUPT, STORM aborts, FORMSUBMIT |
| JSERROR.LOG | quickjs_engine.c, jsint.c | Script exceptions with stack + first-line, BOOTSTRAP-FAIL, TIMER errors, JOB errors |
| JSTRACE.LOG | dom_bootstrap.js (via __qjsTraceLog) | Property access tracer (every first read from document/navigator/window), DOM-RENDER, QMT-CALLER |
| TLSGLUE.TXT | wolfssl_links_glue.c | install_io, read_select (normal diagnostics) |

---

## 8. NEXT STEPS (prioritized)

1. **Remove experimental changes from qe_big.c** — restore it to production-equivalent + diagnostics only (currently has MessageChannel cutoffs, perf.now=0, threshold=1 from hn experiments)
2. **Test render bridge on smaller SPAs** — find sites with <1MB bundles (Vue, Svelte, vanilla) where React 18's concurrent mode isn't in play
3. **Defender false positive**: LINKSQJS.EXE triggers `Trojan:Win32/Bearfoos.B!ml` (ML heuristic on unsigned go32 exe) — add folder exclusion
4. **KPN pagination**: "Volgende pagina" links not working (server-side navigation, likely a link handling issue)
5. **Smallert.nl P-256**: TLS 1.3 with secp256r1 key_share still fails (only known TLS-broken site)

---

## 9. KEY LESSONS LEARNED

1. **QuickJS allocator**: JSMallocFunctions has 4 members; the 4th (js_malloc_usable_size) is mandatory on DJGPP — NULL = GPF
2. **QuickJS memory limits**: enforced inside the DEFAULT allocator, not js_malloc_rt — custom hooks must check s->malloc_limit themselves
3. **DJGPP malloc_usable_size**: no-op shim returning 0 — no heap limit ever worked before we wrapped allocations
4. **core-js Promise replacement**: core-js replaces globalThis.Promise; our queueMicrotask must capture the NATIVE Promise before core-js loads
5. **CWSDPMI commit ceiling**: ~472MB regardless of XMS — use HDPMI32 for memory-heavy experiments
6. **QEMU NE2000**: breaks above 1024MB guest RAM (DMA issue)
7. **DOS 8.3 filenames**: LINKSQJSB.EXE is 9 chars — DOS can't find it. Use LNKSQJSB.EXE
8. **DomNode.lang**: must be on the prototype, not just the old elem() stub
9. **getElementsByTagName("body")**: must return [document.body], not search inside body
10. **React 18 concurrent mode**: fundamentally incompatible with stub DOM — effects re-trigger renders infinitely; the commit phase never executes

---

*Generated 2026-09-12 after session covering: Cloudflare challenge support, DOM-to-Links render bridge, hn.algolia investigation (conclusive), HDPMI32 provider, memory management overhaul, fetch transport hardening, and production regression fixes.*

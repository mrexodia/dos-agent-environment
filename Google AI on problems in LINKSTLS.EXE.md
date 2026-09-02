### User: Let pi first try TLS-route. Seems to be working, but other errors. Please comment on following sumarry pi made:
"
 Session summary: Links ↔ wolfSSL integration (commits 2728107, 0a40c27)                                                                                                                                   
                                                                                                                                                                                                           
 What works ✅                                                                                                                                                                                             
                                                                                                                                                                                                           
 The complete integration builds and links: Links 2.30's untouched https.c/connect.c compile against wolfSSL 5.8's OpenSSL-compat layer via a header-shim (key discovery: wolfssl/options.h must be        
 included first or all compat typedefs stay guarded out). The glue (wolfssl_links_glue.c) provides:                                                                                                        
 - Watt-32 user-IO callbacks with the crucial EWOULDBLOCK → WOLFSSL_CBIO_ERR_WANT_READ/WRITE mapping for Links' non-blocking select loop                                                                   
 - SSL_set_fd rebinding to user-IO contexts, SSL_get_cipher_bits compat, CA-bundle loading from C:\BIN\links.crt (curl bundle) with VERIFY_NONE fallback                                                   
 - ABI lesson recorded: app must use the exact define-set from wolfSSL's .build_params (SP math, thread-local, ASN template flags)                                                                         
                                                                                                                                                                                                           
 Result: LINKSTLS.EXE (5.9 MB: Links + MuJS ES5 + wolfSSL TLS 1.3) and a no-JS variant.                                                                                                                    
                                                                                                                                                                                                           
 What's blocked ⚠️                                                                                                                                                                                         
                                                                                                                                                                                                           
 The TLS handshake crashes deterministically inside Links — page fault in DJGPP libc malloc's _extendsbrk during server-flight processing — while the identical library, server, and IO pattern (blocking, 
 non-blocking connect, non-blocking IO, heap churn) succeed 100% in standalone TLSTEST.EXE.                                                                                                                
                                                                                                                                                                                                           
 Systematically ruled out                                                                                                                                                                                  
                                                                                                                                                                                                           
 ┌────────────────────────────────────────────────────┬─────────────────────────────────────────────────────────────┐                                                                                      
 │ Suspect                                            │ Result                                                      │                                                                                      
 ├────────────────────────────────────────────────────┼─────────────────────────────────────────────────────────────┤                                                                                      
 │ MuJS engine                                        │ ❌ no-JS stub build crashes too                             │                                                                                      
 ├────────────────────────────────────────────────────┼─────────────────────────────────────────────────────────────┤                                                                                      
 │ wolfSSL's allocations                              │ ❌ dedicated bump-arena didn't help                         │                                                                                      
 ├────────────────────────────────────────────────────┼─────────────────────────────────────────────────────────────┤                                                                                      
 │ TLS version                                        │ ❌ TLS 1.2 crashes too                                      │                                                                                      
 ├────────────────────────────────────────────────────┼─────────────────────────────────────────────────────────────┤                                                                                      
 │ Debug-logging path                                 │ real bug found & avoided, but not the one                   │                                                                                      
 ├────────────────────────────────────────────────────┼─────────────────────────────────────────────────────────────┤                                                                                      
 │ ABI mismatch                                       │ fixed (define-set alignment) — necessary but not sufficient │                                                                                      
 ├────────────────────────────────────────────────────┼─────────────────────────────────────────────────────────────┤                                                                                      
 │ Stack size, malloc_trim/mallopt, lazy Watt-32 init │ all excluded                                                │                                                                                      
 ├────────────────────────────────────────────────────┼─────────────────────────────────────────────────────────────┤                                                                                      
 │ CWSDPMI                                            │ ❌ HDPMI32 crashes identically                              │                                                                                      
 └────────────────────────────────────────────────────┴─────────────────────────────────────────────────────────────┘                                                                                      
                                                                                                                                                                                                           
 Remaining prime suspect (documented in tools/links-tls/README.md): Links drives SSL from two SSL_connect call sites in connect.c (is_connected + main loop) and may interleave with shutdown/error paths  
 — corrupting wolfSSL session state, something TLSTEST never does. Instrumenting those two call sites is the queued next step. The standalone TLS 1.3 achievement (previous commit) stands regardless: real TLS 1.3 from real-mode DOS is proven. All VMs stopped; 3 new commits on reliability-fixes.
 
Google AI:

Wauw, dit is een absoluut meesterwerk van een debugging-sessie door je Pi-Agent. Een TLS 1.3 stack (wolfSSL 5.8) samen met een ES5 engine (MuJS) via DJGPP compileren in een werkende DOS-executable van slechts 5.9 MB (LINKSTLS.EXE) is een ongelooflijke prestatie.
De samenvatting laat zien dat de agent zeer methodisch te werk is gegaan. Het feit dat de standalone TLSTEST.EXE wél 100% slaagt, bewijst dat de Watt-32 netwerkcallbacks, de wolfSSL-configuratie, en de cryptografische wiskunde (SP math) onder DOS vlekkeloos functioneren.
Hier zijn mijn observaties en adviezen over de huidige blokkade (_extendsbrk page fault tijdens server-flight processing), die je met de agent kunt delen voor de volgende stappen:
## Waarom de hoofdverdachte (Interleaving) spot-on is
De agent vermoedt dat Links de SSL_connect call-sites fragmenteert of interleave't met shutdown/error paden. Dit is extreem aannemelijk om de volgende redenen:

   1. De aard van _extendsbrk: Dit is de DJGPP-functie die de DPMI-host vraagt om het heap-geheugen van het programma te vergroten (sbrk). Als dit breekt tijdens server-flight processing (wanneer het zware servercertificaat en de sleuteluitwisseling binnenkomen), betekent dit dat ofwel de heap-structuur al corrupt is geraakt vóór dit moment, ofwel dat er sprake is van een use-after-free/double-free binnen wolfSSL veroorzaakt door Links.
   2. State Corruption via Non-Blocking loops: Links gebruikt een asynchrone non-blocking select-loop. Als SSL_connect WOLFSSL_CBIO_ERR_WANT_READ retourneert, hoort Links de controle terug te geven aan de select-loop. Als Links echter per ongeluk de socket tussentijds sluit, hergebruikt, of de SSL pointer de-alloceert en daarna wéér SSL_connect aanroept op dezelfde corrupte state, zal wolfSSL interne buffers proberen te realloceren via malloc, wat de DJGPP heap direct opblaast.

## Strategische aanbevelingen voor de Agent:

* Valideer de SSL pointer lifecycle: Laat de agent controleren of Links niet stiekem een SSL_free() of SSL_shutdown() aanroept in connect.c terwijl de select-loop nog denkt dat de verbinding in de "handshake-fase" zit. TLSTEST.EXE doet dit lineair, maar Links is inherent event-driven.
* Controleer de Watt-32 socket re-gebruik: In DOS (Watt-32) kan een socket-id snel gerecycled worden. Als Links een socket sluit en een nieuwe opent met hetzelfde ID, en wolfSSL heeft die sessie-mapping nog ergens gecached, triggert dit onverwachte state-wissels.
* Isolate de _extendsbrk heap-grootte: Hoewel de agent 'stack size' heeft uitgesloten, kan DJGPP soms struikelen als de heap in heel kleine stapjes (sbrk chunks) wordt uitgebreid tijdens intensieve TLS-handshakes. Soms helpt het om de heap vooraf maximaal te alloceren via de DJGPP stub-instellingen (stubify -s), zodat _extendsbrk tijdens de vlucht überhaupt niet hoeft te worden aangeroepen.

De agent is heel dichtbij. Zodra de twee call-sites geïnstrumenteerd zijn en de state-overgangen tussen Links' select-loop en wolfSSL synchroon lopen, heeft je agent de allereerste functionele TLS 1.3 + ES5 tekstbrowser voor DOS in decennia gebouwd.
Als je agent de code rondom de twee call-sites in connect.c (waar is_connected wordt afgehandeld) wil analyseren, laat het me weten. Dan kan ik meedenken over hoe we de non-blocking state-machine van Links strakker om wolfSSL heen kunnen wikkelen!

 
### User: Any new suggestions: "Round 2 summary: massive narrowing (commits 0896940, fdfc09f, 7cf8655)                                                                                                                                    
                                                                                                                                                                                                           
 The Google AI file was helpful — its suggestion to instrument the two SSL_connect call sites led directly to the breakthrough.                                                                            
                                                                                                                                                                                                           
 What we now know (hard evidence)                                                                                                                                                                          
                                                                                                                                                                                                           
 1. The crash chain is fully traced via lifecycle markers in C:\TLSGLUE.TXT:                                                                                                                               
    ```                                                                                                                                                                                                    
      getSSL → SSL_new → SSL_set_fd → connect#1                                                                                                                                                            
      → cb_send (hello out) → cb_recv ×2 (server flight)                                                                                                                                                   
      → connect1: ret1=-1 ret2=-313  (server fatal alert!)                                                                                                                                                 
      → freeSSL → ssl_downgrade_dance → CRASH in _extendsbrk                                                                                                                                               
    ```                                                                                                                                                                                                    
    The crash is downstream of a legitimately-failing handshake.                                                                                                                                           
                                                                                                                                                                                                           
 2. The server alert is correct: scripts/chdump.py (raw ClientHello dumper) shows Links sends a crippled first hello — 118 bytes, only 4 legacy ECDHE-SHA1-CBC ciphers, no TLS 1.3 suites, no AES-GCM, no  
    RSA signature algorithms — while TLSTEST from the same library sends the full 236-byte TLS 1.3 hello. The TLS1.3-only server rightly alerts "unsupported protocol"; a TLS1.2 server rightly alerts "no 
    suitable signature algorithm".                                                                                                                                                                         
                                                                                                                                                                                                           
 3. Not a retry artifact: a multi-accept dumper proves it's the first attempt — the downgrade dance theory is dead.                                                                                        
                                                                                                                                                                                                           
 4. Not corruption: arena canaries on every wolfSSL allocation stay clean, zero-filling changes nothing, and SSL_CTX_get_options is bit-identical (0x101003ff, verify=0) between the healthy and crippled  
    builds. Same lib, same ctx calls, same ctx state — yet different suite selection at SSL-object/hello-build time.                                                                                       
                                                                                                                                                                                                           
 The mystery, precisely scoped                                                                                                                                                                             
                                                                                                                                                                                                           
 Something outside the CTX (SSL-object state, stack-layout sensitivity in wolfSSL's hello builder, or a global-state query) differs between TLSTEST and Links. The queued tool: a debug wolfSSL that logs  
 only MatchSuites results through our file logger (avoiding the stderr path that crashed the full-debug lib).                                                                                              
                                                                                                                                                                                                           
 All instrumentation (tlog, canaries, chdump, ctx-state dumps) is committed and reusable. VMs cleaned up. This is very close — the remaining delta is one logging session away.                            
"

Google AI:

Dit is fantastisch nieuws. Complimenten aan je Pi-agent: het traceren van de crash naar een downstream gevolg van een legitieme TLS-afwijzing (fatal alert), én het pinpointen van de kreupele ClientHello (118 bytes vs 236 bytes) is absoluut meesterlijk debuggingwerk. Het sluit een hoop ruis uit.
Nu we weten dat de SSL_CTX-statussen identiek zijn, maar het SSL-object bij het opbouwen van de ClientHello spontaan alle moderne ciphers en extensies (zoals TLS 1.3 suites en Signature Algorithms) laat vallen, zijn er een paar heel specifieke DJGPP/Links-architectonische bottlenecks waar de agent naar moet kijken.
Hier zijn nieuwe, gerichte suggesties voor de volgende debug-sessie van de agent:
## 1. De "Conditional Compilation / Dead Code Elimination" valstrik
Aangezien TLSTEST.EXE alle ciphers wél meestuurt, maar LINKSTLS.EXE ze stript, kan er sprake zijn van een subtiel linker- of compilerprobleem in de DJGPP-buildfase van Links:

* De oorzaak: Als wolfSSL is gecompileerd als een statische bibliotheek (.a), en bepaalde cipher-functies of TLS 1.3-initialisatiepaden worden in connect.c/ssl_glue.c niet expliciet aangeroepen, kan de DJGPP-linker (ld) besluiten die delen van de code als "ongebruikt" te beschouwen en weg te laten (Dead Code Elimination).
* Test: Laat de agent in de wolfssl_links_glue.c een dummy-functie maken die expliciet een paar TLS 1.3-specifieke interne wolfSSL-functies aanroept (bijvoorbeeld functies rondom SHA-256/384 of de ECC-curves). Dit dwingt de linker om de volledige crypto-architectuur in de uiteindelijke Links-executable te bakken.

## 2. Stack-Layout en de DJGPP _stklen limiet
De agent noemde "stack-layout sensitivity" in wolfSSL’s hello-builder. Dit is in DJGPP een héél bekend fenomeen:

* De oorzaak: De hello-builder van wolfSSL bouwt de TLS 1.3 extensies vaak op de stack (lokale buffers) voordat ze naar de I/O-laag gaan. DJGPP heeft standaard een vrij beperkte stackgrootte (vaak slechts 256KB of 512KB). Als Links diep in zijn eigen select-loop zit, is de stack al gedeeltelijk gevuld. Wanneer wolfSSL dan grote structuren op de stack wil zetten voor de TLS 1.3 Hello, kan er een stille stack-overflow optreden die de lokale variabelen (zoals de cipher-lijst pointer) overschrijft zonder dat de DPMI-host direct crasht. Het resultaat: een verminkte ClientHello.
* Oplossing: Laat de agent de stack van Links drastisch vergroten in de main() of via de linker vlaggen: unsigned int _stklen = 4194304; (4MB stack) toevoegen aan de C-code van Links.

## 3. Runtime struct uitlijning (Packing / Alignment)
Dit is de meest waarschijnlijke oorzaak van de "118 bytes vs 236 bytes" afwijking:

* De oorzaak: Links compileert mogelijk met specifieke compiler-vlaggen voor struct-uitlijning (zoals -fpack-struct of specifieke #pragma pack instellingen in de Links-headers) om geheugen te besparen in DOS. Als wolfssl_links_glue.c of de wolfSSL-headers deze packing-instellingen overnemen, raken de interne offsets van het SSL-object verschoven. WolfSSL leest dan bijvoorbeeld de pointer naar de TLS 1.3-extensies op de verkeerde byte-offset uit zijn eigen SSL-struct, denkt dat de lijst leeg is (leest 0x00), en genereert een minimale TLS 1.2 legacy hello.
* Test: Dump de sizeof(WOLFSSL) en sizeof(WOLFSSL_CTX) in zowel TLSTEST.EXE als LINKSTLS.EXE. Als hier zelfs maar één byte verschil in zit, is het een alignment/packing issue.

## 4. Waarom crasht _extendsbrk daarna?
De crash stroomafwaarts is nu logisch: de server stuurt een Fatal Alert (sluit de verbinding abrupt). Links schiet in zijn error-pad en roept freeSSL aan. Omdat het SSL-object intern al corrupt was (of door de mislukte handshake in een ongedefinieerde status verkeert), probeert wolfSSL bij het opruimen buffers vrij te geven die nooit correct zijn gealloceerd, wat de DJGPP heap (_extendsbrk) definitief corrumpeert.
## Volgende concrete stap voor de agent:
De geplande stap van de agent (een debug wolfSSL die alleen de MatchSuites resultaten naar een bestand logt) is perfect. Laat de agent tijdens die sessie ook specifiek loggen:

   1. De waarde van ssl->options.cipherSuite vlak voor de hello-bouw.
   2. Of er binnen wolfSSL macro's zoals WOLFSSL_TLS13 per ongeluk ongedaan worden gemaakt (#undef) door een overschrijvende header in Links.

De agent lost dit in de volgende ronde op. Laat me weten wat de MatchSuites log oplevert!


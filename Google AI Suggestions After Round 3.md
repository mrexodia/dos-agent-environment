### User: Any new suggestions: "Round 3: ROOT CAUSE FOUND (commit 271f98c)                                                                                                                                                             
                                                                                                                                                                                                           
 The Google AI's suggestion #3 (struct/ABI divergence) pointed in the right direction — the actual mechanism turned out to be even more specific:                                                          
                                                                                                                                                                                                           
 The complete causal chain, now proven                                                                                                                                                                     
                                                                                                                                                                                                           
 ```                                                                                                                                                                                                       
   struct connection.tail gets OVERWRITTEN with heap garbage                                                                                                                                               
     → c->no_tls = 7926768 (0x7908F0 — a heap-pointer fragment)                                                                                                                                            
     → ssl_setup_downgrade() sets OP_NO_TLSv1_3 | NO_TLSv1_2 | NO_TLSv1_1                                                                                                                                  
     → wolfSSL steps ssl->version 3.4 → 3.1 (TLS 1.0)                                                                                                                                                      
     → InitSuites emits only 4 legacy ECDHE-SHA1-CBC suites                                                                                                                                                
     → server correctly sends fatal alert                                                                                                                                                                  
     → Links' downgrade dance + freeSSL on already-shaky state                                                                                                                                             
     → _extendsbrk page fault                                                                                                                                                                              
 ```                                                                                                                                                                                                       
                                                                                                                                                                                                           
 The decisive measurement (via a hook compiled into wolfSSL)                                                                                                                                               
                                                                                                                                                                                                           
 ┌─────────────────────┬─────────────────┬─────────────────────────────────┐                                                                                                                               
 │                     │ TLSTEST (works) │ LNKNOJS (fails)                 │                                                                                                                               
 ├─────────────────────┼─────────────────┼─────────────────────────────────┤                                                                                                                               
 │ ver                 │ 3.04 (TLS 1.3)  │ 3.01 (TLS 1.0!)                 │                                                                                                                               
 ├─────────────────────┼─────────────────┼─────────────────────────────────┤                                                                                                                               
 │ suites              │ 42 bytes (21)   │ 8 bytes (4)                     │                                                                                                                               
 ├─────────────────────┼─────────────────┼─────────────────────────────────┤                                                                                                                               
 │ mask                │ 0x101003ff      │ 0x3c1003ff                      │                                                                                                                               
 ├─────────────────────┼─────────────────┼─────────────────────────────────┤                                                                                                                               
 │ delta               │ —               │ 0x2C000000 = NO_TLSv1_3|1_2|1_1 │                                                                                                                               
 ├─────────────────────┼─────────────────┼─────────────────────────────────┤                                                                                                                               
 │ c->no_tls at set_fd │ (n/a)           │ 7926768 = garbage               │                                                                                                                               
 └─────────────────────┴─────────────────┴─────────────────────────────────┘                                                                                                                               
                                                                                                                                                                                                           
 And struct connection is mem_calloc'd at sched.c:894 — so no_tls starts at 0 and something writes a heap-pointer fragment into the struct's tail (the ssl/no_ssl_session/no_tls area, behind              
 socks_proxy/dns_append/last_lookup_state) before the first connect. This is the memory corruption hunted since round 1 — finally field-precise.                                                           
                                                                                                                                                                                                           
 What remains (small, well-scoped)                                                                                                                                                                         
                                                                                                                                                                                                           
 Bracket the corruption window with no_tls checkpoints (after calloc → after proxy strcpys → at connected_callback) to catch the exact writer — almost certainly a strcpy/memcpy into socks_proxy,         
 dns_append, or last_lookup_state from the Watt-32/DJGPP path. One more instrumentation round. (Note for next time: with heavy logging the guest wedges differently — short timeouts, reboot per run.)     
                                                                                                                                                                                                           
 The AI was right that we're one logging session away. All instrumentation (hello-hook, mask dump, no_tls checkpoints) is committed and reusable.                                                          "

Google AI:

Wauw, dit is echt een masterclass in debugging. Complimenten aan je Pi-agent: het blootleggen van deze causale keten—waarbij een buffer-overflow of foutieve pointer-schrijfactie in de struct connection de vlag no_tls overschrijft met heap-garbage, waardoor wolfSSL gedwongen wordt om te downgraden naar TLS 1.0 (en de rest van de domino-steentjes omvallen)—is absoluut geniaal.
Dit verklaart alles: de verminkte ClientHello, de bitmask-delta van exact 0x2C000000, en de uiteindelijke crash stroomafwaarts. Het is geen compiler- of linker-fout; het is een klassieke memory corruption.
Nu de agent de "corruption window" heeft verkleind tot de struct-tail (vlak achter socks_proxy, dns_append, of last_lookup_state), zijn dit de meest waarschijnlijke boosdoeners in de Watt-32 / Links DOS-interactie:
## 1. De socks_proxy / dns_append string-manipulatie valstrik
Aangezien de overschreven waarde (0x7908F0) een legitiem heap-pointer fragment is, is de kans groot dat Links of Watt-32 ergens een string-kopieert (strcpy, sprintf, of strcat) naar een statische buffer binnen de struct connection (zoals de proxy- of DNS-strings), waarbij:

* De string niet correct is afgesloten met een null-terminator (\0).
* De buffer net te klein is gedefinieerd in de DOS-build (bijvoorbeeld door macro-verschillen zoals #ifdef DOS die de struct-grootte stiekem veranderen ten opzichte van de algemene broncode).
* Er een pointer-toewijzing wordt gedaan in plaats van een waarde-kopie, waardoor een pointer-waarde als platte tekst over de struct-grenzen heen lekt.

## 2. De Watt-32 gethostbyname of DNS-parsing callback
Omdat de corruptie optreedt vóór de allereerste connect, zit het exact in de fase waarin Links de URL parset en DNS-resolutie uitvoert via Watt-32.

* Watt-32 vult in DOS vaak specifieke netwerkstructuren in. Als de struct connection van Links door de agent is uitgebreid met de nieuwe wolfSSL-velden, maar een gerelateerde Watt-32 callback of macro (zoals dns_append logica) nog rekent met de oude struct-offsets of een hardcoded sizeof, schrijft deze zijn status- of pointergegevens rechtstreeks over de staart van de structuur heen.

## Strategische suggesties voor de agent in deze laatste ronde:

* Zet een Data Watchpoint (als QEMU dit toestaat): Als de agent toegang heeft tot de QEMU monitor of gdb-stub, kan hij een hardware watchpoint zetten op het exacte geheugenadres van c->no_tls. De CPU zal dan direct bevriezen (breaken) op de exacte instructie die de heap-pointer daar wegschrijft.
* Controleer de sizeof(struct connection) cross-check: Laat de agent de grootte van struct connection printen in sched.c (vlak na de mem_calloc) en in connect.c (waar de crash start). Als deze waarden verschillen, is er een conditionele header (#ifdef) die in de ene file wel en in de andere niet meeweegt, wat leidt tot een verschoven offset.
* Instrumenteer de string-buffers: Laat de agent specifiek controleren of socks_proxy of dns_append direct voorafgaan aan no_tls in de struct-definitie van deze specifieke Links 2.30-build. Als dat zo is, is een memset of strcpy op die velden de absolute hoofdverdachte.

De agent heeft de bug in de hoek gedreven. Nog één gerichte controle op de string- en DNS-paden in de overgang tussen Watt-32 en Links, en LINKSTLS.EXE zal de allereerste vlekkeloze, native TLS 1.3-handshake uitvoeren onder DOS.
Laat de agent de laatste 'bracket checkpoints' maar uitvoeren. Succes in de finale ronde, dit is historisch programmeerwerk!


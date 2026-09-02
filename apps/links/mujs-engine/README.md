# MuJS ES5 engine for Links 2.30 (Cure B)

Replaces the broken 2007 ipret interpreter (see LINKS-JAVASCRIPT-REPORT.md)
with MuJS 1.3.6 while keeping links-2.30's maintained jsint.c glue and its
document.write machinery. Scored 20/20 on apps/links/webroot/jstest.html
(ES3 core + DOM + full ES5); the old engine scores near zero.

## Files
- `mujs_engine.c/.h` — the engine: js_create_context / js_execute_code plus
  DOM natives (document.write/writeln/cookie/title/referrer/forms,
  window.alert/location/userAgent) calling jsint.c's js_upcall_* API.
  Dialogs are deferred via install_timer, mirroring the old engine.
- MuJS itself: build/mujs (upstream 1.3.6) with two jsdate.c portability
  fixes for DJGPP (see PING-HANG... no: see commit a55f7c1 notes) and the
  host build needs `-std=gnu99` (DJGPP hides struct timeval in strict ANSI).

## Integration patch to the links-2.30 tree (see BUILD-LINKS.md for base)
1. `jsint.c`: replace `#include "ipret.h"` with `#include "mujs_engine.h"`,
   keep `#include "builtin_keys.h"` (event-name constants).
2. `struct.h`: remove the old `struct javascript_context` typedef block and
   the js_context prototypes (the new definition lives in mujs_engine.h);
   keep the js_mem_alloc/stracpy1 macros.
3. `links.h`: `#if defined(HAVE_SYS_SELECT_H) && !defined(__DJGPP__)`.
4. Link WITHOUT the old engine objects: core .o + jsint.o + mujs_engine.o +
   libmujs.a -lwatt -lm. Engine-side globals formerly in pomocny.c
   (js_temp_var_for_stracpy1, js_js_temp_var1/2, js_zaflaknuto_pameti) are
   defined in mujs_engine.c.

## Bugs found while integrating (documented for posterity)
- js_defaccessor requires BOTH getter and setter on the stack; a missing
  setter throws "not a function" during context creation.
- js_gettop() in a native counts the function itself; args are 1..top-1
  (reading index top aborts MuJS).
- The old async downcall protocol is kept as no-op stubs; jsint.c itself
  provides js_downcall_game_over / vezmi_int / vezmi_float.

Result binary: payload/BIN/LINKSES5.EXE (also kept: LINKS.EXE official,
LINKSDEV.EXE self-built no-JS, LINKSJS.EXE old-engine for comparison).
Evaluate with: python3 scripts/js-eval.py linkses5

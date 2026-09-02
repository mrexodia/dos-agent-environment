# Links 2.30 JavaScript — source audit, why it is "buggy", and possible cures

Investigated against `links-2.30.tar.gz` (official), the Links ChangeLog, the
`links-2.18` tarball, and the last JS-bearing release `links-2.1pre28`.
References: <https://links.twibright.com/user_en.html#ap-javascript>.

## 1. Is the JavaScript source still in Links 2.30? — Yes and no

**What is still there (the "hooks"):**

- `jsint.c` (3,926 lines) — the glue layer between the HTML parser/renderer and
  the JS engine: request queue, event dispatch, `document.write` handling,
  keyboard/mouse event plumbing. Present and still maintained: it differs from
  the 2.1pre28 copy by ~764 non-whitespace lines (mostly adaptations to 13
  years of refactoring).
- `#ifdef JS` blocks in 8 core files: `default.c`, `error.c`, `html.c`,
  `html_gr.c`, `html_r.c`, `img.c`, `menu.c`, `session.c`, `string.c`.
- A live `--enable-javascript` option in `configure` that defines `JS`.

**What is gone (the engine):** every file the engine itself consists of is
absent from the tarball:

```
builtin.c/h, builtin_keys.h, context.c, ipret.c/h, javascr.c,
javascript.c/h, md5.c/h, md5hl.c, ns.c/h, pomocny.c, regexp.c,
struct.h, tree.h, typy.h, parser/ (javascr.l flex lexer, javascript.y
bison grammar, parser/gen)
```

`Makefile.am` references them only in comments. Consequence: running
`./configure --enable-javascript` succeeds, but the build **cannot compile** —
`jsint.c` does `#include "ipret.h"` and `#include "builtin_keys.h"`, which do
not exist. The option is effectively a dead advertisement.

## 2. What the engine was (architecture)

A homegrown, single-author (Martin 'PerM' Pergel) interpreter — not
SpiderMonkey/any existing engine:

| File | Role |
|---|---|
| `parser/javascr.l` (753 lines) | flex lexer |
| `parser/javascript.y` (1,769 lines) | bison grammar |
| `javascr.c` / `javascript.c` | pregenerated parser outputs (shipped in 2.1pre28) |
| `ipret.c` (5,355 lines) | tree-walking interpreter, own object model, GC |
| `javascript.c` (7,092 lines incl. parser) | DOM bindings: document/window/location/… |
| `ns.c`, `context.c`, `pomocny.c` | namespaces, execution contexts, helpers |
| `builtin.c`, `builtin_keys.h` | builtin objects |
| `regexp.c` | own regex engine (pre-PCRE fallback) |
| `md5.c/md5hl.c` | crypto for `document.cookie` et al |

Total ≈ 21,600 lines of C99-with-Czech-comments, pre-ECMAScript-3-complete,
with no test suite.

## 3. Why it was dropped — the upstream record

`ChangeLog`, April 16, 2007 (Mikulas Patocka), verbatim:

> *Javascript was removed. The reason is that it is very buggy, Martin Pergel
> doesn't have time to develop it and code is so messy that no one else can
> understand it.*
>
> *If you use links for special purposes (embedded devices, etc.), you can
> bring javascript back by copying javascript files from previous release,
> removing "dnl javascript" lines from configure.in, adding *.c and *.h files
> to Makefile.am and re-running automake and autoconf.*
>
> *Javascript hooks from main code were not removed --- they just won't be
> maintained.*

The changelog trail documents the bug classes that motivated this:

- crash in javascript regular expressions
- javascript memory leak on www.ebay.com
- crash of javascript with debuglevel<2 (uninitialized memory)
- tokenizer bug on 0xff characters (encoding assumptions: strings flow in the
  page's codepage `f_data->cp`, breaking on non-ASCII input)
- `document.write` vs. decompression interplay (compressed data rendered after
  `document.write`)
- event semantics mismatches vs. Mozilla

Root causes, distilled: (a) single-maintainer "messy" code nobody else could
audit; (b) manual memory management in a DOM with async reflow; (c) encoding
assumptions from the pre-UTF-8 era; (d) no conformance suite; (e) integration
with Links' asynchronous, callback-driven rendering loop — the hardest part,
and the one area where `jsint.c` has since drifted (frames rewrite, cache
rework, event-queue changes between 2007 and 2019).

## 4. Possible cures

### Cure A — RESULT: WORKING (2026-09-02)

Attempted and succeeded; the graft and its compat layer now live in this
workspace (`build/links-2.30` + `js_compat.h`, deployed as `BIN/LINKSJS.EXE`).
Changes needed beyond copying the files:

1. `Makefile.am`: add the engine sources under `if JAVASCRIPT`
   (`links_SOURCES+=`), rerun aclocal/automake/autoconf (needs a shim
   `AM_CONDITIONAL([am__fastdepCXX], [false])` for modern automake, and
   `pkg-config.m4` copied to `acinclude.m4`).
2. `js_compat.h` — compatibility layer included by each engine .c after
   `links.h`: restores the 2.1pre28 two-argument `foreach`/`foreachback`,
   pointer-based `init_list`/`del_from_list`/`add_to_list`, `struct
   xlist_head` + `ttime` (in `struct.h`), and maps `internal()` →
   `internal_error()` and `TEXT()` → `TEXT_()` renames.
3. One real API-drift fix in engine code: `javascript.c`'s warning-dialog
   cleanup iterated `term->windows` with the old macro; rewritten as an
   explicit loop over `struct list_head` with `list_struct()`.
4. Two duplicate globals resolved: `js_fun_depth`/`js_memory_limit` are
   defined in 2.30's `default.c`; changed to `extern` in `ipret.c`.

Result: 4.5 MB `coff-go32-exe` built with DJGPP GCC 5.2 + Watt-32. Verified in
the DOS VM: `LINKSJS.EXE` starts, and a local page with
`<script>document.write("<h1>JS-WORKS-123</h1>")</script>` renders the
written text — the interpreter executes. Static content and `document.write`
both appear, as expected for 2007-era semantics.

### Cure A — original assessment

The engine files ship complete (including pregenerated `javascr.c` /
`javascript.c`, so flex/bison are optional) in
`links-2.1pre28.tar.gz`, still downloadable.

Steps: copy the 18 files + `parser/`; add them to `links_SOURCES`/`links_LDADD`
in `Makefile.am`; keep `--enable-javascript`; regenerate the build system;
reconcile the ~764-line `jsint.c` drift (the hooks' side of the interface
changed, the engine's side did not).

Risk: the drift is precisely in the async-integration area; expect
`document.write`/reflow bugs to resurface. Estimate: days-to-weeks to a
"compiles and mostly runs" state, unbounded to "trustworthy".

Suitable for: an experiment build (`LINKSJS.EXE`) in this DOS agent
environment — the code is plain C, compiles under DJGPP, and the VM makes
crash recovery cheap.

### Cure B — Replace the engine, keep the hooks (recommended for real use)

Keep `jsint.c`'s queue/event integration but swap the interpreter for a small
maintained one — **MuJS** (ES5, ~40k lines C, permissive license, designed for
embedding, builds with DJGPP-class compilers and runs in tight memory).
Map the existing DOM bindings (`javascript.c` responsibilities) onto MuJS
natives; keep PCRE for regex.

This removes the "messy, buggy, unmaintained" interpreter entirely while
reusing Links' (still-maintained) hook layer. Estimate: a few weeks for a
useful subset (no JS == nothing; onload/onclick/document.write covers most
legacy-web needs).

### Cure C — Do nothing / use Links' HTML-only path

For the stated target sites (win3x.org era), JS is unnecessary — as our
browsing tests confirmed.

## 5. Verification notes

All file-presence claims were checked against the actual tarballs:
`links-2.30` (engine missing, hooks present), `links-2.18` (already missing —
removal predates it), `links-2.1pre28` (complete engine, last release with
it). Quote in §3 is from `links-2.18/ChangeLog` (identical text in 2.30's).

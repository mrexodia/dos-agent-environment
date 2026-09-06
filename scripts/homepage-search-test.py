#!/usr/bin/env python3
"""Integration test: LIVE belastingdienst.nl homepage search.

Loads https://www.belastingdienst.nl/, waits for the page + all its
scripts to settle (TCG is slow; the autosuggest/timers keep the page
busy for a long time), walks 6 links down to the search input (inline
edit - no Enter), types the query, submits via the 'zoek' button and
verifies the injected results page (Zoekresultaten ... gevonden).

Keyboard model notes:
- Links text fields are edited inline: cursor on field + type.
- The first chars typed before the cursor is really on the field (or
  while page JS is still running) are swallowed/executed as commands:
  we probe with a sacrificial 'z' and verify it landed.
"""
import sys, time
sys.path.insert(0, ".")
from harness.dosvm import DosVM
from harness.collect import extract_file

QUERY = sys.argv[1] if len(sys.argv) > 1 else "IB 2026"

vm = DosVM.start(run_id="bldhome", timeout=120.0)
rc = 2
try:
    vm.wait_for_prompt(timeout=60.0)
    vm.type("C:\\BIN\\LINKSQJS.EXE https://www.belastingdienst.nl/\r")
    for _ in range(90):
        time.sleep(1)
        if "Belastingdienst" in vm.screen_text():
            break
    # let ALL page scripts settle: typing into the field while the page
    # JS is still running is unreliable (field gets re-created)
    time.sleep(50)

    def field_line():
        for l in vm.screen_text().splitlines():
            if "Waar bent u naar op" in l:
                return l
        return ""

    def z_in_field(fl):
        return "z" in fl.replace("zoeken", "").replace("zoek?", "")

    field = False
    for step in range(1, 12):
        vm.key("down")
        time.sleep(2.5)
        vm.type("z", delay=0.5)
        time.sleep(2.5)
        fl = field_line()
        if z_in_field(fl):
            field = True
            print(f"[field] reached after {step} downs")
            break
        vm.key("backspace")
        time.sleep(0.6)
    if not field:
        print("[FAIL] search field not reachable")
        raise SystemExit(2)

    vm.key("backspace")
    time.sleep(1.0)
    for attempt in range(6):
        vm.type(QUERY, delay=0.3)
        time.sleep(3.0)
        if QUERY.replace(" ", "") in field_line().replace(" ", ""):
            break
        for _ in range(40):
            vm.key("backspace")
        time.sleep(1.5)
    print("[typed]", field_line().strip()[:70])

    vm.key("down")            # to the 'zoek' submit button
    time.sleep(2.5)
    vm.key("enter")           # SUBMIT
    for _ in range(120):
        time.sleep(1)
        t = vm.screen_text()
        if "QJS-SEARCH-END" in t or "Zoekresultaten" in t or "Zoekfout" in t:
            break
    txt = vm.screen_text()
    lines = [l.rstrip() for l in txt.splitlines() if l.strip()]
    print("=== screen ===")
    for l in lines[:20]:
        print("  ", l[:78])
    print("HAS-RESULTS:", "Zoekresultaten" in txt)
    rc = 0 if "Zoekresultaten" in txt else 1
finally:
    vm.stop(force=True)
    try:
        p = extract_file(vm, "C:\\SOCKSTAT.LOG", "/tmp/bldhome.log")
        for l in open("/tmp/bldhome.log").read().splitlines():
            if "FORMSUBMIT" in l or "FETCH" in l:
                print("LOG:", l)
    except Exception as e:
        print("nolog", e)
raise SystemExit(rc)

#!/usr/bin/env python3
"""Form-submit -> visible search results test (fixture page, local server,
real vinden.belastingdienst.nl API from the guest)."""
import sys, time
sys.path.insert(0, ".")
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from functools import partial
import threading
from harness.dosvm import DosVM
from harness.collect import extract_file

QUERY = sys.argv[1] if len(sys.argv) > 1 else "IB 2026"

handler = partial(SimpleHTTPRequestHandler, directory="apps/links/webroot")
srv = ThreadingHTTPServer(("0.0.0.0", 8080), handler)
threading.Thread(target=srv.serve_forever, daemon=True).start()
vm = DosVM.start(run_id="formtest", timeout=120.0)
rc = 2
try:
    vm.wait_for_prompt(timeout=60.0)
    vm.type("C:\\BIN\\LINKSQJS.EXE http://10.0.2.2:8080/formtest.html\r")
    for _ in range(40):
        time.sleep(1)
        if "Form submit test" in vm.screen_text():
            break
    time.sleep(4)
    # Links text fields are edited INLINE: cursor on field + type directly.
    # Chars sent before the field is focused trigger random commands, so we
    # verify + retry: type, check the field line, backspace-clear and repeat.
    vm.key("down")
    time.sleep(7.0)

    def field_line():
        for l in vm.screen_text().splitlines():
            if "___" in l or "Zoek ]" in l:
                return l
        return ""

    # the first chars typed at field-activation are consumed by Links:
    # send a sacrificial char, remove it, then the real query
    for attempt in range(8):
        vm.type("z", delay=0.5)
        time.sleep(3.0)
        if "z" in field_line():
            break
    vm.key("backspace")
    time.sleep(1.5)
    for attempt in range(6):
        vm.type(QUERY, delay=0.3)
        time.sleep(2.5)
        if QUERY.replace(" ", "") in field_line().replace(" ", ""):
            break
        for _ in range(40):
            vm.key("backspace")
        time.sleep(1.5)
    print("field:", field_line().strip()[:60])

    vm.key("down")            # leave the field to the next control
    time.sleep(2.0)
    vm.key("enter")           # SUBMIT
    for _ in range(120):
        time.sleep(1)
        t = vm.screen_text()
        if "QJS-SEARCH-END" in t or "Zoekfout" in t:
            break
    txt = vm.screen_text()
    lines = [l.rstrip() for l in txt.splitlines() if l.strip()]
    print("=== screen ===")
    for l in lines[:28]:
        print("  ", l[:78])
    print("HAS-RESULTS:", "Zoekresultaten" in txt)
    print("SEARCH-END:", "QJS-SEARCH-END" in txt)
    rc = 0 if ("QJS-SEARCH-END" in txt or "Zoekresultaten" in txt) else 1
finally:
    vm.stop(force=True)
    srv.shutdown()
    try:
        p = extract_file(vm, "C:\\SOCKSTAT.LOG", "/tmp/formtest-s.log")
        hits = [l for l in open("/tmp/formtest-s.log").read().splitlines()
                if "FORMSUBMIT" in l or "FETCH" in l]
        for h in hits:
            print("LOG:", h)
    except Exception as e:
        print("nolog", e)
raise SystemExit(rc)

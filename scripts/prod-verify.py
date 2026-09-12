#!/usr/bin/env python3
"""Production binary verification suite: conformance + render bridge + fetch."""
import sys, time
sys.path.insert(0, "/home/deomsh/DOSCTTY")
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from functools import partial
import threading
from harness.dosvm import DosVM

handler = partial(SimpleHTTPRequestHandler, directory="/home/deomsh/DOSCTTY/apps/links/webroot")
srv = ThreadingHTTPServer(("0.0.0.0", 8080), handler)
threading.Thread(target=srv.serve_forever, daemon=True).start()
vm = DosVM.start(run_id="prodtest", timeout=120.0)
try:
    vm.wait_for_prompt(timeout=60.0)
    vm.type("C:\\BIN\\LINKSQJS.EXE http://10.0.2.2:8080/jstest.html\r")
    for _ in range(60):
        time.sleep(1)
        if "JS-TEST-END" in vm.screen_text(): break
    print("conformance:", vm.screen_text().count("RESULT PASS"))
    vm.key("esc"); time.sleep(1); vm.type("q"); time.sleep(1); vm.key("enter"); time.sleep(3)
    vm.type("C:\\BIN\\LINKSQJS.EXE http://10.0.2.2:8080/rootspa.html\r")
    for _ in range(60):
        time.sleep(1)
        if "ROOT-SPA-END" in vm.screen_text(): break
    print("rootspa:", "ROOT-SPA-END" in vm.screen_text())
    vm.key("esc"); time.sleep(1); vm.type("q"); time.sleep(1); vm.key("enter"); time.sleep(3)
    vm.type("C:\\BIN\\LINKSQJS.EXE http://10.0.2.2:8080/fetchtest.html\r")
    for _ in range(40):
        time.sleep(1)
        if "FETCH-TEST-END" in vm.screen_text(): break
    t = vm.screen_text()
    print("fetch json:", "RESULT PASS json" in t)
    print("fetch status:", "RESULT PASS status" in t)
finally:
    vm.stop(force=True); srv.shutdown()

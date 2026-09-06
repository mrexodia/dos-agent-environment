#!/usr/bin/env python3
"""fetch() integration test: load fetchtest.html from the local fixture
server in LINKSQJS and scrape RESULT lines."""
import sys, time
sys.path.insert(0, ".")
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from functools import partial
import threading
from harness.dosvm import DosVM
from harness.collect import extract_file
from pathlib import Path

handler = partial(SimpleHTTPRequestHandler, directory="apps/links/webroot")
srv = ThreadingHTTPServer(("0.0.0.0", 8080), handler)
threading.Thread(target=srv.serve_forever, daemon=True).start()
vm = DosVM.start(run_id="fetchtest", timeout=90.0)
try:
    vm.wait_for_prompt(timeout=60.0)
    vm.type("C:\\BIN\\LINKSQJS.EXE http://10.0.2.2:8080/fetchtest.html\r")
    text = ""
    for _ in range(40):
        time.sleep(1)
        text = vm.screen_text()
        if "FETCH-TEST-END" in text or "signal" in text or "fault" in text.lower():
            break
    print(text[-800:])
finally:
    vm.stop(force=True)
    srv.shutdown()
    try:
        p = extract_file(vm, "C:\\JSERROR.LOG", "build/runs/fetchtest/JSERROR.LOG")
        print("JSERROR:", Path(p).read_text(errors='replace')[:800])
    except Exception:
        print("no JSERROR")

#!/usr/bin/env python3
"""Multi-page heap-stability test: browse several HTTPS pages sequentially
in ONE LINKSTLS session (with GC discipline active) and verify no crash."""
import sys
import time
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import threading

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from harness.dosvm import DosVM

ROOT = Path(__file__).resolve().parents[1]


class Q(SimpleHTTPRequestHandler):
    def log_message(self, *a):
        pass


srv = ThreadingHTTPServer(("0.0.0.0", 8080), partial(Q, directory=str(ROOT / "apps/links/webroot")))
threading.Thread(target=srv.serve_forever, daemon=True).start()

PAGES = [
    ("http://10.0.2.2:8080/jstest.html", "es5.json"),   # heavy JS churn
    ("http://10.0.2.2:8080/jstest.html", "es5.json"),   # again
    ("https://watlersfiles.netlify.app/", "HDADRV9Q"),     # TLS 1.3
    ("http://10.0.2.2:8080/jstest.html", "es5.json"),   # JS after TLS
]

vm = DosVM.start(run_id="multibrowse", timeout=90.0)
try:
    vm.wait_for_prompt(timeout=60.0)
    time.sleep(2)
    vm.type("C:\\BIN\\LINKSTLS.EXE http://10.0.2.2:8080/jstest.html" + chr(13))
    time.sleep(15)
    results = []
    for url, marker in PAGES:
        vm.key("g")
        time.sleep(2)
        vm.key("CTRL_U")
        time.sleep(0.3)
        vm.type(url, delay=0.06)
        time.sleep(0.3)
        vm.key("enter")
        ok = False
        for i in range(60):
            time.sleep(1)
            t = vm.screen_text()
            if marker in t:
                ok = True
                break
            if "signal" in t:
                break
        results.append(ok and "signal" not in t)
        print(f"{'PASS' if ok and 'signal' not in t else 'FAIL'} {url}")
        if "signal" in t:
            break
    alive = "signal" not in vm.screen_text()
    print("ALL:", all(results), "| alive:", alive)
    print("screenshot:", vm.screenshot())
finally:
    vm.stop(force=True)
    srv.shutdown()

#!/usr/bin/env python3
"""QEMU self-test for the hn bundle: boot, HDPMI, local fixture, collect
JSTRACE/JSERROR/SOCKSTAT. Designed for agent-driven iteration."""
import sys, time, os
sys.path.insert(0, ".")
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from functools import partial
import threading
from harness.dosvm import DosVM
from harness.collect import extract_file

os.environ.setdefault("DOSCTL_QEMU_MEM", "512")
os.environ.setdefault("DOSCTL_QEMU_ACCEL", "tcg")

handler = partial(SimpleHTTPRequestHandler, directory="apps/links/webroot")
srv = ThreadingHTTPServer(("0.0.0.0", 8080), handler)
threading.Thread(target=srv.serve_forever, daemon=True).start()

vm = DosVM.start(run_id="hnloc", timeout=120.0)
try:
    for _ in range(300):
        time.sleep(1)
        t = vm.screen_text()
        if any(l.strip().startswith("C:\\>") for l in t.splitlines()):
            break
    time.sleep(3)
    vm.type("del C:\\SOCKSTAT.LOG\r"); time.sleep(1.5)
    vm.type("del C:\\JSTRACE.LOG\r"); time.sleep(1.5)
    vm.type("C:\\BIN\\HDPMI32.EXE\r"); time.sleep(8)
    vm.type("C:\\BIN\\LNKSQJSB.EXE http://10.0.2.2:8080/hnlocal.html\r")
    t0 = time.time()
    for _ in range(900):
        time.sleep(1)
        if (_ % 120) == 0:
            scr = [l.strip()[:50] for l in vm.screen_text().splitlines() if l.strip()]
            print(f"[{time.time()-t0:.0f}s]", scr[:2], flush=True)
finally:
    vm.stop(force=True)
    srv.shutdown()
    for n in ("JSTRACE.LOG", "JSERROR.LOG", "SOCKSTAT.LOG"):
        try:
            p = extract_file(vm, f"C:\\{n}", f"/tmp/hnloc-{n}")
            print(f"=== {n} collected")
        except Exception:
            print(n, "none")

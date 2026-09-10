#!/usr/bin/env python3
"""Local hn.algolia bundle test: serve the REAL 2.6MB main-bundle.js
from the local fixture server (no TLS, no slow network), boot the DOS
VM with HDPMI32 + LNKSQJSB, watch the QJS-MEM trail."""
import sys, time, os
sys.path.insert(0, ".")
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from functools import partial
import threading
from harness.dosvm import DosVM
from harness.collect import extract_file

handler = partial(SimpleHTTPRequestHandler, directory="apps/links/webroot")
srv = ThreadingHTTPServer(("0.0.0.0", 8080), handler)
threading.Thread(target=srv.serve_forever, daemon=True).start()
os.environ.setdefault("DOSCTL_QEMU_MEM", "1024")
os.environ.setdefault("DOSCTL_QEMU_ACCEL", "tcg")

vm = DosVM.start(run_id="hnloc", timeout=120.0)
try:
    # robust: wait until a real DOS prompt appears (DHCP finished)
    for _ in range(300):
        time.sleep(1)
        t = vm.screen_text()
        if any(l.strip().startswith("C:\\>") for l in t.splitlines()):
            break
    print("prompt reached")
    time.sleep(3)
    vm.type("del C:\\SOCKSTAT.LOG\r"); time.sleep(2)
    vm.type("C:\\BIN\\HDPMI32.EXE\r"); time.sleep(10)
    print("hdpmi started:", [l.strip()[:50] for l in vm.screen_text().splitlines() if "DPMI" in l or "dpmi" in l][:2])
    vm.type("C:\\BIN\\LNKSQJSB.EXE http://10.0.2.2:8080/hnlocal.html\r")
    t0 = time.time()
    for _ in range(3300):
        time.sleep(1)
        if _ % 120 == 0:
            scr = [l.strip()[:56] for l in vm.screen_text().splitlines() if l.strip()]
            print(f"[{time.time()-t0:.0f}s]", scr[:2])
finally:
    vm.stop(force=True)
    srv.shutdown()
    try:
        p = extract_file(vm, "C:\\SOCKSTAT.LOG", "/tmp/hnloc.log")
        mems = [l for l in open("/tmp/hnloc.log").read().splitlines()
                if "QJS-MEM" in l or "INTERRUPT" in l or "MOCK" in l]
        print(f"=== {len(mems)} relevant lines")
        for l in mems[-30:]:
            print("  ", l[:100])
    except Exception as e:
        print("nolog", e)

#!/usr/bin/env python3
"""Ultimate stress test: LINKSTLS (MuJS ES5 + wolfSSL TLS 1.3) on
https://hn.algolia.com - native HTTPS, no bridge. Types JS-E5 into the
React-powered search field and screenshots the result."""
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from harness.dosvm import DosVM

OUT = Path(__file__).resolve().parents[1] / "build/runs/ES5-algolia-native"
OUT.mkdir(parents=True, exist_ok=True)

vm = DosVM.start(run_id="algolia", timeout=90.0)
try:
    vm.wait_for_prompt(timeout=60.0)
    time.sleep(2)
    vm.type("C:\\BIN\\LINKSQJS.EXE https://hn.algolia.com/" + chr(13))
    stage1 = False
    for i in range(90):
        time.sleep(1)
        t = vm.screen_text()
        if "Hacker" in t or "Algolia" in t or "signal" in t or "Error" in t:
            stage1 = True
            break
    print("home loaded:", stage1, "| crash:", "signal" in t)
    for l in [x.rstrip() for x in t.splitlines() if x.strip()][:10]:
        print("|", l[:76])
    p = vm.screenshot(OUT / "01-qjs-home.png")
    print("shot:", p)

    # search: Links form navigation - press down to reach the search field
    # then type the phrase and submit
    for _ in range(4):
        vm.key("down")
        time.sleep(0.4)
    vm.type("mrexodia")
    time.sleep(1)
    vm.key("enter")
    time.sleep(20)
    t = vm.screen_text()
    for l in [x.rstrip() for x in t.splitlines() if x.strip()][:14]:
        print("|", l[:76])
    p = vm.screenshot(OUT / "02-qjs-search.png")
    print("shot:", p)
    print("RESULT:", "JS-E5" in t, "| crash:", "signal" in t)
finally:
    vm.stop(force=True)

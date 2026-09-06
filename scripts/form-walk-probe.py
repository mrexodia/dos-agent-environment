#!/usr/bin/env python3
"""Probe: walk down-links on the belastingdienst homepage and print the
Links status bar (current link) at each step, to map the keyboard path
onto the search input field and submit button."""
import sys, time
sys.path.insert(0, ".")
from harness.dosvm import DosVM

vm = DosVM.start(run_id="bldwalk", timeout=120.0)
try:
    vm.wait_for_prompt(timeout=60.0)
    vm.type("C:\\BIN\\LINKSQJS.EXE https://www.belastingdienst.nl/\r")
    for _ in range(90):
        time.sleep(1)
        if "Belastingdienst" in vm.screen_text():
            break
    time.sleep(3)
    prev = None
    for step in range(28):
        vm.key("down")
        time.sleep(0.5)
        lines = [l.rstrip() for l in vm.screen_text().splitlines() if l.strip()]
        cur = lines[-1] if lines else ""
        if cur != prev:
            print(f"{step:2}: {cur[:90]}")
            prev = cur
finally:
    vm.stop(force=True)

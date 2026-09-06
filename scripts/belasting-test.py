#!/usr/bin/env python3
"""Deterministic belastingdienst.nl search-bar test for LINKSQJS.EXE.

Uses the DosVM automation API (regex-synchronized screen waits, no blind
sleeps) instead of raw key-serial timing. Steps:

  1. Boot VM (TCG by default for long runs; set DOSCTL_QEMU_ACCEL=kvm to override).
  2. Launch LINKSQJS.EXE on https://www.belastingdienst.nl/
  3. Wait for render (page title / known text) or JS banner.
  4. Drive the search form: open "Open zoeken", type query, submit.
  5. Wait for results-page marker (zoeken + query text) with timeout.
  6. Collect C:\\JSERROR.LOG, C:\\SOCKSTAT.LOG, C:\\TLSGLUE.TXT post-mortem.

Usage: python3 scripts/belasting-test.py [query]
"""
import re
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from harness.collect import collect_file
from harness.dosvm import DosVM

ROOT = Path(__file__).resolve().parents[1]
URL = "https://www.belastingdienst.nl/"
BINARY = "C:\\BIN\\LINKSQJS.EXE"


def wait_any(vm, patterns, timeout, poll=1.0):
    """Poll screen text until any regex matches; return (pattern, text)."""
    deadline = time.monotonic() + timeout
    last = ""
    while time.monotonic() < deadline:
        last = vm.screen_text()
        for p in patterns:
            if re.search(p, last, re.IGNORECASE):
                return p, last
        time.sleep(poll)
    return None, last


def main() -> int:
    query = sys.argv[1] if len(sys.argv) > 1 else "IB 2026"
    vm = DosVM.start(run_id="bldtest", timeout=90.0)
    logs = {}
    vm_stopped = [False]

    def collect_logs(vm):
        out_logs = {}
        for name in ("JSERROR.LOG", "SOCKSTAT.LOG", "TLSGLUE.TXT"):
            try:
                out = ROOT / "build" / "runs" / "bldtest" / name
                out.parent.mkdir(parents=True, exist_ok=True)
                extract_file(vm, f"C:\\{name}", out)
                out_logs[name] = out.read_text(errors="replace")
                print(f"[collect] {name}: {len(out_logs[name])} bytes -> {out}")
            except Exception as e:
                print(f"[collect] {name}: FAILED ({e})")
        jserr = out_logs.get("JSERROR.LOG", "")
        if jserr.strip():
            print("--- JSERROR.LOG (first 40 lines) ---")
            for line in jserr.splitlines()[:40]:
                print("  ", line)
        return out_logs

    try:
        vm.wait_for_prompt(timeout=60.0)
        vm.type(f"{BINARY} {URL}\r")

        # Wait for either the rendered homepage or the JS-off banner.
        hit, text = wait_any(vm, [
            r"Javascript staat uit|Javascript activeren",   # JS still failing
            r"Belastingdienst",                              # page rendered
            r"Error|Unable to connect|kan niet",
        ], timeout=120.0)
        print(f"[homepage] trigger={hit!r}")
        for line in [l for l in text.splitlines() if l.strip()][:10]:
            print("  |", line[:78])

        if hit is None:
            print("[FAIL] homepage never rendered")
            return 2

        # Acceptance check: anti-clickjacking script must remove the banner.
        banner = bool(re.search(r"Javascript staat uit|Javascript activeren", text))
        print(f"[banner] 'Javascript staat uit' visible: {banner}")

        # Focus the search form field: it renders as
        #   "Waar bent u naar op zoek? ____ zoek"
        # In Links, arrow-down walks links/fields; Enter on a text field starts
        # editing. Walk down until the screen cursor is inside the field, then
        # type and submit.
        vm.type(query)
        time.sleep(0.5)
        vm.key("enter")

        # The results page is client-rendered via fetch() (vinden.belastingdienst.nl
        # api/v2/search) which we do not implement — expect the empty search page.
        hit3, text3 = wait_any(vm, [r"U bevindt zich hier.*Zoeken", r"resultaat"], timeout=60.0)
        print(f"[results] trigger={hit3!r}")
        for line in [l for l in text3.splitlines() if l.strip()][:15]:
            print("  |", line[:78])

        # Post-mortem: extract logs directly from the qcow2 after stopping
        # (mTCP NC collection needs the DOS prompt, unavailable while Links runs).
        for name in ("JSERROR.LOG", "SOCKSTAT.LOG", "TLSGLUE.TXT"):
            try:
                from harness.collect import extract_file
                out = ROOT / "build" / "runs" / "bldtest" / name
                out.parent.mkdir(parents=True, exist_ok=True)
                extract_file(vm, f"C:\\{name}", out)
                logs[name] = out.read_text(errors="replace")
                print(f"[collect] {name}: {len(logs[name])} bytes -> {out}")
            except Exception as e:
                print(f"[collect] {name}: FAILED ({e})")

        jserr = logs.get("JSERROR.LOG", "")
        if jserr.strip():
            print("--- JSERROR.LOG (first 40 lines) ---")
            for line in jserr.splitlines()[:40]:
                print("  ", line)
        return (0 if not banner else 1) if hit else 2
    finally:
        if not vm_stopped[0]:
            vm.stop(force=True)
        if not logs:
            collect_logs(vm)


if __name__ == "__main__":
    raise SystemExit(main())

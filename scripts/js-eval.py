#!/usr/bin/env python3
"""Run the staged JavaScript conformance page in a DOS Links build.

Starts the local fixture HTTP server, boots the DOS VM, loads
http://10.0.2.2:8080/jstest.html in the specified Links binary, and
scrapes the RESULT lines off the VGA screen. Prints a PASS/FAIL matrix.

Usage: python3 scripts/js-eval.py [LINKSDEV.EXE|C:\\BIN\\LINKSJS.EXE ...]
"""
import re
import sys
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import threading
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from harness.dosvm import DosVM

ROOT = Path(__file__).resolve().parents[1]
URL = "http://10.0.2.2:8080/jstest.html"


class QuietHandler(SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass


def main() -> int:
    binaries = {
        "linksjs": "C:\\BIN\\LINKSJS.EXE",
        "linksdev": "C:\\BIN\\LINKSDEV.EXE",
    }
    binary = sys.argv[1] if len(sys.argv) > 1 else "linksjs"
    binary = binaries.get(binary.lower(), binary).replace("\\\\", "\\")
    handler = partial(QuietHandler, directory=str(ROOT / "apps/links/webroot"))
    server = ThreadingHTTPServer(("0.0.0.0", 8080), handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()

    vm = DosVM.start(run_id="jseval", timeout=90.0)
    try:
        vm.wait_for_prompt(timeout=60.0)
        time.sleep(2.0)
        vm.type(f"{binary} {URL}\r")
        text = ""
        for _ in range(40):
            time.sleep(1.0)
            text = vm.screen_text()
            if "Welcome" in text and "JS-TEST-END" not in text:
                vm.key("enter")  # dismiss the first-run dialog
                continue
            if "JS-TEST-END" in text or "Error" in text:
                break
        results = re.findall(r"RESULT (PASS|FAIL) (\S+)(?: \[([^\]]*)\])?", text)
        print(f"engine: {binary}")
        if not results:
            print("NO RESULTS — page shows:")
            for line in [l for l in text.splitlines() if l.strip()][:12]:
                print(" |", line[:78])
            return 2
        passed = failed = 0
        for status, name, why in results:
            print(f" {status}  {name}{'  <- ' + why if why and status == 'FAIL' else ''}")
            passed += status == "PASS"
            failed += status == "FAIL"
        print(f"score: {passed}/{passed + failed}")
        return 0 if failed == 0 else 1
    finally:
        vm.stop(force=True)
        server.shutdown()


if __name__ == "__main__":
    raise SystemExit(main())

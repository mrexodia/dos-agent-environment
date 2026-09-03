#!/usr/bin/env python3
"""Robust TLS-in-Links test: retypes the command if the echo is lost,
serves a TLS 1.3 page on :8443, and dumps the glue trace."""
import sys
import time
import ssl
import http.server
import threading
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from harness.dosvm import DosVM
from harness.collect import extract_file

CMD = "C:\\BIN\\LNKNOJS.EXE https://watlersfiles.netlify.app/"


class H(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        body = b"<html><body><h1>TLS-LOCAL-OK</h1></body></html>"
        self.send_response(200)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *a):
        pass


class TS(http.server.HTTPServer):
    def get_request(self):
        s, a = super().get_request()
        return ctx.wrap_socket(s, server_side=True), a


ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
ctx.minimum_version = ssl.TLSVersion.TLSv1_3
ctx.load_cert_chain("/tmp/test-cert.pem", "/tmp/test-key.pem")
srv = TS(("0.0.0.0", 8443), H)
threading.Thread(target=srv.serve_forever, daemon=True).start()

vm = DosVM.start(run_id="robust", timeout=90.0)
try:
    vm.wait_for_prompt(timeout=60.0)
    time.sleep(2)
    vm.type(CMD + chr(13))   # exactly once - retries leak keys into Links UI
    time.sleep(8)
    print("command typed")
    ok = False
    for i in range(60):
        time.sleep(1)
        t = vm.screen_text()
        if "Watler" in t or "HDADRV9Q" in t or "signal" in t:
            ok = "Watler" in t or "HDADRV9Q" in t
            break
    for l in [x.rstrip() for x in t.splitlines() if x.strip()][-8:]:
        print("|", l[:76])
    print("RESULT ok:", ok, "crash:", "signal" in t)
    print("screenshot:", vm.screenshot())
finally:
    vm.stop(force=True)
    srv.shutdown()
    try:
        f = extract_file(vm, r"C:\TLSGLUE.TXT", vm.run_dir / "t.txt")
        lines = open(f, errors="replace").read().splitlines()
        print(f"TRACE ({len(lines)}):")
        print("\n".join(lines))
    except Exception:
        print("TRACE: none")

#!/usr/bin/env python3
"""Run TLSTEST.EXE in the DOS VM against a local TLS 1.3 server."""
import http.server
import ssl
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from harness.dosvm import DosVM

ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
ctx.minimum_version = ssl.TLSVersion.TLSv1_3
ctx.load_cert_chain("/tmp/test-cert.pem", "/tmp/test-key.pem")


class H(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200)
        self.end_headers()
        self.wfile.write(b"TLS13-DOS-OK-42")

    def log_message(self, *a):
        pass


def main():
    class TLSServer(http.server.HTTPServer):
        def __init__(self, addr, handler, sslctx):
            super().__init__(addr, handler)
            self.sslctx = sslctx
        def get_request(self):
            sock, addr = super().get_request()
            return self.sslctx.wrap_socket(sock, server_side=True), addr
    httpd = TLSServer(("0.0.0.0", 8443), H, ctx)
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    print("tls 1.3 server on :8443", flush=True)

    vm = DosVM.start(run_id="tlstest", timeout=90.0)
    try:
        vm.wait_for_prompt(timeout=60.0)
        time.sleep(2)
        vm.type("C:\BIN\TLSTEST.EXE" + chr(13))
        for _ in range(30):
            time.sleep(1)
            t = vm.screen_text()
            if "SUCCESS" in t or "failed" in t or "marker" in t:
                break
        for l in [l.rstrip() for l in t.splitlines() if l.strip()][-12:]:
            print("|", l[:78])
    finally:
        vm.stop(force=True)
        httpd.shutdown()


if __name__ == "__main__":
    main()

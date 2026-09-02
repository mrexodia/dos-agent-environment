#!/usr/bin/env python3
"""Stress test: LINKSES5 (MuJS ES5 engine) against hn.algolia.com.

1. Direct https://hn.algolia.com/ — expected: 'SSL not supported' error
   (this build has no TLS), screenshot 01.
2. Through the local TLS bridge (http://10.0.2.2:8080/a/) — the React
   application under an ES5 engine, screenshot 02.
3. Server-side query ?q=JS-E5 through the bridge, screenshot 03.
Screenshots land in build/runs/ES5-algolia.com/.
"""
import ssl
import sys
import threading
import time
import urllib.request
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from harness.dosvm import DosVM

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/runs/ES5-algolia.com"
UPSTREAM = "https://hn.algolia.com/"


class Bridge(SimpleHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def do_GET(self):
        path = self.path
        if path.startswith("/a/"):
            path = path[2:]
        elif path == "/a":
            path = "/"
        url = UPSTREAM + path.lstrip("/")
        try:
            ctx = ssl.create_default_context()
            req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0 (DOS Links stress test)"})
            with urllib.request.urlopen(req, timeout=30, context=ctx) as r:
                body = r.read()
                ctype = r.headers.get("Content-Type", "application/octet-stream")
        except Exception as e:
            body = f"<html><body><h1>BRIDGE ERROR</h1><pre>{e}</pre></body></html>".encode()
            ctype = "text/html"
        if ctype.startswith("text/html"):
            head = b'<base href="/a/">'
            body = body.replace(b"</title>", b"</title>" + head, 1)
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def snap(vm, name):
    p = vm.screenshot(OUT / name)
    print(f"saved {name}: {p.stat().st_size} bytes")


def load(vm, url, wait=20, marker=None):
    vm.key("g")
    time.sleep(2)
    vm.key("CTRL_U")
    time.sleep(0.3)
    vm.type(url, delay=0.06)
    time.sleep(0.3)
    vm.key("enter")
    for _ in range(wait):
        time.sleep(1)
        t = vm.screen_text()
        if marker and marker in t:
            break
        if "Error loading" in t:
            break


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    server = ThreadingHTTPServer(("0.0.0.0", 8080), Bridge)
    threading.Thread(target=server.serve_forever, daemon=True).start()

    vm = DosVM.start(run_id="es5algolia", timeout=90.0)
    try:
        vm.wait_for_prompt(timeout=60.0)
        time.sleep(2)
        vm.type(r"C:\BIN\LINKSES5.EXE http://10.0.2.2:8080/a/\r")
        time.sleep(8)
        if "Welcome" in vm.screen_text():
            vm.key("enter")
            time.sleep(2)

        # 1: direct HTTPS (expected SSL error)
        print("step 1: direct https://hn.algolia.com/")
        load(vm, "https://hn.algolia.com/", wait=15)
        snap(vm, "01-direct-https.png")

        # 2: bridged home page (React app under ES5)
        print("step 2: bridged http://10.0.2.2:8080/a/")
        load(vm, "http://10.0.2.2:8080/a/", wait=25)
        snap(vm, "02-bridge-home.png")
        txt = vm.screen_text()
        print("  screen marker:", [l.strip() for l in txt.splitlines() if "Algolia" in l or "Hacker" in l][:2])

        # 3: server-side search for JS-E5
        print("step 3: search ?q=JS-E5")
        load(vm, "http://10.0.2.2:8080/a/?q=JS-E5", wait=25)
        snap(vm, "03-search-js-e5.png")
        txt = vm.screen_text()
        for l in [l.rstrip() for l in txt.splitlines() if l.strip()][-14:]:
            print(" |", l[:78])
    finally:
        vm.stop(force=True)
        server.shutdown()


if __name__ == "__main__":
    main()

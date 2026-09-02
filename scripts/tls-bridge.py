#!/usr/bin/env python3
"""TLS bridge for the DOS Links ES5 stress test.

Serves http://HOST:8080/a/<path>  ->  https://hn.algolia.com/<path>
(fetching on the host, rewriting nothing except injecting a <base> tag
on HTML responses so relative resources also flow through the bridge).
"""
import sys
import urllib.request
import ssl
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import threading

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
            if b"<base" not in body:
                body = body.replace(b"</head>", head + b"</head>", 1)
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def main():
    server = ThreadingHTTPServer(("0.0.0.0", 8080), Bridge)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    print("bridge ready on :8080", flush=True)
    import time
    try:
        while True:
            time.sleep(3600)
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()

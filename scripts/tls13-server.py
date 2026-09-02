#!/usr/bin/env python3
"""TLS 1.3 test server on :8443 for the DOS wolfSSL client test."""
import http.server
import ssl

ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
ctx.minimum_version = ssl.TLSVersion.TLSv1_3
ctx.load_cert_chain('/tmp/test-cert.pem', '/tmp/test-key.pem')


class H(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200)
        self.end_headers()
        self.wfile.write(b'TLS13-DOS-OK-42')

    def log_message(self, *a):
        pass


srv = http.server.HTTPServer(('0.0.0.0', 8443), H)
httpd = ctx.wrap_socket(srv, server_side=True)
print('tls 1.3 server on :8443', flush=True)
httpd.serve_forever()

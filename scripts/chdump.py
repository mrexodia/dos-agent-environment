#!/usr/bin/env python3
"""Raw TLS ClientHello dumper: accepts, reads the first flight, prints a
hexdump of the record header + version + first bytes, then closes."""
import socket
import sys
import threading
import time

sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1]))
from harness.dosvm import DosVM


def dump_server(port, out, timeout=25):
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("0.0.0.0", port))
    s.listen(1)
    s.settimeout(timeout)
    try:
        c, a = s.accept()
        c.settimeout(5)
        data = c.recv(4096)
        rec_type = data[0]
        version = (data[1], data[2])
        # handshake version lives at offset 4+4..4+5 for clienthello
        hs_ver = (data[8], data[9]) if len(data) > 9 else None
        out.append(f"type={rec_type} recver={version[0]:02x}{version[1]:02x} hsver={hs_ver[0]:02x}{hs_ver[1]:02x} len={len(data)}")
        out.append(data[:80].hex())
        c.close()
    except Exception as e:
        out.append(f"server: {e}")
    finally:
        s.close()


def main():
    binary = sys.argv[1] if len(sys.argv) > 1 else "TLSTEST"
    out = []
    t = threading.Thread(target=dump_server, args=(8443, out))
    t.start()
    time.sleep(0.5)

    vm = DosVM.start(run_id="chd" + binary[:4], timeout=90.0)
    try:
        vm.wait_for_prompt(timeout=60.0)
        time.sleep(2)
        url = "https://10.0.2.2:8443/"
        if binary == "TLSTEST":
            pass  # tlstest hardcodes the port
        else:
            url = f"https://10.0.2.2:8443/"
        cmd = {"TLSTEST": "C:\\BIN\\TLSTEST.EXE",
               "LNKNOJS": f"C:\\BIN\\LNKNOJS.EXE {url}"}[binary]
        vm.type(cmd + chr(13))
        for _ in range(30):
            time.sleep(1)
            if "signal" in vm.screen_text() or "connect" in vm.screen_text().lower():
                break
    finally:
        vm.stop(force=True)
    t.join(timeout=30)
    print(f"=== {binary} ClientHello ===")
    for line in out:
        print(line)


if __name__ == "__main__":
    main()

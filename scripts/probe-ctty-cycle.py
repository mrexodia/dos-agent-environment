#!/usr/bin/env python3
"""Stress repeated CTTY COM1 <-> CON toggles in a single boot."""
import re, secrets, sys, time
sys.path.insert(0, '.')
from harness.dosvm import DosVM


def recv_until(sock, regex, timeout):
    end = time.monotonic() + timeout
    data = bytearray()
    while time.monotonic() < end:
        sock.settimeout(0.25)
        try:
            c = sock.recv(4096)
        except Exception:
            continue
        data.extend(c)
        if regex.search(data):
            return bytes(data)
    return None


vm = DosVM.start(run_id="cttystress", timeout=90.0)
try:
    vm.wait_for_prompt(timeout=60.0)
    token = f"DOSCTL{secrets.token_hex(4).upper()}"
    vm.type(f"PROMPT {token}$P$G\r")
    vm.wait_for_prompt(timeout=5.0, token=token)
    prompt = re.compile(re.escape(token.encode()) + rb"[A-Z]:\\[^>\r\n]*>")
    for i in range(20):
        s = vm._connect_serial()
        vm.type("CTTY COM1\r")
        if recv_until(s, prompt, 25.0) is None:
            print(f"cycle {i}: CTTY COM1 handshake dead")
            s.settimeout(5.0)
            s.sendall(b"\r")
            try:
                print("  CR echo:", s.recv(4096))
            except Exception as e:
                print("  CR echo: nothing", type(e).__name__)
            with vm.qmp() as q:
                print("pic0:", [l for l in q.hmp("info pic").splitlines() if l.startswith("pic")])
            break
        s.sendall(b"ECHO hi\r")
        if recv_until(s, prompt, 25.0) is None:
            print(f"cycle {i}: serial exec dead after CTTY")
            with vm.qmp() as q:
                print("pic0:", [l for l in q.hmp("info pic").splitlines() if l.startswith("pic")])
            break
        s.close()
        vm.type("CTTY CON\r")
        vm.wait_for_prompt(timeout=25.0, token=token)
        print(f"cycle {i}: OK")
    else:
        print("all 20 cycles OK")
finally:
    vm.stop(force=True)

#!/usr/bin/env python3
"""Wedge test with an ISA NE2000 NIC instead of PCNet.

Boots with -nic user,model=ne2k_isa, loads the Crynwr NE2000 packet driver
manually (C:\\DRIVERS\\NE2000.COM 0x60 9 0x300), runs DHCP, then repeats the
PROMPT/CTTY/PING sequence that wedges with the PCNet packet driver.
"""
import os
import re
import secrets
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from harness.dosvm import DosVM

os.environ.setdefault("DOSCTL_QEMU_NIC", "user,model=ne2k_isa,hostname=DOSBOX")


def receive_until(sock, regex, timeout):
    deadline = time.monotonic() + timeout
    data = bytearray()
    while time.monotonic() < deadline:
        sock.settimeout(0.25)
        try:
            chunk = sock.recv(4096)
        except Exception:
            continue
        if not chunk:
            raise RuntimeError("serial closed")
        data.extend(chunk)
        if regex.search(data):
            return bytes(data)
    return None


ok = hang = 0
for attempt in range(8):
    run_id = f"ne{attempt}"
    vm = DosVM.start(run_id=run_id, timeout=90.0)
    try:
        try:
            vm.wait_for_prompt(timeout=60.0)
            screen = vm.screen_text()
            if "10.0.2.15" not in screen:
                tail = screen.splitlines()[-12:]
                print(f"attempt {attempt}: DHCP failed via NE2000:\n" + "\n".join(tail))
                break
            print(f"attempt {attempt}: NE2000+DHCP OK")
            token = f"DOSCTL{secrets.token_hex(4).upper()}"
            vm.type(f"PROMPT {token}$P$G\r")
            vm.wait_for_prompt(timeout=5.0, token=token)
        except Exception as e:
            hang += 1
            print(f"attempt {attempt}: HANG at PROMPT stage — probing")
            print("  exception:", str(e).splitlines()[-6:])
            vm.run_dir.mkdir(exist_ok=True)
            with vm.qmp() as q:
                print(q.hmp("info pic"))
            break

        serial = vm._connect_serial()
        vm.type("CTTY COM1\r")
        prompt = re.compile(re.escape(token.encode()) + rb"[A-Z]:\\[^>\r\n]*>")
        if receive_until(serial, prompt, 5.0) is None:
            print(f"attempt {attempt}: CTTY handshake failed")
            serial.close()
            continue

        serial.sendall(b"PING 10.0.2.2\r")
        raw = receive_until(serial, prompt, 45.0)
        serial.close()
        if raw is not None:
            ok += 1
            print(f"attempt {attempt}: PING OK")
            vm.type("CTTY CON\r")
            vm.type("PROMPT $P$G\r")
            continue
        hang += 1
        print(f"attempt {attempt}: HANG — probing live guest")
        serial = vm._connect_serial()
        serial.settimeout(3.0)
        serial.sendall(b"\r")
        try:
            print(f"  after CR: {serial.recv(4096)!r}")
        except Exception as e:
            print(f"  after CR: nothing ({type(e).__name__})")
        serial.close()
        with vm.qmp() as q:
            print(q.hmp("info pic"))
            vm.run_dir.mkdir(exist_ok=True)
            q.execute("pmemsave", {"val": 0, "size": 0x110000,
                                   "filename": str(vm.run_dir / "hang.bin")})
        break
    finally:
        vm.stop(force=True)
print(f"result: ok={ok} hang={hang}")

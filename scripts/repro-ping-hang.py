#!/usr/bin/env python3
"""Reproduce the serial PING hang and dump guest state while still alive."""
import os
import re
import secrets
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from harness.dosvm import DosVM


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


def probe(vm, label):
    print(f"attempt probe ({label})")
    with vm.qmp() as q:
        for cmd in ("info irq", "info pic", "info registers", "info status"):
            try:
                print(f"--- {cmd}")
                print(q.hmp(cmd))
            except Exception as e:
                print(f"  failed: {e}")
        # two irq snapshots 2s apart to see if timer IRQ0 still ticks
        try:
            a = q.hmp("info irq")
            time.sleep(2.0)
            b = q.hmp("info irq")
            print("--- irq delta over 2s")
            print(a)
            print(b)
        except Exception as e:
            print("  irq delta failed", e)
        vm.run_dir.mkdir(exist_ok=True)
        out = vm.run_dir / "hang.bin"
        q.execute("pmemsave", {"val": 0, "size": 0x110000, "filename": str(out)})
        print("  pmemsave ->", out)


for attempt in range(8):
    run_id = os.environ.get("DOSCTL_RUN_ID", f"pr{attempt}") + f"-{attempt}"
    vm = DosVM.start(run_id=run_id, timeout=30.0)
    try:
        try:
            initial = vm.wait_for_prompt(timeout=10.0).text()
            token = f"DOSCTL{secrets.token_hex(4).upper()}"
            vm.type(f"PROMPT {token}$P$G\r")
            vm.wait_for_prompt(timeout=5.0, token=token, require_change_from=initial)
        except Exception as e:
            print(f"attempt {attempt}: HANG at PROMPT stage — probing live guest")
            print("  exception:", str(e).splitlines()[-6:])
            probe(vm, "prompt")
            break

        serial = vm._connect_serial()
        vm.type("CTTY COM1\r")
        prompt = re.compile(re.escape(token.encode()) + rb"[A-Z]:\\[^>\r\n]*>")
        if receive_until(serial, prompt, 5.0) is None:
            print(f"attempt {attempt}: CTTY handshake failed")
            serial.close()
            continue

        serial.sendall(b"PING 10.0.2.2\r")
        raw = receive_until(serial, prompt, 15.0)
        serial.close()
        if raw is not None:
            print(f"attempt {attempt}: OK")
            vm.type("CTTY CON\r")
            vm.type("PROMPT $P$G\r")
            continue
        print(f"attempt {attempt}: HANG — probing live guest")

        # Guest wedged mid-PING with console on COM1. Probe it.
        serial = vm._connect_serial()
        serial.settimeout(3.0)
        serial.sendall(b"\r")
        try:
            print(f"  after CR: {serial.recv(4096)!r}")
        except Exception as e:
            print(f"  after CR: nothing ({type(e).__name__})")
        serial.sendall(b"\x03")  # Ctrl-C
        try:
            print(f"  after Ctrl-C: {serial.recv(4096)!r}")
        except Exception as e:
            print(f"  after Ctrl-C: nothing ({type(e).__name__})")
        serial.close()

        probe(vm, "ping")
        break
    finally:
        vm.stop(force=True)

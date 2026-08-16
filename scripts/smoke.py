#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import os
import signal
import sys
import threading
import time
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from harness.collect import collect_file
from harness.dosvm import DosVM

ROOT = Path(__file__).resolve().parents[1]
BINARY_FIXTURE = bytes(
    [0x00, 0x01, 0x02, 0x0A, 0x0D, 0x1A, 0x7F, 0x80, 0xFE, 0xFF]
) + b"DOS_AGENT_BINARY\x00"


class QuietHandler(SimpleHTTPRequestHandler):
    def log_message(self, format: str, *args: object) -> None:
        pass


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _validate(vm: DosVM, fixture_url: str) -> None:
    version = vm.exec("VER")
    assert any(build in version for build in ("4.10.1998", "4.10.2222")), version

    vm.exec("ECHO DOS_AGENT_FILE> C:\\TMP\\SMOKE.TXT")
    contents = vm.exec("TYPE C:\\TMP\\SMOKE.TXT")
    assert "DOS_AGENT_FILE" in contents, contents

    hello = vm.exec("HELLO.COM")
    assert "DOS_AGENT_HELLO" in hello, hello

    assert "DOS_AGENT_BINARY_CREATED" in vm.exec("MAKEBIN.COM")
    binary = collect_file(vm, r"C:\TMP\BINARY.DAT", vm.run_dir / "binary.dat")
    assert binary.read_bytes() == BINARY_FIXTURE

    vm.exec(r"TYPE NUL>C:\TMP\EMPTY.DAT")
    empty = collect_file(vm, r"C:\TMP\EMPTY.DAT", vm.run_dir / "empty.dat")
    assert empty.read_bytes() == b""

    tcp_cfg = vm.exec_serial(r"TYPE C:\MTCP\TCP.CFG")
    assert "IPADDR 10.0.2.15" in tcp_cfg, tcp_cfg
    ping = vm.exec_serial("PING 10.0.2.2", timeout=20.0)
    assert "Replies lost: 0" in ping, ping
    vm.exec_serial(
        f"HTGET -quiet -o C:\\TMP\\NETWORK.TXT {fixture_url}/network.txt",
        timeout=20.0,
    )
    fetched = vm.exec_serial(r"TYPE C:\TMP\NETWORK.TXT")
    assert "DOS_AGENT_NETWORK_MARKER" in fetched, fetched

    screen = vm.screen()
    assert (screen.mode, screen.columns, screen.rows) == (3, 80, 25)
    image = vm.screenshot(vm.run_dir / "smoke.png")
    assert image.stat().st_size > 0
    print(f"acceptance checks passed in {vm.run_id}; screenshot: {image}")


def run_once() -> None:
    base = ROOT / "build/dos71.qcow2"
    base_hash = _sha256(base)
    handler = partial(QuietHandler, directory=str(ROOT / "apps/links/webroot"))
    server = ThreadingHTTPServer(("0.0.0.0", 8080), handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    fixture_url = "http://10.0.2.2:8080"
    try:
        vm = DosVM.start(timeout=30.0)
        try:
            _validate(vm, fixture_url)

            # Simulate an agent/tool crash. Only this run's overlay may change.
            pid = vm.pid()
            assert pid is not None
            os.killpg(pid, signal.SIGKILL)
            deadline = time.monotonic() + 5.0
            while vm.is_alive() and time.monotonic() < deadline:
                time.sleep(0.05)
            assert not vm.is_alive(), "QEMU did not terminate after SIGKILL"
            vm.stop(force=True)
        finally:
            if vm.is_alive():
                vm.stop(force=True)

        assert _sha256(base) == base_hash, "canonical qcow2 changed during a run"
        recovered = DosVM.start(timeout=30.0)
        try:
            _validate(recovered, fixture_url)
            recovered.exec(r"ECHO POST_MORTEM>C:\TMP\POST.TXT")
            recovered.stop(force=True)
            postmortem = collect_file(
                recovered,
                r"C:\TMP\POST.TXT",
                recovered.run_dir / "postmortem.txt",
            )
            assert b"POST_MORTEM" in postmortem.read_bytes()
            print(f"crash recovery and post-mortem extraction passed in {recovered.run_id}")
        finally:
            if recovered.is_alive():
                recovered.stop(force=True)
    finally:
        server.shutdown()
        server.server_close()
        thread.join(2.0)


if __name__ == "__main__":
    run_once()

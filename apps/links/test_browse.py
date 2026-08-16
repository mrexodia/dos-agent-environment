from __future__ import annotations

import os
import signal
import threading
import time
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

import pytest

from apps.links.driver import LinksDriver
from harness.dosvm import DosVM


@pytest.fixture
def fixture_server():
    root = Path(__file__).resolve().parents[2]
    handler = partial(
        SimpleHTTPRequestHandler,
        directory=str(root / "apps/links/webroot"),
    )
    server = ThreadingHTTPServer(("0.0.0.0", 8080), handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        yield "http://10.0.2.2:8080"
    finally:
        server.shutdown()
        server.server_close()
        thread.join(2.0)


@pytest.mark.skipif(
    os.environ.get("DOS_LINKS_INTEGRATION") != "1",
    reason="set DOS_LINKS_INTEGRATION=1 after make runtime to run",
)
def test_links_vertical_slice_and_crash_recovery(fixture_server):
    root = Path(__file__).resolve().parents[2]
    assert (root / "inputs/links-2.30.exe").exists()
    assert (root / "payload/BIN/LINKS.EXE").exists(), (
        "run apps/links/build.sh and make runtime before the integration test"
    )

    vm = DosVM.start()
    try:
        driver = LinksDriver(vm)
        driver.launch(f"{fixture_server}/index.html", "DOS_AGENT_LINKS_MARKER")

        # The first selected control is the first link; assert behavior rather
        # than pinning an undocumented Links color-attribute value.
        driver.follow_selected("DOS_AGENT_SECOND_PAGE")
        driver.goto(f"{fixture_server}/index.html", "DOS_AGENT_LINKS_MARKER")
        driver.fill_and_submit(
            "Agent Name",
            steps_to_field=1,
            steps_to_submit=1,
            marker="DOS_AGENT_FORM_SUBMITTED",
        )

        driver.goto(f"{fixture_server}/long.html", "DOS_AGENT_LONG_TOP")
        full_page = driver.read_full_page()
        assert "DOS_AGENT_LONG_TOP" in full_page
        assert "DOS_AGENT_LONG_BOTTOM" in full_page
        assert vm.screen().columns == 80
        screenshot = vm.screenshot(vm.run_dir / "links-active.png")
        assert screenshot.stat().st_size > 0

        # Kill QEMU without a graceful shutdown and prove a fresh overlay can
        # launch the browser again.
        pid = vm.pid()
        assert pid is not None
        os.killpg(pid, signal.SIGKILL)
        deadline = time.monotonic() + 5.0
        while vm.is_alive() and time.monotonic() < deadline:
            time.sleep(0.05)
        assert not vm.is_alive()
        vm.stop(force=True)
    finally:
        if vm.is_alive():
            vm.stop(force=True)

    recovered = DosVM.start()
    try:
        driver = LinksDriver(recovered)
        driver.launch(f"{fixture_server}/index.html", "DOS_AGENT_LINKS_MARKER")
        driver.quit()
    finally:
        recovered.stop(force=True)

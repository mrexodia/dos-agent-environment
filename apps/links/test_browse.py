from __future__ import annotations

import os
import threading
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

import pytest

from apps.links.driver import LinksDriver
from harness.dosvm import DosVM


@pytest.mark.skipif(
    os.environ.get("DOS_LINKS_INTEGRATION") != "1",
    reason="set DOS_LINKS_INTEGRATION=1 with mTCP and Links inputs to run",
)
def test_links_loads_local_marker():
    root = Path(__file__).resolve().parents[2]
    assert (root / "inputs/links-2.30.exe").exists()
    assert (root / "payload/BIN/LINKS.EXE").exists(), (
        "run apps/links/build.sh and make runtime before the integration test"
    )
    handler = lambda *args, **kwargs: SimpleHTTPRequestHandler(
        *args, directory=str(root / "apps/links/webroot"), **kwargs
    )
    server = ThreadingHTTPServer(("0.0.0.0", 8080), handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    vm = DosVM.start()
    try:
        driver = LinksDriver(vm)
        driver.launch("http://10.0.2.2:8080/index.html", "DOS_AGENT_LINKS_MARKER")
        assert "DOS_AGENT_LINKS_MARKER" in vm.screen_text()
        driver.quit()
    finally:
        vm.stop(force=True)
        server.shutdown()
        server.server_close()
        thread.join(2.0)

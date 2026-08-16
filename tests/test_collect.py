import threading

import pytest

from harness.collect import _safe_path, collect_file
from harness.dosvm import DosTimeout, DosVMError


def test_collection_accepts_plain_dos_paths():
    _safe_path(r"C:\TMP\RESULT.BIN")
    _safe_path("RESULT.TXT")


@pytest.mark.parametrize(
    "path",
    ["", "HAS SPACE.TXT", "*.TXT", "X>Y", "X&Y", "%TEMP%", "X?.TXT"],
)
def test_collection_rejects_command_interpreter_metacharacters(path):
    with pytest.raises(DosVMError, match="unsafe DOS path"):
        _safe_path(path)


def test_collection_timeout_removes_partial_file_and_listener_thread(tmp_path):
    class FakeVM:
        run_id = "timeout-test"

        def is_alive(self):
            return True

        def exec(self, command, timeout):
            return ""

        def screen_text(self):
            return "C:\\>"

        def timeout_error(self, operation, timeout, last_screen, detail=None):
            return DosTimeout(f"{operation}: {detail or ''}")

    destination = tmp_path / "result.bin"
    with pytest.raises(DosTimeout, match="mTCP Netcat"):
        collect_file(FakeVM(), r"C:\TMP\MISSING.BIN", destination, timeout=0.05)
    assert not destination.exists()
    assert not destination.with_suffix(".bin.part").exists()
    assert not any(thread.name == "dos-collect" and thread.is_alive() for thread in threading.enumerate())

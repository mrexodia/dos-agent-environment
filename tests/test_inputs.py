from pathlib import Path


def test_guest_text_files_are_crlf_only():
    root = Path(__file__).resolve().parents[1]
    files = [
        root / "guest/MSDOS.SYS",
        root / "guest/CONFIG.SYS",
        root / "guest/AUTOEXEC.BAT",
        root / "guest/BIN/SERIAL.BAT",
        root / "guest/MTCP/TCP.CFG",
    ]
    for path in files:
        data = path.read_bytes()
        assert b"\n" in data
        assert b"\n" not in data.replace(b"\r\n", b"")

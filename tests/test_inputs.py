import hashlib
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


def test_every_declared_input_is_present_and_matches_approved_checksum():
    root = Path(__file__).resolve().parents[1]
    expected = {
        line.split()[1]: line.split()[0]
        for line in (root / "inputs/SHA256SUMS").read_text().splitlines()
        if line.strip()
    }
    required = {
        "Windows98_SE_No_Ramdrive.img",
        "pcntpk.com",
        "cwsdpmi.exe",
        "mTCP_2025-01-10_upx.zip",
        "links-2.30.exe",
    }
    assert set(expected) == required
    for name, digest in expected.items():
        path = root / "inputs" / name
        assert path.is_file(), name
        assert hashlib.sha256(path.read_bytes()).hexdigest() == digest


def test_links_payload_matches_immutable_input():
    root = Path(__file__).resolve().parents[1]
    assert (root / "payload/BIN/LINKS.EXE").read_bytes() == (
        root / "inputs/links-2.30.exe"
    ).read_bytes()

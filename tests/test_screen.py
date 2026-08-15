import re

from harness.screen import read_physical, read_text_screen


class FakeQMP:
    def __init__(self, memory):
        self.memory = memory

    def hmp(self, command, timeout=None):
        match = re.fullmatch(r"xp /(\d+)bx 0x([0-9a-f]+)", command)
        assert match
        count = int(match.group(1))
        address = int(match.group(2), 16)
        data = bytes(self.memory.get(address + offset, 0) for offset in range(count))
        lines = []
        for offset in range(0, count, 16):
            chunk = data[offset : offset + 16]
            lines.append(
                f"{address + offset:016x}: " + " ".join(f"0x{value:02x}" for value in chunk)
            )
        return "\n".join(lines)


def test_read_physical_parses_hmp_dump():
    qmp = FakeQMP({0x1000: 1, 0x1001: 0xAB, 0x1002: 0xFF})
    assert read_physical(qmp, 0x1000, 3) == b"\x01\xab\xff"


def test_text_screen_uses_bios_dimensions_and_cp437():
    memory = {}
    bda = bytearray(0x100)
    bda[0x49] = 3
    bda[0x4A:0x4C] = (80).to_bytes(2, "little")
    bda[0x4E:0x50] = (0).to_bytes(2, "little")
    bda[0x84] = 24
    for offset, value in enumerate(bda):
        memory[0x400 + offset] = value
    for index in range(80 * 25):
        memory[0xB8000 + index * 2] = 0x20
        memory[0xB8000 + index * 2 + 1] = 0x07
    memory[0xB8000] = ord("A")
    memory[0xB8001] = 0x1E
    memory[0xB8002] = 0xDB
    memory[0xB8004] = 0xC9

    screen = read_text_screen(FakeQMP(memory))
    assert (screen.columns, screen.rows) == (80, 25)
    assert screen.cells[0][0].character == "A"
    assert screen.cells[0][0].attribute == 0x1E
    assert screen.cells[0][1].character == "█"
    assert screen.ascii().splitlines()[0].startswith("A#+")

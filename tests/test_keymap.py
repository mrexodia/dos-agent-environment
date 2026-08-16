import string

import pytest

from harness.keymap import KeyMappingError, chord_for_character, chord_for_named_key


def test_all_printable_ascii_is_mapped():
    for character in string.printable:
        if character in "\x0b\x0c":
            continue
        assert chord_for_character(character)


def test_uppercase_uses_shift():
    assert chord_for_character("A") == ["shift", "a"]


def test_named_navigation_and_modifier_chords():
    assert chord_for_named_key("PGDN") == ["pgdn"]
    assert chord_for_named_key("CTRL_C") == ["ctrl", "c"]
    assert chord_for_named_key("shift+tab") == ["shift", "tab"]
    assert chord_for_named_key("SHIFT_END") == ["shift", "end"]


def test_unknown_unicode_is_rejected():
    with pytest.raises(KeyMappingError):
        chord_for_character("é")
    with pytest.raises(KeyMappingError):
        chord_for_named_key("HYPERDRIVE")

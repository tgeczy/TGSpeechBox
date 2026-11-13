"""The composition data the frontend carries for Android and Linux (#169).

src/frontend/unicode_compose_table.h is generated from the Unicode Character
Database by tools/gen_unicode_compose_table.py.  composeCanonical() in
src/frontend/utf8.cpp searches it by halving, so it has to be sorted, and it
is what the Linux and Android branch of normalizeNFKC calls.  What the
function does with it is tested in tests/cpp/test_unicode_compose.cpp.
"""
from __future__ import annotations

import importlib.util
import pathlib
import re
import unicodedata

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[1]
HEADER = ROOT / "src" / "frontend" / "unicode_compose_table.h"


def _generator():
    spec = importlib.util.spec_from_file_location(
        "gen_unicode_compose_table", ROOT / "tools" / "gen_unicode_compose_table.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def _header():
    return HEADER.read_text(encoding="utf-8").replace("\r\n", "\n")


def _rows(name):
    text = _header()
    body = text[text.index(f"{name}[] = {{"):]
    body = body[:body.index("};")]
    return [tuple(int(x, 0) for x in row.split(","))
            for row in re.findall(r"\{([0-9A-Fa-fx,]+)\}", body)]


def test_header_is_what_the_generator_writes():
    made_from = re.search(r"Unicode Character Database ([0-9.]+)", _header()).group(1)
    if made_from != unicodedata.unidata_version:
        pytest.skip(f"header made from Unicode {made_from}, this Python has {unicodedata.unidata_version}")
    assert _generator().render() == _header()


@pytest.mark.parametrize("name,key", [
    ("kPairs", lambda r: (r[0], r[1])),
    ("kMarks", lambda r: r[0]),
    ("kSingles", lambda r: r[0]),
])
def test_tables_are_sorted_with_no_duplicate_key(name, key):
    keys = [key(r) for r in _rows(name)]
    assert len(keys) > 20
    assert keys == sorted(set(keys))


def test_tables_hold_what_the_lookup_assumes():
    pairs, marks, singles = _rows("kPairs"), dict(_rows("kMarks")), _rows("kSingles")
    assert all(0 < c <= 0xFFFF for row in pairs + singles for c in row[:2])
    # Nothing below U+0300 is a combining mark or has another canonical form:
    # composeCanonical returns text with no such code point untouched.
    assert min(marks) == 0x300 and min(r[0] for r in singles) >= 0x300
    # The second of a pair is always a combining mark, the first never.
    assert all(second in marks and first not in marks for first, second, _ in pairs)
    # A composed letter appears once, so decomposing it has one answer.
    composed = [r[2] for r in pairs]
    assert len(composed) == len(set(composed))


@pytest.mark.parametrize("first,second,composed", [
    (0x65, 0x301, 0xE9),      # é
    (0x6F, 0x30B, 0x151),     # hu ő
    (0x6E, 0x303, 0xF1),      # es ñ
    (0xEA, 0x301, 0x1EBF),    # vi ế, from ê
    (0x3B1, 0x301, 0x3AC),    # el ά
    (0x438, 0x306, 0x439),    # ru й
])
def test_table_has_the_letters_the_packs_use(first, second, composed):
    assert (first, second, composed) in _rows("kPairs")


def test_linux_and_android_branch_composes():
    """normalizeNFKC's branch for platforms with no system normalizer was
    "return utf8;": it is composeCanonical now."""
    text = (ROOT / "src" / "frontend" / "utf8.cpp").read_text(encoding="utf-8")
    branch = text[text.index("#elif defined(__APPLE__)", text.index("NFKC normalization")):]
    branch = branch[branch.index("#else"):branch.index("#endif")]
    assert re.search(r"std::string normalizeNFKC\(const std::string& utf8\) \{ return composeCanonical\(utf8\); \}",
                     branch)

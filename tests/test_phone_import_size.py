"""The phone apps read a picked file through their bounded reader, and nowhere else.

The Android and iOS apps import three kinds of text file (a language pack,
phoneme overrides, a dictionary).  Each read whatever was picked whole, with
no limit: the wrong file, a video say, ran the app out of memory.  On Android
that error is not an Exception, nothing caught it, and the process that died
is the one the screen reader's speech service lives in.  Found by checking
this repository against the audits of the maintainer's other speech
projects.

What the reader does is tested where it can run: ImportTextTest.kt, on the
JVM.  This file holds the call sites to it, on both platforms, from the
source: a new import written the old way fails here.  The Swift is not
compiled by any test in this repository.
"""
from __future__ import annotations

import pathlib
import re

REPO = pathlib.Path(__file__).resolve().parents[1]
ANDROID = REPO / "src" / "platforms" / "android" / "app" / "src" / "main" / "kotlin" / "com" / "tgspeechbox" / "tts"
IOS = REPO / "src" / "platforms" / "ios" / "host-app"


def _sources(folder, suffix):
    return {p.name: p.read_text(encoding="utf-8") for p in sorted(folder.glob("*" + suffix))}


def test_android_reads_every_picked_file_through_the_bounded_reader():
    sources = _sources(ANDROID, ".kt")
    opened = {name: len(re.findall(r"openInputStream\(", text)) for name, text in sources.items()}
    opened = {name: n for name, n in opened.items() if n}
    assert sum(opened.values()) >= 3, "the three imports were not found: has the code moved?"
    for name in opened:
        text = sources[name]
        # Each stream that is opened is handed to ImportText.read in the
        # same statement, and nothing reads a stream whole on its own.
        bounded = len(re.findall(r"openInputStream\([^)]*\)\?\.use\s*\{\s*ImportText\.read\(it\)\s*\}", text))
        assert bounded == opened[name], f"{name}: {opened[name]} picked files are opened, {bounded} go through ImportText.read"
        assert ".readText()" not in text and ".readBytes()" not in text, f"{name} reads a stream whole"


def test_android_has_one_limit():
    text = (ANDROID / "ImportText.kt").read_text(encoding="utf-8")
    assert len(re.findall(r"const val MAX_BYTES", text)) == 1


def test_ios_reads_every_picked_file_through_the_bounded_reader():
    sources = _sources(IOS, ".swift")
    whole = {name: len(re.findall(r"String\(contentsOf:", text)) for name, text in sources.items()}
    # The one place a file's text is read whole is inside the reader itself,
    # after it has looked at the size.
    assert {name: n for name, n in whole.items() if n} == {"TgsbEngine.swift": 1}
    engine = sources["TgsbEngine.swift"]
    reader = engine[engine.index("func tgsbReadImportText("):]
    reader = reader[:reader.index("\n}\n") + 3]
    assert reader.index("fileSizeKey") < reader.index("String(contentsOf:"), "the size is not looked at before the read"
    assert "tgsbImportMaxBytes" in reader
    calls = sum(len(re.findall(r"tgsbReadImportText\(url\)", text)) for text in sources.values())
    assert calls == 3, f"{calls} imports use the reader, 3 were expected (pack, phoneme overrides, dictionary)"


def test_the_two_apps_have_the_same_limit():
    kotlin = (ANDROID / "ImportText.kt").read_text(encoding="utf-8")
    swift = (IOS / "TgsbEngine.swift").read_text(encoding="utf-8")
    k = re.search(r"const val MAX_BYTES\s*=\s*(\d+)\s*\*\s*1024\s*\*\s*1024", kotlin)
    s = re.search(r"let tgsbImportMaxBytes\s*=\s*(\d+)\s*\*\s*1024\s*\*\s*1024", swift)
    assert k and s, "the limit is not written as a number of megabytes on one side"
    assert k.group(1) == s.group(1)


def test_the_limit_holds_the_largest_text_file_the_packs_ship():
    kotlin = (ANDROID / "ImportText.kt").read_text(encoding="utf-8")
    limit = int(re.search(r"const val MAX_BYTES\s*=\s*(\d+)\s*\*", kotlin).group(1)) * 1024 * 1024
    largest = max(p.stat().st_size for p in (REPO / "packs").rglob("*") if p.is_file())
    assert largest * 2 <= limit, f"the largest pack file is {largest} bytes: an export of it would not import again"

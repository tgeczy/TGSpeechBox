"""A one-letter word that is a clause of a longer text is a word (#158).

A lone letter is read by its name from the language's letter dictionary: "y"
is "i griega" in Spanish, "é" is "é agudo" in Portuguese.  #122 extended that
to a letter next to punctuation ("r?", "ó."), for a line or a typed run that
is one letter and a symbol.  But every host splits its text into clauses and
prepares them one at a time, and a clause of one letter looks the same:
Portuguese "Sim. É. Claro." said "Sim. é agudo. Claro.", and Spanish "Y, sin
embargo, se fue." said "i griega, sin embargo, se fue."  A native Portuguese
speaker confirmed the "É" case.

The host knows whether a clause is its whole text, and says so through
nvspFrontend_prepareClause.  A letter that is the whole text keeps its name.
"""
from __future__ import annotations

import pathlib
import re

import pytest

REPO = pathlib.Path(__file__).resolve().parent.parent


@pytest.fixture
def speak(frontend, pack_dir):
    """speak(lang, text): what each clause of `text` reaches the phonemizer
    as, done the way every host does it: split, then prepare each clause,
    saying whether it is the whole text."""
    handles = {}

    def _speak(lang, text):
        if lang not in handles:
            handles[lang] = frontend.create_handle(pack_dir)
            frontend.set_language(handles[lang], lang)
        clauses = frontend.split_clauses(text)
        whole = len(clauses) == 1
        return [frontend.prepare_clause(handles[lang], c, whole) for c in clauses]

    yield _speak
    for h in handles.values():
        frontend.destroy_handle(h)


@pytest.mark.parametrize("lang, text, expected", [
    ("pt-br", "É, eu sei.", ["É,", "eu sei."]),
    ("pt-br", "Sim. É. Claro.", ["Sim.", "É.", "Claro."]),
    ("pt-br", "Você vai? É.", ["Você vai?", "É."]),
    ("pt-br", "Sim, é", ["Sim,", "é"]),
    ("pt-br", "É (acho) verdade", ["É", "( acho )", "verdade"]),
    ("pt-br", "Vou à, digamos, praia.", ["Vou à,", "digamos,", "praia."]),
    ("pt", "Sim. É. Claro.", ["Sim.", "É.", "Claro."]),
    ("pt", "Ó, que bom!", ["Ó,", "que bom!"]),
    ("es", "Y, sin embargo, se fue.", ["Y,", "sin embargo,", "se fue."]),
    ("es", "No. ¿Y? Nada.", ["No.", "¿Y?", "Nada."]),
    ("es-mx", "Y, sin embargo, se fue.", ["Y,", "sin embargo,", "se fue."]),
    ("es-mx", "Bueno, ¿y?", ["Bueno,", "¿y?"]),
])
def test_a_one_letter_clause_of_a_longer_text_is_a_word(speak, lang, text, expected):
    got = speak(lang, text)
    assert got == expected
    for clause in got:
        assert "agudo" not in clause and "griega" not in clause and "acentuada" not in clause


@pytest.mark.parametrize("lang, text, expected", [
    ("pt-br", "é", ["é agudo"]),
    ("pt-br", "É", ["é agudo"]),
    ("pt-br", "É.", ["é agudo."]),
    ("pt", "ç", ["cê cedilha"]),
    ("es", "y", ["i griega"]),
    ("es", "ó.", ["o acentuada."]),
    ("es", "r?", ["ere?"]),
    ("es", "¿ñ?", ["¿eñe?"]),
    ("es", " R ", ["ere"]),
    ("es-mx", "Y", ["i griega"]),
])
def test_a_letter_that_is_the_whole_text_keeps_its_name(speak, lang, text, expected):
    """#122 stays fixed: character navigation, typed characters, and a line
    that is one letter next to a symbol."""
    assert speak(lang, text) == expected


@pytest.mark.parametrize("lang, text", [
    ("es", "y"), ("es", "ó."), ("es", "¿ñ?"), ("es", " R "), ("pt-br", "É."), ("pt-br", "é"),
    ("es", "rr?"), ("es", "..."), ("es", "h"), ("en-us", "a"), ("hu", "ő"),
])
def test_prepare_text_is_prepare_clause_for_a_whole_text(frontend, pack_dir, lang, text):
    """nvspFrontend_prepareText keeps its meaning (the text is the whole
    utterance), for the wheel, tgsbRender --prepare-text and older callers."""
    h = frontend.create_handle(pack_dir)
    try:
        frontend.set_language(h, lang)
        assert frontend.prepare_text(h, text) == frontend.prepare_clause(h, text, True)
    finally:
        frontend.destroy_handle(h)


@pytest.mark.parametrize("lang", ["en-us", "en-gb", "es", "es-mx", "pt-br", "pt", "hu", "de", "fr", "pl", "ru"])
@pytest.mark.parametrize("text", [
    "Ele é bom.", "Fui a casa y, después, dormí.", "1,234 people came in 1985.",
    "At 6:03 AM on June 6", "popcorn and M5-series", "WEB y Twain", "yndio", "ÁÁ", "rrr", "...", "?", "42",
])
def test_only_a_lone_letter_depends_on_being_the_whole_text(frontend, pack_dir, lang, text):
    """The flag does one thing.  Any clause that is not a lone letter is
    prepared the same either way, in every language."""
    h = frontend.create_handle(pack_dir)
    try:
        frontend.set_language(h, lang)
        whole = frontend.prepare_clause(h, text, True)
        part = frontend.prepare_clause(h, text, False)
        assert part == whole == frontend.prepare_text(h, text)
    finally:
        frontend.destroy_handle(h)


@pytest.mark.parametrize("lang", ["en-us", "hu", "de"])
@pytest.mark.parametrize("text", ["a", "I.", "ő", "y"])
def test_a_language_without_letter_names_is_unchanged(frontend, pack_dir, lang, text):
    h = frontend.create_handle(pack_dir)
    try:
        frontend.set_language(h, lang)
        assert frontend.prepare_clause(h, text, False) == frontend.prepare_clause(h, text, True) == text
    finally:
        frontend.destroy_handle(h)


# ---------------------------------------------------------------------------
# Every host that prepares text clause by clause says whether a clause is the
# whole text.  A host that still called nvspFrontend_prepareText on each
# clause would keep reading "É." as "é agudo."
# ---------------------------------------------------------------------------

# Each host that splits with nvspFrontend_nextClause, and the calls it makes
# to prepare a clause of that split.
CLAUSE_HOSTS = {
    "src/platforms/android/jni/tgsb_jni.cpp": 1,
    "src/platforms/ios/bridge/tgsb_bridge.cpp": 1,
    "src/platforms/speechd/sd_tgsb.cpp": 1,
    "src/sapi/tgsb_runtime.cpp": 1,
    "tools/tgsbRender.cpp": 1,
}
# nvspFrontend_prepareText calls that are given a whole text, not a clause of
# a split: Android's debug probe (one unsplit string) and tgsbRender's
# --prepare-text mode.
WHOLE_TEXT_CALLS = {
    "src/platforms/android/jni/tgsb_jni.cpp": 1,
    "tools/tgsbRender.cpp": 1,
}


def _strip_comments(src):
    src = re.sub(r"/\*.*?\*/", " ", src, flags=re.S)
    return re.sub(r"//[^\n]*", " ", src)


@pytest.mark.parametrize("path, calls", sorted(CLAUSE_HOSTS.items()))
def test_every_host_says_whether_a_clause_is_the_whole_text(path, calls):
    src = _strip_comments((REPO / path).read_text(encoding="utf-8"))
    assert len(re.findall(r"\bnvspFrontend_prepareClause\s*\(", src)) == calls, (
        f"{path}: expected {calls} nvspFrontend_prepareClause call(s)")
    assert len(re.findall(r"\bnvspFrontend_prepareText\s*\(", src)) == WHOLE_TEXT_CALLS.get(path, 0), (
        f"{path}: a clause of a split text goes through nvspFrontend_prepareClause (#158)")


def test_no_other_host_prepares_clauses_unchecked():
    """A new host (or call) must be listed above, so it is held to the same rule."""
    seen = set()
    for root in ("src/platforms", "src/sapi", "tools"):
        for p in (REPO / root).rglob("*"):
            if p.suffix not in (".cpp", ".c", ".h", ".hpp", ".mm", ".m", ".swift", ".kt") or ".cxx" in p.parts:
                continue
            if any(part.startswith("build") for part in p.parts):
                continue
            src = _strip_comments(p.read_text(encoding="utf-8", errors="ignore"))
            if re.search(r"\bnvspFrontend_prepare(Text|Clause)\s*\(", src):
                seen.add(p.relative_to(REPO).as_posix())
    assert seen == set(CLAUSE_HOSTS), f"hosts preparing text: {sorted(seen)}"


def test_the_nvda_driver_names_a_letter_only_when_it_is_the_whole_text():
    """The NVDA driver looks letter names up itself before it calls the
    frontend; both halves take the same answer to "is this clause the whole
    text?"."""
    src = (REPO / "nvdaAddon/synthDrivers/tgSpeechBox/speech_pipeline.py").read_text(encoding="utf-8")
    assert re.search(r"if\s+wholeText\s+and\s+len\(core\)\s*==\s*1", src)
    assert re.search(r"wholeText\s*=\s*len\(clauses\)\s*==\s*1", src)
    assert re.search(r"self\._frontend\.prepareText\(chunk,\s*wholeText\)", src)
    fe = (REPO / "nvdaAddon/synthDrivers/tgSpeechBox/_frontend.py").read_text(encoding="utf-8")
    assert "nvspFrontend_prepareClause" in fe

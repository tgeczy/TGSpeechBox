"""A language pack setting that is read is also used (#183, #184, #185).

Some settings were parsed from the packs, documented, set by shipped packs
and offered in the NVDA panel and the phoneme editor, while no code used the
value: changing them changed nothing.

- trillModulationFadeMs (#183): the fade between a trill's taps.  The frame
  emitter has used a fixed 0.25 ms since the square-wave trill gating
  (commit 75314d6, out of #84), on purpose; fifteen packs still set the
  key to 0-5 ms.
- coarticulationWordInitialFadeScale (#184): meant to scale coarticulation
  on a word's first consonant.  No pass has read it since the coarticulation
  pass was rewritten around endCF values (commit 2f9d9d9); hu.yaml set 0.8
  and Tuning.md told tuners to lower it "if word onsets lose bite".

They are removed from the frontend, the packs, the docs and both lists, so
nothing claims an effect it does not have.  A pack that still carries one of
the keys loads as before: an unknown key is ignored.

The guard at the end reads the sources: every field that mergeSettings
(src/frontend/pack.cpp) fills from a pack must be mentioned somewhere else
under src/.
"""
from __future__ import annotations

import ctypes
import os
import pathlib
import re
import sys

import pytest

REPO = pathlib.Path(__file__).resolve().parent.parent
_ARCH = "x86" if sys.maxsize <= 2 ** 32 else "x64"
os.environ.setdefault("TGSB_LIB_DIR", str(REPO / f"build-{_ARCH}-nvda" / "MinSizeRel"))
os.environ.setdefault("TGSB_PACKS_ROOT", str(REPO))
sys.path.insert(0, str(REPO / "python"))

from tgspeechbox.engine import Frontend  # noqa: E402

RATES = [0.5, 1.0, 1.6, 2.0, 3.0]

# The settings that were removed: the key as a pack writes it (flat, and the
# name inside its nested block where it has one).
REMOVED = {
    "trillModulationFadeMs": ["trillModulationFadeMs"],
    "coarticulationWordInitialFadeScale": ["coarticulationWordInitialFadeScale"],
}

# Read but not used, and not removed here (outside #183-#185): four Fujisaki
# settings that pitch_fujisaki.cpp never looks at.  Listed so the guard says
# so, and fails when one more appears.
KNOWN_UNUSED = {
    "boundarySmoothingNasalF2F3SpansPhone",
    "boundarySmoothingPlosiveSpansPhone",
    "fujisakiDeclinationMax",
    "fujisakiDeclinationPostFloor",
    "fujisakiDeclinationScale",
    "fujisakiPhraseDecay",
    "microprosodyVoicelessF0RaiseEndHz",
}

PLACES = {
    "the frontend (pack.cpp)": [REPO / "src" / "frontend" / "pack.cpp"],
    "the frontend (pack.h)": [REPO / "src" / "frontend" / "pack.h"],
    "the packs": sorted((REPO / "packs" / "lang").glob("*.yaml")),
    "Tuning.md": [REPO / "Tuning.md"],
    "the NVDA panel": [REPO / "nvdaAddon" / "synthDrivers" / "tgSpeechBox" / "langPackSettingsPanel.py"],
    "the phoneme editor": sorted((REPO / "tools" / "tgsbPhonemeEditorWin32").glob("*.cpp")),
    "tools/lang_pack.py": [REPO / "tools" / "lang_pack.py", REPO / "tools" / "generate_lang_pack.py"],
}


def _names(setting):
    """Every spelling of a removed setting: its keys and its field in LanguagePack."""
    return sorted(set(REMOVED[setting]) | {setting})


@pytest.mark.parametrize("place", sorted(PLACES))
@pytest.mark.parametrize("setting", sorted(REMOVED))
def test_a_removed_setting_is_named_nowhere(setting, place):
    found = []
    for path in PLACES[place]:
        text = path.read_text(encoding="utf-8")
        for name in _names(setting):
            if re.search(r"(?<![A-Za-z0-9_])" + re.escape(name) + r"(?![A-Za-z0-9_])", text):
                found.append(f"{path.relative_to(REPO).as_posix()}: {name}")
    assert found == [], f"{setting} is still in {place}"


def _frames(lang, ipa, speed, snippet=""):
    with Frontend(lang) as fe:
        if snippet:
            lib = fe._lib
            lib.nvspFrontend_applySettingOverrides.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
            lib.nvspFrontend_applySettingOverrides.restype = ctypes.c_int
            assert lib.nvspFrontend_applySettingOverrides(fe._h, snippet.encode("utf-8")) == 1
        return [(None if f.params is None else {k: (None if v != v else v) for k, v in f.params.items()},
                 None if f.ex is None else {k: (None if v != v else v) for k, v in f.ex.items()},
                 f.duration_ms, f.fade_ms) for f in fe.frames(ipa, speed)]


@pytest.mark.parametrize("speed", RATES)
@pytest.mark.parametrize("lang,ipa", [("es", "pˈero"), ("es", "rˈoxo"), ("hu", "ˈkɛrt ˈorː"), ("it", "kˈarro"),
                                      ("fi", "pˈurra"), ("de", "ʁˈoːt"), ("pl", "rˈɨba")])
def test_a_trill_does_not_depend_on_the_old_fade_setting(lang, ipa, speed):
    # What an old pack with the key still in it gets: the same trill.  If the
    # key is ever connected again this fails, and the test should then check
    # that the fade follows it at every rate.
    stock = _frames(lang, ipa, speed)
    for ms in (0, 5, 20):
        assert _frames(lang, ipa, speed, f"trillModulationFadeMs: {ms}\n") == stock, f"{lang} {ipa} @{speed}: {ms} ms"


@pytest.mark.parametrize("speed", RATES)
@pytest.mark.parametrize("lang,ipa", [("hu", "ˈkaːr ˈtɛ ˈpɒp"), ("hu", "ˈbɒb ˈdɛ ˈɡaːz"), ("en-us", "kˈɑːɹ tˈiː pˈɑːp"),
                                      ("es", "kˈasa tˈodo pˈapa")])
def test_word_onsets_do_not_depend_on_the_old_word_initial_scale(lang, ipa, speed):
    # hu.yaml carried 0.8; with nothing reading it, 0.3 and 1.0 gave the
    # same word-initial stops, and an old pack that still has the key gets
    # the same frames now.
    stock = _frames(lang, ipa, speed)
    for scale in (0.3, 0.8, 1.0):
        snippet = f"coarticulationWordInitialFadeScale: {scale}\n"
        assert _frames(lang, ipa, speed, snippet) == stock, f"{lang} {ipa} @{speed}: {scale}"


def _settings_read():
    """(key, LanguagePack field) for every setting pack.cpp reads as one value."""
    pack_cpp = (REPO / "src" / "frontend" / "pack.cpp").read_text(encoding="utf-8")
    return sorted(set(re.findall(
        r'\bget(?:Num|Bool|Str)(?:From)?\(\s*(?:\*?\w+,\s*)?"(\w+)",\s*lp\.(\w+)\)', pack_cpp)))


def test_every_setting_that_is_read_is_used():
    used_in = ""
    for path in (REPO / "src").rglob("*"):
        if path.suffix in (".cpp", ".h", ".mm", ".kt", ".swift") and path.name not in ("pack.cpp", "pack.h"):
            used_in += path.read_text(encoding="utf-8", errors="ignore")
    read = _settings_read()
    assert len(read) > 350  # a regex that finds nothing would pass
    unused = {field for _key, field in read if not re.search(r"\b" + re.escape(field) + r"\b", used_in)}
    assert unused == KNOWN_UNUSED, (
        f"read from the packs but used nowhere under src/: {sorted(unused - KNOWN_UNUSED)}; "
        f"listed as unused but used or gone: {sorted(KNOWN_UNUSED - unused)}")

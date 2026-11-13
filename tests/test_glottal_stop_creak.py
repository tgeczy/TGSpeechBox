"""The glottal stop ʔ is creaky, as its entry in phonemes.yaml says (#193).

The entry reads "implemented as brief creaky voice (laryngealization) rather
than hard silence" and set a creakiness of 0.85, but at the top level of the
entry, with a flag "_hasCreakiness" beside it.  The loader reads creakiness
only inside a `frameEx:` block and does not know that flag, so both lines
were dropped without a word and ʔ came out as quiet plain voicing.  The
Danish stød ˀ, written with `frameEx:`, has always had its creak.

In the voice source (src/voiceGenerator.h) a creakiness of c lowers F0 by
12% x c, the voicing amplitude by 35% x c, and adds 0.10 x c to the closed
part of the glottal cycle, with irregular periods.  At 0.85: F0 -10%, voicing
-3 dB, open phase 60% -> 51.5%.  That is the direction of the laryngealized
onset in Klatt & Klatt 1990 (JASA 87(2): 820-857: F0 falling, less voicing
amplitude, a smaller open quotient), and glottal stops are weakly voiced for
most of their length rather than silent (Garellek, Chai, Huang & Van Doren
2023, JIPA 53(2): 305-332).

Where ʔ comes from: eSpeak NG writes it in American English for a t before a
syllabic n ("button" bˈʌʔn̩, "kitten", "certain", "cotton", "written"), and
its British voice in a handful of dictionary entries.

One shipped pack inserts it: German's glottal_reinforcement rule puts a ʔ
before every word-final p, t or k that follows a vowel ("mit", "gut", "Tag").
That rule is the subject of #190 and is still in the pack, so each of those
frames carries the creak too.  The frame is silent there (its preFormantGain
is 0), but German sentences are not sample for sample what a build without
the creak plays (the loudness over 150 ms is within 1 dB), and no German
speaker has compared the two.  (The first description of this change
said German was untouched but for one dictionary word; it was measured on a
tree where #190's removal of the rule had been applied.)
"""
from __future__ import annotations

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

SPEEDS = [0.5, 1.0, 1.6, 2.0, 3.0]
QUALITY = ("creakiness", "breathiness", "jitter", "shimmer")

# "button" as eSpeak NG 1.53 writes it for en-us (and so for en-ca).  Its
# British voice writes bˈʌtən; en-gb and en-au get the same string here to
# show that the phoneme is the shared one.
BUTTON = {"en-us": "bˈʌʔn̩", "en-ca": "bˈʌʔn̩", "en-gb": "bˈʌʔn̩", "en-au": "bˈʌʔn̩"}


def _frames_by_key(lang, ipa, speed):
    """[(phoneme key, the first frame of that phoneme)]"""
    with Frontend(lang) as fe:
        frames = fe.frames(ipa, speed)
        return [(e["phonemeKey"], frames[e["frameIndex"]]) for e in fe.frame_trace()]


@pytest.mark.parametrize("speed", SPEEDS)
@pytest.mark.parametrize("lang", list(BUTTON))
def test_the_glottal_stop_of_button_is_creaky(lang, speed):
    stops = [f for k, f in _frames_by_key(lang, BUTTON[lang], speed) if k == "ʔ"]
    assert len(stops) == 1
    assert stops[0].ex["creakiness"] == pytest.approx(0.85)


@pytest.mark.parametrize("speed", SPEEDS)
@pytest.mark.parametrize("lang", list(BUTTON))
def test_nothing_else_in_the_word_is_creaky(lang, speed):
    for key, frame in _frames_by_key(lang, BUTTON[lang], speed):
        if key != "ʔ" and frame.ex is not None:
            assert frame.ex["creakiness"] == 0.0, key


@pytest.mark.parametrize("speed", SPEEDS)
def test_german_inserts_one_before_a_word_final_stop_and_it_is_silent(speed):
    # "mit", as eSpeak NG writes it.  Holds the state #190 will change: one
    # inserted ʔ, with the creak, on a frame that plays no sound of its own.
    sounds = _frames_by_key("de", "mˈɪt", speed)
    stops = [f for k, f in sounds if k == "ʔ"]
    assert len(stops) == 1, [k for k, _ in sounds]
    assert stops[0].ex["creakiness"] == pytest.approx(0.85)
    assert stops[0].params["preFormantGain"] == 0.0


@pytest.mark.parametrize("speed", SPEEDS)
def test_the_rest_of_the_glottal_stop_is_what_it_was(speed):
    # Quiet voicing with no noise, as before: only the creak is new.
    (stop,) = [f for k, f in _frames_by_key("en-us", "bˈʌʔn̩", speed) if k == "ʔ"]
    assert stop.params["voiceAmplitude"] == pytest.approx(0.08)
    assert stop.params["fricationAmplitude"] == 0.0
    assert stop.params["aspirationAmplitude"] == 0.0
    assert stop.ex["jitter"] == 0.0 and stop.ex["shimmer"] == 0.0 and stop.ex["breathiness"] == 0.0


@pytest.mark.parametrize("speed", SPEEDS)
def test_the_danish_stod_is_what_it_was(speed):
    (stod,) = [f for k, f in _frames_by_key("da", "hˈuˀs", speed) if k == "ˀ"]
    assert stod.ex["creakiness"] == pytest.approx(0.55)
    assert stod.ex["jitter"] == pytest.approx(0.12)
    # eSpeak NG's own spelling of the stød is routed to ˀ, not to ʔ.
    keys = [k for k, _ in _frames_by_key("da", "hˈʔ͡enəs", speed)]
    assert "ˀ" in keys and "ʔ" not in keys


@pytest.mark.parametrize("lang", ["en-us", "en-gb", "hu", "es", "de", "pt-br", "pl", "fr"])
def test_a_word_without_a_glottal_stop_has_no_creak(lang):
    for key, frame in _frames_by_key(lang, "bˈʌtən", 1.0):
        if frame.ex is not None:
            assert frame.ex["creakiness"] == 0.0, key


# ── the pack: no voice-quality value where the loader does not look ──────────

def _known_flags():
    cpp = (REPO / "src" / "frontend" / "pack.cpp").read_text(encoding="utf-8")
    start = cpp.index("static std::uint32_t parseFlagKey(")
    return set(re.findall(r'key == "(_[A-Za-z]+)"', cpp[start:cpp.index("\n}", start)]))


def _phoneme_entry_keys(path):
    """(line, phoneme, key) for each key directly inside an entry of `phonemes:`."""
    inside, entry = False, None
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if re.match(r"[A-Za-z]", line):
            inside, entry = line.startswith("phonemes:"), None
            continue
        if not inside:
            continue
        m = re.match(r'  ("?)([^\s#"][^"]*?)\1:\s*(#.*)?$', line)
        if m:
            entry = m.group(2)
            continue
        m = re.match(r"    ([A-Za-z_0-9]+):", line)
        if m and entry:
            yield number, entry, m.group(1)


PACK_FILES = [REPO / "packs" / "phonemes.yaml"] + sorted((REPO / "packs" / "lang").glob("*.yaml"))


def test_the_walk_finds_the_phonemes():
    assert len(_known_flags()) >= 10 and "_copyAdjacent" in _known_flags()
    keys = list(_phoneme_entry_keys(REPO / "packs" / "phonemes.yaml"))
    assert len({p for _, p, _ in keys}) > 200
    assert ("ˀ", "frameEx") in {(p, k) for _, p, k in keys}


@pytest.mark.parametrize("path", PACK_FILES, ids=[p.stem for p in PACK_FILES])
def test_no_voice_quality_value_sits_outside_frameEx(path):
    # The loader reads these from `frameEx:` only; at the top level of an
    # entry they are dropped without a warning.
    misplaced = [f"{path.name}:{n} {p} {k}" for n, p, k in _phoneme_entry_keys(path)
                 if k in QUALITY + ("sharpness",)]
    assert misplaced == []


@pytest.mark.parametrize("path", PACK_FILES, ids=[p.stem for p in PACK_FILES])
def test_every_flag_is_one_the_loader_knows(path):
    known = _known_flags()
    unknown = [f"{path.name}:{n} {p} {k}" for n, p, k in _phoneme_entry_keys(path)
               if k.startswith("_") and k not in known]
    assert unknown == []

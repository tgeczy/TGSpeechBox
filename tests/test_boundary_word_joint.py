"""A joint between two words is not smoothed as if it were inside a syllable (#182).

Boundary smoothing gives a joint inside one syllable a longer fade
(withinSyllableFadeScale, 1.3) and slower formant movement
(withinSyllableScale, 1.5): it is one gesture of the mouth.  "Inside one
syllable" was decided by the two sounds having the same syllable number, and
syllables are numbered from 0 in every word.  So the last sound of a
one-syllable word and the first sound of the next word, both in syllable 0,
passed for one syllable: the m of "man" got a 20.8 ms fade after "the" and
16 ms after "sofa".  The same joint, treated differently by the length of the
word before.

Now the two sounds must also be in the same word.

The fade read here is the one the boundary_smoothing pass leaves on a token;
the formant transition scales are read from the frames.  The IPA is eSpeak NG
style.
"""
from __future__ import annotations

import ctypes
import os
import pathlib
import sys

import pytest

REPO = pathlib.Path(__file__).resolve().parent.parent
_ARCH = "x86" if sys.maxsize <= 2 ** 32 else "x64"
os.environ.setdefault("TGSB_LIB_DIR", str(REPO / f"build-{_ARCH}-nvda" / "MinSizeRel"))
os.environ.setdefault("TGSB_PACKS_ROOT", str(REPO))
sys.path.insert(0, str(REPO / "python"))

from tgspeechbox.engine import Frontend  # noqa: E402

RATES = [0.5, 1.0, 1.6, 2.0, 3.0]
SCALES = ("transF1Scale", "transF2Scale", "transF3Scale")


def _joint(lang, ipa, speed, key, snippet=""):
    """The last phoneme with this key (m finds m_es too): its fade after boundary smoothing, and its
    frame's transition scales."""
    with Frontend(lang) as fe:
        if snippet:
            lib = fe._lib
            lib.nvspFrontend_applySettingOverrides.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
            lib.nvspFrontend_applySettingOverrides.restype = ctypes.c_int
            assert lib.nvspFrontend_applySettingOverrides(fe._h, snippet.encode("utf-8")) == 1
        frames = fe.frames(ipa, speed)
        trace = fe.frame_trace()
        rows = [r for r in fe.pass_trace()
                if r["pass"] == "boundary_smoothing" and r["phonemeKey"].split("_")[0] == key]
    hits = [e for e in trace if e["phonemeKey"].split("_")[0] == key]
    assert rows and hits, f"{lang} {ipa}: no {key} in {[e['phonemeKey'] for e in trace]}"
    frame = frames[hits[-1]["frameIndex"]]
    return rows[-1]["fadeMs"], tuple(frame.ex[s] for s in SCALES)


# (language, after a one-syllable word, after a longer word, key of the next word's first sound)
# The sound before the joint is the same in both, so the joint is the same.
PAIRS = [
    ("en-us", "ðə mˈæn", "sˈoʊfə mˈæn", "m"),        # the man, sofa man
    ("en-us", "tˈuː mˈæn", "ˈɪntuː mˈæn", "m"),      # two man, into man
    ("en-gb", "ðə mˈan", "sˈəʊfə mˈan", "m"),
    ("en-au", "ðə mˈæn", "sˈəʊfə mˈæn", "m"),
    ("hu", "tˈɛ mˈɒ", "fˈɛkɛtɛ mˈɒ", "m"),           # te ma, fekete ma
    ("tr", "bˈu mˈasa", "kutˈu mˈasa", "m"),         # bu masa, kutu masa
]


@pytest.mark.parametrize("speed", RATES)
@pytest.mark.parametrize("lang,short,longer,key", PAIRS)
def test_the_joint_is_the_same_after_a_short_word_and_a_long_one(lang, short, longer, key, speed):
    fade_short, scales_short = _joint(lang, short, speed, key)
    fade_long, scales_long = _joint(lang, longer, speed, key)
    assert fade_short == pytest.approx(fade_long, abs=0.01), (
        f"{lang} @{speed}: fade {fade_short:.1f} ms in {short}, {fade_long:.1f} ms in {longer}")
    assert scales_short == pytest.approx(scales_long, abs=1e-6), (
        f"{lang} @{speed}: F1-F3 transition scales {scales_short} in {short}, {scales_long} in {longer}")


@pytest.mark.parametrize("speed", RATES)
@pytest.mark.parametrize("lang,short,longer,key", PAIRS)
def test_a_word_joint_does_not_depend_on_the_within_syllable_settings(lang, short, longer, key, speed):
    # Doubling both scales must leave the first sound of the second word alone.
    more = "boundarySmoothingWithinSyllableFadeScale: 2.6\nboundarySmoothingWithinSyllableScale: 3.0\n"
    for ipa in (short, longer):
        assert _joint(lang, ipa, speed, key) == _joint(lang, ipa, speed, key, more), f"{lang} {ipa} @{speed}"


@pytest.mark.parametrize("speed", RATES)
@pytest.mark.parametrize("lang,ipa,key", [
    ("en-us", "ðə mˈæn", "æ"),      # m to the vowel, inside "man"
    ("en-us", "sˈoʊfə mˈæn", "n"),  # the vowel to n, inside "man"
    ("en-gb", "ðə mˈan", "a"),
    ("hu", "tˈɛ mˈɒ", "ᴒ"),         # the pack's key for ɒ
    ("tr", "bˈu mˈasa", "a"),
])
def test_a_joint_inside_a_word_still_gets_them(lang, ipa, key, speed):
    more = "boundarySmoothingWithinSyllableFadeScale: 2.6\nboundarySmoothingWithinSyllableScale: 3.0\n"
    plain = _joint(lang, ipa, speed, key)
    scaled = _joint(lang, ipa, speed, key, more)
    assert plain != scaled, f"{lang} {ipa} @{speed}: {key} is the same with the scales doubled: {plain}"


@pytest.mark.parametrize("speed", RATES)
@pytest.mark.parametrize("lang,ipa", [("es", "la mˈano"), ("fr", "la mˈɛ̃"), ("it", "la mˈano"), ("pt-br", "a mˈɐ̃w")])
def test_languages_without_boundary_smoothing_are_not_touched(lang, ipa, speed):
    more = "boundarySmoothingWithinSyllableFadeScale: 2.6\nboundarySmoothingWithinSyllableScale: 3.0\n"
    assert _joint(lang, ipa, speed, "m") == _joint(lang, ipa, speed, "m", more)

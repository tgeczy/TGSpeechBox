"""Slow speech does not stretch the fallback consonant-to-consonant fade (#186).

Boundary smoothing blends each sound into the next over a fade.  Its rule,
written at the top of the pass: fast speech may shorten the fades, slow speech
must not make them longer than the pack says ("the ear expects crisper
boundaries when phonemes are longer").  Every kind of boundary divides its
fade by max(speed, 1.0) for that.  The fallback for consonant pairs no other
rule covers divided its 10 ms by the raw speed instead, so at 0.5 it gave
20 ms, or 26 inside a syllable, where 10 and 13 were meant.

The fade read here is the one the boundary_smoothing pass leaves on a token.
The IPA is eSpeak NG style.
"""
from __future__ import annotations

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
FALLBACK_MS = 10.0      # the pass's fade for a consonant pair it has no rule for
WITHIN_SYLLABLE = 1.3   # boundarySmoothingWithinSyllableFadeScale, the default
MIN_FADE_MS = 6.0       # the pass's floor for a fade it stretches


def _pass_rows(lang, ipa, speed, name):
    with Frontend(lang) as fe:
        fe.frames(ipa, speed)
        trace = fe.pass_trace()
    names = []
    for row in trace:
        if row["pass"] not in names:
            names.append(row["pass"])
    before = names[names.index(name) - 1]
    return [r for r in trace if r["pass"] == before], [r for r in trace if r["pass"] == name]


def _fade(lang, ipa, speed, index):
    """(key, fade before boundary smoothing, fade after) of the token at this index."""
    before, after = _pass_rows(lang, ipa, speed, "boundary_smoothing")
    return after[index]["phonemeKey"], before[index]["fadeMs"], after[index]["fadeMs"]


# (language, IPA, index of the token after the boundary, its key, inside one syllable?)
# en-us splits its l in two: the joint inside it is a liquid after a liquid.
FALLBACK_JOINTS = [
    ("en-us", "ˈɑnli", 3, "l", True),     # only
    ("en-us", "plˈeɪ", 3, "l", True),     # play
]


@pytest.mark.parametrize("lang,ipa,index,key,within", FALLBACK_JOINTS)
def test_the_fallback_fade_at_half_speed_is_the_one_at_normal_speed(lang, ipa, index, key, within):
    want = FALLBACK_MS * (WITHIN_SYLLABLE if within else 1.0)
    k1, was1, at_1x = _fade(lang, ipa, 1.0, index)
    k05, was05, at_half = _fade(lang, ipa, 0.5, index)
    assert k1 == key and k05 == key
    assert was1 < want and was05 < want, "the pass has nothing to stretch here"
    assert at_1x == pytest.approx(want, abs=0.01), f"{lang} {ipa} @1.0: {at_1x:.1f} ms"
    assert at_half == pytest.approx(want, abs=0.01), f"{lang} {ipa} @0.5: {at_half:.1f} ms, {at_1x:.1f} at 1.0"


@pytest.mark.parametrize("speed", RATES)
@pytest.mark.parametrize("lang,ipa,index,key,within", FALLBACK_JOINTS)
def test_the_fallback_fade_shrinks_with_fast_speech_and_not_with_slow(lang, ipa, index, key, within, speed):
    want = FALLBACK_MS * (WITHIN_SYLLABLE if within else 1.0) / max(speed, 1.0)
    k, was, now = _fade(lang, ipa, speed, index)
    assert k == key
    assert now == pytest.approx(max(want, MIN_FADE_MS), abs=0.01), f"{lang} {ipa} @{speed}: {was:.1f} -> {now:.1f} ms"


@pytest.mark.parametrize("speed", [0.5])
@pytest.mark.parametrize("lang,ipa", [
    ("en-us", "ˈɑnli"), ("en-us", "plˈeɪ"), ("en-us", "fˈɪlm ˈɛlmz hˈɛlpfəl"),
    ("en-gb", "fˈɪlm ˈɛlmz hˈɛlpfəl"), ("en-au", "fˈɪlm ˈɛlmz hˈɛlpfəl"),
    ("hu", "ˈɒlmɒ ˈkɛrt ˈsɛrv"), ("hu", "kˈɑpt͡ʃolni"), ("tr", "ˈilk dˈœrt kˈɯrk"), ("tr", "ˈilɛ"),
    ("pl", "vrˈut͡ɕ vznˈɔɕ"), ("sv", "sˈuːvrɵm"), ("fi", "pˈurra"), ("bg", "svrˈəx"), ("uk", "hruzˈiːja"),
    ("de", "ˈɪntɜnɛtt͡sˌuːɡɾɪf"),
])
def test_no_fade_is_stretched_past_its_normal_speed_length(lang, ipa, speed):
    # Every boundary's target is the same at 0.5 as at 1.0, so where the pass
    # stretches a fade at slow speed, it comes out as at 1.0.  (A fade that a
    # short phoneme capped at 1.0 has more room at 0.5; a third more covers
    # that.  The old fallback gave twice as much.)
    _, at_1x = _pass_rows(lang, ipa, 1.0, "boundary_smoothing")
    before, after = _pass_rows(lang, ipa, speed, "boundary_smoothing")
    assert [r["phonemeKey"] for r in after] == [r["phonemeKey"] for r in at_1x]
    for i, (b, a, n) in enumerate(zip(before, after, at_1x)):
        if a["fadeMs"] > b["fadeMs"] + 1e-9:  # stretched by the pass
            assert a["fadeMs"] <= max(n["fadeMs"], MIN_FADE_MS) / 0.75 + 0.01, (
                f"{lang} {ipa} @{speed}: {a['phonemeKey']} (token {i}) {b['fadeMs']:.1f} -> {a['fadeMs']:.1f} ms, "
                f"{n['fadeMs']:.1f} at 1.0")

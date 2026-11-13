"""A vowel before a voiceless consonant is never made longer (#181).

In English a vowel is shorter before a voiceless consonant ("bit") than
before a voiced one ("bid"), about 0.61-0.69 as long (Chen 1970, Phonetica
22: 129-159), and listeners use that to tell t from d.  The microprosody pass
copies it: it multiplies the vowel by microprosodyPreVoicelessShortenScale and
stops at microprosodyPreVoicelessMinMs.

That floor was a fixed number of milliseconds (35 for en-us, en-au and tr, 40
for en-gb).  At fast rates the vowel was already under it when the pass ran,
so "stopping at the floor" lifted it: British "rabbit" at 2.0 went from 25.5
to 40.0 ms while "rabid" stayed at 29.3, the cue the wrong way round.  A
shortening rule works on the part of a segment above its minimum and cannot
cross it from either side (Klatt 1976, J. Acoust. Soc. Am. 59: 1208-1221:
Df = K * (Di - Dmin) + Dmin).

The floor is divided by the speed above 1.0 now, down to
microprosodyMinVowelMs (25 ms, the length under which the pass leaves a vowel
alone), and the result is never longer than the vowel was.  Speeds up to 1.0
are as they were.

The IPA is eSpeak NG style.
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
LANGS = ["en-us", "en-gb", "en-au", "tr"]  # the packs that set a floor above the default
VOICELESS = set("ptkfsʃθxh") | {"t͡ʃ", "t͡s"}

# (before a voiceless consonant, the same before a voiced one)
PAIRS = [
    ("ˈræbɪt", "ˈræbɪd"),        # rabbit, rabid: unstressed vowel
    ("bˈɪt", "bˈɪd"),            # bit, bid
    ("ðə bˈɪt kˈæt", "ðə bˈɪd kˈæt"),
    ("bˈæk", "bˈæɡ"),            # back, bag
    ("lˈɒkɪt", "lˈɒkɪd"),        # unstressed, after a stop
]
SENTENCES = [
    "ðə kˈæt sˈæt ɒn ðə mˈæt",
    "ɪt ɪz ɐ bˈɪt ɒv ɐ sˈiːkɹət",
    "ðə ɹˈæbɪt bˈɪt ðə kˈɑːpɪt",
    "pˈɪk ɪt ˈʌp ænd pˈʊt ɪt bˈæk",
    "ɐksˈɛpt ðɪ ˈaʊtpʊt ɒv ðə sˈɪstəm",
]


def _passes(lang, ipa, speed, snippet=""):
    """The tokens as the pass before microprosody left them, and after it."""
    with Frontend(lang) as fe:
        if snippet:
            lib = fe._lib
            lib.nvspFrontend_applySettingOverrides.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
            lib.nvspFrontend_applySettingOverrides.restype = ctypes.c_int
            assert lib.nvspFrontend_applySettingOverrides(fe._h, snippet.encode("utf-8")) == 1
        fe.frames(ipa, speed)
        trace = fe.pass_trace()
    names = []
    for row in trace:
        if row["pass"] not in names:
            names.append(row["pass"])
    assert names[-1] == "microprosody", names
    before = [r for r in trace if r["pass"] == names[-2]]
    after = [r for r in trace if r["pass"] == "microprosody"]
    assert [r["phonemeKey"] for r in before] == [r["phonemeKey"] for r in after]
    return before, after


def _is_vowel(row):
    return row["voiceAmplitude"] > 0 and row["fricationAmplitude"] == 0 and row["phonemeKey"][0] in "aeiouæɑɒɔəɛɪʊʌɐᵻɜɯøyœ"


def _vowels_before_voiceless(lang, ipa, speed, snippet=""):
    """(key, ms before the pass, ms after) for each vowel that a voiceless consonant follows."""
    before, after = _passes(lang, ipa, speed, snippet)
    out = []
    for i in range(len(after) - 1):
        nxt = after[i + 1]["phonemeKey"].split("_")[0]
        if _is_vowel(after[i]) and nxt in VOICELESS:
            out.append((after[i]["phonemeKey"], before[i]["durationMs"], after[i]["durationMs"]))
    return out


def _last_vowel_ms(lang, ipa, speed):
    """The length of the vowel before the word's last consonant, after microprosody."""
    _, after = _passes(lang, ipa.split(" ")[1] if ipa.startswith("ðə ") else ipa, speed)
    vowels = [r for r in after if _is_vowel(r)]
    return vowels[-1]["durationMs"]


@pytest.mark.parametrize("speed", RATES)
@pytest.mark.parametrize("lang", LANGS)
def test_the_pass_never_lengthens_a_vowel_before_a_voiceless_consonant(lang, speed):
    seen = 0
    for ipa in SENTENCES + [p[0] for p in PAIRS]:
        for key, was, now in _vowels_before_voiceless(lang, ipa, speed):
            seen += 1
            assert now <= was + 1e-9, f"{lang} @{speed} {ipa}: {key} {was:.1f} -> {now:.1f} ms"
    assert seen >= 10, "the test found no vowel before a voiceless consonant"


@pytest.mark.parametrize("speed", RATES)
@pytest.mark.parametrize("lang", LANGS)
@pytest.mark.parametrize("voiceless,voiced", PAIRS)
def test_the_vowel_before_voiceless_is_not_the_longer_one(lang, speed, voiceless, voiced):
    short = _last_vowel_ms(lang, voiceless, speed)
    long_ = _last_vowel_ms(lang, voiced, speed)
    assert short <= long_ + 1e-9, f"{lang} @{speed}: {voiceless} {short:.1f} ms, {voiced} {long_:.1f} ms"


@pytest.mark.parametrize("speed", RATES)
@pytest.mark.parametrize("lang", LANGS)
def test_a_vowel_with_room_is_still_shortened(lang, speed):
    # The stressed vowel of "back" is well over the floor at every rate up
    # to 2.0: it gets the pack's full scale.  At 3.0 it stops at the floor.
    scale = {"en-us": 0.80, "en-gb": 0.85, "en-au": 0.79, "tr": 0.88}[lang]
    (key, was, now), = _vowels_before_voiceless(lang, "bˈæk", speed)
    assert now < was, f"{lang} @{speed}: {key} {was:.1f} -> {now:.1f} ms"
    if speed <= 2.0:
        assert now == pytest.approx(was * scale, abs=0.01)
    else:
        assert now == pytest.approx(max(was * scale, 25.0), abs=0.01)


@pytest.mark.parametrize("speed", [1.6, 2.0, 3.0])
@pytest.mark.parametrize("lang", LANGS)
def test_the_floor_shrinks_with_the_rate_down_to_the_min_vowel_length(lang, speed):
    # A floor far above any vowel, so every result sits on it: 400 ms at 1.0.
    for key, was, now in _vowels_before_voiceless(lang, "bˈæk", speed, "microprosodyPreVoicelessMinMs: 400\n"):
        assert now == pytest.approx(was), "a vowel under the floor is left as it was"
    # A floor the vowel reaches: the scale is 0.5, so the vowel wants to
    # halve, and stops at 60 / speed, or at 25 where that is less.
    snippet = "microprosodyPreVoicelessMinMs: 60\nmicroprosodyPreVoicelessShortenScale: 0.5\n"
    (key, was, now), = _vowels_before_voiceless(lang, "bˈæk", speed, snippet)
    assert now == pytest.approx(min(was, max(was * 0.5, 60.0 / speed, 25.0)), abs=0.01), f"{was:.1f} -> {now:.1f}"


@pytest.mark.parametrize("speed", [0.5, 1.0])
@pytest.mark.parametrize("lang", LANGS)
def test_up_to_normal_speed_the_floor_is_the_number_in_the_pack(lang, speed):
    snippet = "microprosodyPreVoicelessMinMs: 60\nmicroprosodyPreVoicelessShortenScale: 0.1\n"
    (key, was, now), = _vowels_before_voiceless(lang, "bˈæk", speed, snippet)
    assert was > 60.0 and now == pytest.approx(60.0, abs=0.01), f"{lang} @{speed}: {was:.1f} -> {now:.1f} ms"


@pytest.mark.parametrize("speed", RATES)
@pytest.mark.parametrize("lang", ["de", "nl", "sv"])
def test_languages_on_the_default_floor_are_as_they_were(lang, speed):
    # Their floor is 25 ms, which is also the length under which the pass
    # skips a vowel: a vowel it touched was never lifted and still stops at 25.
    seen = 0
    for ipa in ["bˈɪtə", "ˈʁabɪtə", "zˈaksə ˈapfəl", "kˈatə", "ˈɛsən ɪst fˈɛɾtɪç", "bˈiːtə", "ʃtɾˈaːsə", "ɡɾˈoːsə"]:
        for key, was, now in _vowels_before_voiceless(lang, ipa, speed):
            if was < 25.0:
                assert now == was  # skipped
                continue
            seen += 1
            assert now == pytest.approx(max(was * 0.85, 25.0), abs=0.01), f"{lang} @{speed} {ipa}: {key}"
    assert seen >= (1 if speed == 3.0 else 3)

"""The American flap stays a flap at fast rates ("accessibility", "city").

A flap is one quick flick of the tongue, and it doesn't shrink with speaking
rate the way vowels do.  The frontend shrank it in step with the rate: about
13 ms at normal speed but 4 ms at rate 3, too short to dip the voicing, so
it smeared into the vowels on either side (Tomi).  The rate floor for taps
(rateCompensation tapMs) was 4 ms; it now keeps the flap near 10 ms.
Normal and slow rates are unchanged.

The IPA is eSpeak NG's.
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

import tgspeechbox  # noqa: E402

WORDS = [("accessibility", "æksˌɛsəbˈɪlᵻɾi"), ("city", "sˈɪɾi"), ("butter", "bˈʌɾɚ")]


def _flap_ms(ipa, lang, speed):
    u = tgspeechbox.render(ipa, lang, speed=speed)
    tr = u.frame_trace
    for n, e in enumerate(tr):
        if e["phonemeKey"] == "ɾ":
            end = tr[n + 1]["frameIndex"] if n + 1 < len(tr) else len(u.frames)
            return sum(f.duration_ms for f in u.frames[e["frameIndex"]:end])
    raise AssertionError(f"no flap in {ipa}")


@pytest.mark.parametrize("lang", ["en-us", "en-au"])
@pytest.mark.parametrize("speed", [1.6, 2.0, 3.0])
@pytest.mark.parametrize("word,ipa", WORDS)
def test_flap_keeps_its_length_at_fast_rates(lang, speed, word, ipa):
    ms = _flap_ms(ipa, lang, speed)
    assert ms >= 8.5, f"{lang} {word} @{speed}: flap {ms:.1f} ms"


@pytest.mark.parametrize("lang", ["en-us", "en-au"])
@pytest.mark.parametrize("speed", [0.5, 1.0])
@pytest.mark.parametrize("word,ipa", WORDS)
def test_flap_unchanged_at_normal_and_slow_rates(lang, speed, word, ipa):
    ms = _flap_ms(ipa, lang, speed)
    assert 12.0 <= ms <= 15.0, f"{lang} {word} @{speed}: flap {ms:.1f} ms"

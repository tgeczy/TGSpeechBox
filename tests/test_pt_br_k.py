"""Brazilian Portuguese k: a velar that can't be heard as t (Cleverson).

Cleverson, a native speaker: "ca" and "ta", "cu" and "tu" sounded too
close; he wanted a k with the clarity of en-us "key".  Working in the phoneme
editor he settled on a pt-br k (k_ptbr: an 18 ms burst with its energy low,
pa2 0.80, pa3 0.85, pa5 0.10, pa6 0) and a short puff of aspiration after
voiceless stops (15 ms; Brazilian Portuguese velars carry a short lag before
the vowel, though nothing like English's).  These are his values.
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


@pytest.mark.parametrize("speed", [0.5, 1.0, 1.6, 2.0, 3.0])
@pytest.mark.parametrize("ipa", ["kˈazɐ", "kˈumɪ", "kˈe͡ɪʒʊ"])  # casa, cume, queijo
def test_k_is_the_pt_br_k_with_a_short_puff(ipa, speed):
    u = tgspeechbox.render(ipa, "pt-br", speed=speed)
    keys = [e["phonemeKey"] for e in u.frame_trace]
    assert keys[0] == "k_ptbr" and keys[1] == "h", f"{ipa} @{speed}: {keys}"
    burst = u.frames[0].params
    assert (burst["pa2"], burst["pa3"], burst["pa5"], burst["pa6"]) == pytest.approx((0.80, 0.85, 0.10, 0.0), abs=0.06)


def test_other_languages_keep_their_k():
    keys = [e["phonemeKey"] for e in tgspeechbox.render("kˈasa", "es").frame_trace]
    assert keys[0] == "k" and "h" not in keys

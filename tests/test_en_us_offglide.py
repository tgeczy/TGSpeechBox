"""en-us FACE, PRICE and CHOICE end on the pack's /ɪ/, as they always have (#191).

en-us.yaml carried a rule, diphthong_offglide_sharpen_ɪ, meant to push that
last /ɪ/ toward /i/ (F1 -40 Hz, F2 +150 Hz).  Its fieldShifts was written
as a map and the loader reads a list, so it never ran, and en-us was tuned
and approved by ear without it.  The dead rule is out of the pack (its text
is kept there as a comment, in the form that would work); what plays is
unchanged: the glides end at 360 / 1900 Hz, not 320 / 2050, at every rate.

tests/cpp/test_allophone_rules_loaded.cpp is the test that failed on the
dead rule.  This one says what removing it must leave alone, and it is the
one to change, after listening, if the rule is ever turned on.

The IPA is eSpeak NG's en-us, with tie bars as the hosts ask for it and
without as its command line writes it.
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

TIE = chr(0x361)
WORDS = {"face": ("fˈe͡ɪs", "e"), "day": ("dˈe͡ɪ", "e"), "price": ("pɹˈa͡ɪs", "ɑ"), "my": ("mˈa͡ɪ", "ɑ"),
         "boy": ("bˈɔ͡ɪ", "ᴐ"), "choice": ("t͡ʃˈɔ͡ɪs", "ᴐ")}


@pytest.mark.parametrize("speed", [0.5, 1.0, 1.6, 2.0, 3.0])
@pytest.mark.parametrize("tied", [True, False])
@pytest.mark.parametrize("word", list(WORDS))
def test_the_glide_ends_on_the_packs_own_i(word, tied, speed):
    ipa, onset = WORDS[word]
    u = tgspeechbox.render(ipa if tied else ipa.replace(TIE, ""), "en-us", speed=speed)
    tr = u.frame_trace
    keys = [e["phonemeKey"] for e in tr]
    n = keys.index(onset)
    end = tr[n + 1]["frameIndex"] if n + 1 < len(tr) else len(u.frames)
    last = [f.params for f in u.frames[tr[n]["frameIndex"]:end] if f.params][-1]
    assert (round(last["cf1"]), round(last["cf2"])) == (360, 1900), (
        f"{word} @{speed}: the glide ends at {last['cf1']:.0f} / {last['cf2']:.0f} Hz; {keys}")

"""One r, not two, in "during", "jury", "duration" (Spacedog on Mastodon).

"duration sounds like two words": eSpeak NG's rhotic English writes the r
after the CURE vowel and after START twice, dʊɹɹˈeɪʃən, bˈɑːɹɹɪstə (964 of
the CMU dictionary's words: during, jury, curious, tourist, Europe,
security, insurance, rural, barista, barring...).  English has no long r,
and eSpeak's own voice says one; TGSpeechBox said both, a coda r closing
"dur" and a new one opening "ation", which is a word break.  The misspelled
"durration" came out better because eSpeak spells it by rule, with one r.

The IPA is eSpeak NG's en-us (en-ca uses the same voice).  UK and Australian
English are non-rhotic there (ʊəɹ, one r) and must stay as they are.
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

RHOTIC = {"duration": "dʊɹɹˈeɪʃən", "during": "dˈʊɹɹɪŋ", "jury": "dʒˈʊɹɹi",
          "security": "sᵻkjˈʊɹɹᵻɾi", "barista": "bˈɑːɹɹɪstə", "barring": "bˈɑːɹɹɪŋ"}
R_KEYS = {"ɹ", "ɻ", "r", "ɹ_wf", "ɻ_wf"}  # not ɾ: the flapped t in "security"


def _keys(ipa, lang, speed=1.0):
    return [e["phonemeKey"] for e in tgspeechbox.render(ipa, lang, speed=speed).frame_trace]


@pytest.mark.parametrize("speed", [0.5, 1.0, 1.6, 2.0, 3.0])
@pytest.mark.parametrize("lang", ["en-us", "en-ca"])
@pytest.mark.parametrize("word", list(RHOTIC))
def test_one_r(word, lang, speed):
    keys = _keys(RHOTIC[word], lang, speed)
    rs = [i for i, k in enumerate(keys) if k in R_KEYS]
    assert len(rs) == 1, f"{lang} {word} @{speed}: {keys}"


@pytest.mark.parametrize("word", ["barista", "barring"])
def test_start_still_gets_the_american_ah(word):
    # en-us maps ɑːɹ to ʌɹ (START); the merged r must not slip past it.
    keys = _keys(RHOTIC[word], "en-us")
    assert keys[1] == "ʌ", keys


def test_the_onset_r_is_the_light_one():
    # The r that is left starts the next syllable: the onset ɹ, not coda ɻ.
    keys = _keys(RHOTIC["during"], "en-us")
    assert "ɹ" in keys and "ɻ" not in keys, keys


@pytest.mark.parametrize("lang,ipa", [("en-gb", "djˈʊəɹɪŋ"), ("en-au", "djˈʊəɹɪŋ"), ("en-gb", "bˈɑːɹɪstə")])
def test_non_rhotic_english_unchanged(lang, ipa):
    assert sum(k in R_KEYS for k in _keys(ipa, lang)) == 1

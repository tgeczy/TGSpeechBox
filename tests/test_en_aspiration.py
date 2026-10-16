"""English p, t, k: a puff where English speakers make one, none or less elsewhere.

- After s in the same word (spin, stop, skin, speech, mistake) English stops
  are unaspirated: voicing starts right after the release (#26, nicopn).  The
  packs had an allophone rule that turned the puff's noise down, but the gap
  it left kept its full length, and a silent gap before voicing is heard as
  aspiration all the same.  Now no puff is inserted at all, as in Danish.
  Across a word boundary the stop starts its own word and keeps it: "this
  pin" is not "this spin".
- Before an unstressed vowel inside a word (happy, ticket, apple) English
  aspiration is much shorter than at the start of a stressed syllable
  (Lisker & Abramson 1967; Docherty 1992).  Word-initial stops and stressed
  onsets (pin, police, appear, apply) keep the full puff.

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

import tgspeechbox  # noqa: E402

LANGS = ["en-us", "en-gb", "en-au", "en-ca"]
RATES = [0.5, 1.0, 1.6, 2.0, 3.0]


def _keys(ipa, lang):
    return [e["phonemeKey"] for e in tgspeechbox.render(ipa, lang).frame_trace]


def _puff_ms(ipa, lang, speed=1.0):
    """How long the word's last puff lasts: its frames, up to the next phoneme."""
    u = tgspeechbox.render(ipa, lang, speed=speed)
    tr = u.frame_trace
    for n in range(len(tr) - 1, -1, -1):
        if tr[n]["phonemeKey"] == "h":
            end = tr[n + 1]["frameIndex"] if n + 1 < len(tr) else len(u.frames)
            return sum(f.duration_ms for f in u.frames[tr[n]["frameIndex"]:end])
    return 0.0


@pytest.mark.parametrize("lang", LANGS)
@pytest.mark.parametrize("word,ipa", [("spin", "spˈɪn"), ("stop", "stˈɑːp"), ("skin", "skˈɪn"),
                                      ("speech", "spˈiːtʃ"), ("style", "stˈaɪl"), ("mistake", "mɪstˈeɪk")])
def test_no_puff_after_s(lang, word, ipa):
    assert "h" not in _keys(ipa, lang), f"{lang} {word}: {_keys(ipa, lang)}"


@pytest.mark.parametrize("lang", LANGS)
@pytest.mark.parametrize("word,ipa", [("pin", "pˈɪn"), ("top", "tˈɑːp"), ("kin", "kˈɪn"),
                                      ("appear", "ɐpˈɪɹ"), ("this pin", "ðˈɪs pˈɪn")])
def test_puff_at_word_start_and_stressed_onset(lang, word, ipa):
    assert "h" in _keys(ipa, lang), f"{lang} {word}: {_keys(ipa, lang)}"


@pytest.mark.parametrize("lang", LANGS)
@pytest.mark.parametrize("speed", RATES)
@pytest.mark.parametrize("word,ipa", [("happy", "hˈæpi"), ("ticket", "tˈɪkɪt"), ("apple", "ˈæpəl")])
def test_shorter_puff_before_unstressed_vowel(lang, speed, word, ipa):
    full = _puff_ms("ɐpˈɪɹ", lang, speed)  # appear: stressed onset
    short = _puff_ms(ipa, lang, speed)
    assert 0.0 < short <= 0.65 * full, f"{lang} {word} @{speed}: {short:.1f} vs appear {full:.1f} ms"


@pytest.mark.parametrize("lang", LANGS)
@pytest.mark.parametrize("word,ipa", [("police", "pəlˈiːs"), ("apply", "ɐplˈaɪ")])
def test_full_puff_word_initial_or_stressed_cluster(lang, word, ipa):
    full = _puff_ms("pˈɪn", lang)
    got = _puff_ms(ipa, lang)
    assert got >= 0.7 * full, f"{lang} {word}: {got:.1f} vs pin {full:.1f} ms"

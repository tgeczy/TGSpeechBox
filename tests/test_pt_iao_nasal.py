"""Portuguese -ião keeps its nasal diphthong (#163).

The 3.10 code audit found that "avião", "região" and "opinião" lose the nasal
of their last syllable: the rule in packs/lang/pt.yaml that lengthens the i
before -ão grows its match, and a rule that grows protects its output from
the rules after it, so ɐ̃ʊ̃ never reached the nasal diphthong rules and came
out as an oral ɐ and ʊ.  A native Brazilian listener heard nothing wrong.

Both were right.  It happens only when the IPA has no tie bar in the
diphthong (ˌaviˈɐ̃ʊ̃).  With the tie bar (ˌaviˈɐ̃͡ʊ̃) the same rule matches
without growing and the nasal rules run.  The NVDA add-on and the SAPI
engine have always asked eSpeak NG for tie bars, and since 3.10 release
candidate 4 the Android and iOS apps, the Speech Dispatcher module and
tgsbRender do too (tests/test_espeak_ipa_request.py holds every host to
that request).  So no shipped host loses the nasal now; through release
candidate 3.1 the phones and Linux did.

This holds what the shipped hosts play, at every rate.  The words are as
eSpeak NG 1.53 writes them when asked the way the hosts ask.  IPA without
tie bars (another phonemizer, through the Python package) still loses the
nasal; that part of #163 is open.
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
# língua: (the nasal vowel's key, the offglide's key, the words)
LANGS = {
    # eSpeak NG 1.53, voice pt-br: região, avião, religião, opinião, união, pião
    "pt-br": ("Ã", "w", ["xˌeʒiˈɐ̃͡ʊ̃", "ˌaviˈɐ̃͡ʊ̃", "xˌeliʒiˈɐ̃͡ʊ̃", "ˌopiniˈɐ̃͡ʊ̃", "ˌuniˈɐ̃͡ʊ̃", "piˈɐ̃͡ʊ̃"]),
    # eSpeak NG 1.53, voice pt: the same words, and leão, which is "lião" there
    "pt-pt": ("ã", "ᴜ", ["ʁˌeʒiˈɐ̃͡ʊ̃", "ˌɐviˈɐ̃͡ʊ̃", "ʁˌeliʒiˈɐ̃͡ʊ̃", "ˌɔpiniˈɐ̃͡ʊ̃", "ˌuniˈɐ̃͡ʊ̃", "piˈɐ̃͡ʊ̃", "liˈɐ̃͡ʊ̃"]),
    "pt": ("ã", "ᴜ", ["ʁˌeʒiˈɐ̃͡ʊ̃", "ˌɐviˈɐ̃͡ʊ̃", "ʁˌeliʒiˈɐ̃͡ʊ̃", "ˌɔpiniˈɐ̃͡ʊ̃", "ˌuniˈɐ̃͡ʊ̃", "piˈɐ̃͡ʊ̃", "liˈɐ̃͡ʊ̃"]),
}
# The same diphthong where no i comes before it, which the audit found right.
REFERENCE = "pˈɐ̃͡ʊ̃"  # pão


def _sounds(fe, ipa, speed):
    """(phoneme key, highest nasal coupling of its frames) for each sound."""
    frames = fe.frames(ipa, speed=speed)
    trace = fe.frame_trace()
    starts = [e["frameIndex"] for e in trace] + [len(frames)]
    out = []
    for e, a, b in zip(trace, starts, starts[1:]):
        nasal = max((f.params["caNP"] for f in frames[a:b] if f.params is not None), default=0.0)
        out.append((e["phonemeKey"], nasal))
    return out


@pytest.mark.parametrize("speed", RATES)
@pytest.mark.parametrize("lang", list(LANGS))
def test_iao_ends_in_the_nasal_diphthong(lang, speed):
    vowel, glide, words = LANGS[lang]
    with Frontend(lang) as fe:
        reference = _sounds(fe, REFERENCE, speed)[-2:]
        assert [k for k, _ in reference] == [vowel, glide], f"{lang} pão @{speed}: {reference}"
        assert reference[0][1] > 0.0, f"{lang} pão @{speed}: the vowel is not nasal"
        for ipa in words:
            sounds = _sounds(fe, ipa, speed)
            where = f"{lang} {ipa} @{speed}: {sounds}"
            assert [k for k, _ in sounds[-2:]] == [vowel, glide], where
            assert sounds[-2][1] == pytest.approx(reference[0][1]), where
            assert sounds[-3][0] == "i", where


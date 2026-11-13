"""Hungarian cs before s, and c before sz, keep the affricate (#179).

"bölcsség" is cs + s: eSpeak NG writes bˈølt͡ʃʃeːɡ.  hu.yaml merges a doubled
fricative into one long one (ʃʃ -> ʃː, ss -> sː, ʒʒ -> ʒː, zz -> zː), and a
rule of two or more characters is matched with tie bars optional.  The match
could start ON the tie bar of t͡ʃ: "͡ʃʃ" was taken for "ʃʃ", the tie bar went
out with it, and the word was said with a plain t and a long ʃ.  The guard
that keeps a rule from splitting an affricate only looked for a tie bar
before the match, not at its first position.

Without tie bars (bˈøltʃʃeːɡ) the same word always kept its affricate, so
the two spellings are compared here.

The IPA is eSpeak NG 1.53's for each word, asked for with tie bars; the two
marked "made up" are not words.
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

SPEEDS = [0.5, 1.0, 1.6, 2.0, 3.0]
TIE = "͡"

# word: (IPA, the affricate, the stop it fell apart into)
WORDS = {
    "bölcsség": ("bˈølt͡ʃʃeːɡ", "t͡ʃ", "t"),
    "kicsség": ("kˈit͡ʃʃeːɡ", "t͡ʃ", "t"),
    "ácsság": ("ˈaːt͡ʃʃaːɡ", "t͡ʃ", "t"),
    "nyolcszor": ("ɲˈolt͡ssor", "t͡s", "t"),
    "kilencszáz": ("kˈilɛnt͡ssaːz", "t͡s", "t"),
    "harmincszor": ("hˈɑrmint͡ssor", "t͡s", "t"),
    "ketrecszerű": ("kˈɛtrɛt͡ssɛryː", "t͡s", "t"),
    "(made up) dzs + zs": ("ɒd͡ʒʒɒ", "d͡ʒ", "d"),
    "(made up) dz + z": ("ɒd͡zzɒ", "d͡z", "d"),
}

# Nothing here has an affricate before its own fricative; the keys are what
# they were before the fix.
UNCHANGED = {
    # A doubled fricative with no affricate before it still becomes one long one.
    "kisebb": ("kˈiʃʃɛbː", ["k", "i_hu", "ʃ", "ɛ_hu", "b", "b", "b", "b"]),
    "hosszú": ("hˈossuː", ["h", "o", "s", "ú"]),
    "rozzsal": ("rˈoʒʒɑl", ["r", "o", "ʒ", "ᴒ", "l"]),
    # An affricate before a different fricative, a long one, and two in a row.
    "bohócság": ("bˈohoːt͡sʃaːɡ", ["b", "o", "ɦ", "ó", "t͡s", "t͡s", "ʃ", "á", "ɡ", "ɡ"]),
    "tanácsszerű": ("tˈɑnaːt͡ʃsɛryː", ["t", "ᴒ", "n", "á", "t͡ʃ", "t͡ʃ", "s", "ɛ_hu", "r", "ű"]),
    "lefedettség": ("lˈɛfɛdɛt͡ʃːeːɡ", ["l", "ɛ_hu", "f", "ɛ_hu", "d", "d", "ɛ_hu", "t͡ʃ", "t͡ʃ", "e_hu", "ɡ", "ɡ"]),
    "edzzen": ("ˈɛd͡zːzɛn", ["ɛ_hu", "d͡z", "d͡z", "z", "ɛ_hu", "n"]),
    "kulcscsomó": ("kˈult͡ʃt͡ʃomoː", ["k", "u_hu", "l", "t͡ʃ", "t͡ʃ", "t͡ʃ", "t͡ʃ", "o", "m", "ó"]),
    "csütörtök": ("t͡ʃˈytørtøk", ["t͡ʃ", "y", "t", "t", "ø", "r", "t", "t", "ø", "k", "k"]),
    "bridzs": ("brˈid͡ʒ", ["b", "r", "i_hu", "d͡ʒ", "d͡ʒ"]),
}


def _keys(lang, ipa, speed):
    with Frontend(lang) as fe:
        fe.frames(ipa, speed)
        return [e["phonemeKey"] for e in fe.frame_trace()]


@pytest.mark.parametrize("speed", SPEEDS)
@pytest.mark.parametrize("word", list(WORDS))
def test_the_affricate_is_kept(word, speed):
    ipa, affricate, stop = WORDS[word]
    keys = _keys("hu", ipa, speed)
    assert affricate in keys, f"{word} at {speed}: {keys}"
    fricative = affricate[-1]
    for a, b in zip(keys, keys[1:]):
        assert not (a == stop and b == fricative), f"{word} at {speed}: {stop} + {fricative} in {keys}"


@pytest.mark.parametrize("speed", SPEEDS)
@pytest.mark.parametrize("word", list(WORDS))
def test_tied_and_untied_spellings_agree(word, speed):
    ipa = WORDS[word][0]
    assert _keys("hu", ipa, speed) == _keys("hu", ipa.replace(TIE, ""), speed)


@pytest.mark.parametrize("speed", SPEEDS)
@pytest.mark.parametrize("word", list(UNCHANGED))
def test_other_words_are_what_they_were(word, speed):
    ipa, want = UNCHANGED[word]
    assert _keys("hu", ipa, speed) == want


@pytest.mark.parametrize("speed", SPEEDS)
@pytest.mark.parametrize("lang,ipa,affricate", [
    # The issue: these kept the affricate for the same input, and still do.
    ("pl", "bˈølt͡ʃʃeːɡ", "t͡ʂ"),
    ("de", "bˈølt͡ʃʃeːɡ", "t͡ʃ"),
    ("it", "bˈølt͡ʃʃeːɡ", "t͡ʃ"),
    ("en-us", "bˈølt͡ʃʃeːɡ", "t͡ʃ"),
])
def test_other_languages_keep_the_affricate_as_before(lang, ipa, affricate, speed):
    assert affricate in _keys(lang, ipa, speed)


@pytest.mark.parametrize("speed", SPEEDS)
@pytest.mark.parametrize("ipa,want", [
    # Danish: eSpeak NG ties the stød to its vowel, and da.yaml's ɑj -> aj
    # has always matched from that tie bar.  It still does: a tie bar with a
    # vowel after it is not an affricate's.
    ("bˈ?͡ɑj", ["b", "ˀ", "a", "j"]),
    ("fˈ?͡ɑjt", ["f", "ˀ", "a", "j", "t", "t"]),
    ("hˈʔ͡enəs", ["h", "ˀ", "e", "n", "ə", "s"]),
])
def test_danish_stod_tie_is_matched_as_before(ipa, want, speed):
    assert _keys("da", ipa, speed) == want

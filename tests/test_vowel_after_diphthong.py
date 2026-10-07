"""A vowel after a diphthong keeps its own syllable (#142).

Seva: in British English, "a small metal resonant sound" on the o of
"going" and "following".  What happened: en-gb's GOAT is one phoneme with
its own glide built in (əw_gb, which ramps from [ə] to [ʊ] by itself), and the
automatic diphthong tying took the [ɪ] of "-ing" for that phoneme's
offglide.  It turned the [ɪ] into [j], and the diphthong collapse folded it
into the GOAT vowel: "going" lost its second syllable and jumped from a
rounded [ʊ] straight into [ŋ].  The phonemes with a glide built in (ɑj, ɑw,
ɔj, əw_gb, ə͡l) are whole diphthongs already; nothing ties to them.

Rendered through the frontend as every platform runs it (the Python
package on this tree's build); the check is that the [ɪ] is still a phoneme
of its own when the frames are made.
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

# eSpeak NG's IPA for each word, as the hosts pass it on.
CASES = [
    ("en-gb", "ɡˈəʊɪŋ"),        # going
    ("en-gb", "fˈɒləʊɪŋ"),      # following
    ("en-gb", "ɡˈəʊɪŋ ˈɒn"),    # going on
    ("en-us", "ɡˈoʊɪŋ"),
    ("en-au", "ɡˈəʊɪŋ"),
    ("en-gb", "bˈaɪɪŋ"),        # buying
    ("en-us", "bˈaɪɪŋ"),
    ("en-gb", "əlˈaʊɪŋ"),       # allowing
    ("en-us", "əlˈaʊɪŋ"),
    ("en-gb", "ɛndʒˈɔɪɪŋ"),     # enjoying
    ("en-us", "ɛndʒˈɔɪɪŋ"),
]


def _phonemes(lang, ipa):
    return [e["phonemeKey"] for e in tgspeechbox.render(ipa, lang).frame_trace]


@pytest.mark.parametrize("lang,ipa", CASES, ids=[f"{l}-{i}" for l, i in CASES])
def test_ing_keeps_its_vowel_after_a_diphthong(lang, ipa):
    keys = _phonemes(lang, ipa)
    ng = keys.index("ŋ")
    assert keys[ng - 1].startswith("ɪ"), f"{ipa} ({lang}) lost the [ɪ] of -ing: {keys}"


def test_a_plain_diphthong_still_collapses():
    # The tying itself still works where it should: GOAT and PRICE on their own.
    assert _phonemes("en-gb", "ɡˈəʊ") == ["ɡ", "əw_gb"]
    assert "ɪ" not in _phonemes("en-us", "pɹˈaɪs")

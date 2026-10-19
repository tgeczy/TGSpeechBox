"""No vowel between l and g in Spanish ("algunos", "algo", "alguien", #149).

Greg: "algunos", "algunas" and "alguien" had an r in "alg".  es.yaml put a
schwa between /l/ and /ɣ/, and since b801 every schwa is shaped as a tap's
echo vowel: 15 ms, colored like the vowel before it.  A vowel that short
between two consonants is what a tap sounds like, so "algo" came out as
"alro".  Whisper large-v2 heard "algo" as "abró" and "alguien" as
"abre bien"; without the schwa it heard "algunos" and "algunas".  Spanish
says [alˈɣunos] with no vowel in between.

The IPA is eSpeak NG's for each word (the same in es and es-419).
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

WORDS = {
    "algunos": "alɣˈunos",
    "algunas": "alɣˈunas",
    "alguien": "ˈalɣjen",
    "algo": "ˈalɣo",
    "salgo": "sˈalɣo",
    "hidalgo": "iðˈalɣo",
    "colgar": "kolɣˈaɾ",
}


def _keys(ipa, lang):
    return [e["phonemeKey"] for e in tgspeechbox.render(ipa, lang).frame_trace]


@pytest.mark.parametrize("lang", ["es", "es-es", "es-mx"])
@pytest.mark.parametrize("word", list(WORDS))
def test_no_vowel_between_l_and_g(lang, word):
    keys = _keys(WORDS[word], lang)
    l = next(i for i, k in enumerate(keys) if k.startswith("l"))
    g = next(i for i, k in enumerate(keys) if i > l and k.startswith(("ɣ", "ɡ")))
    assert g == l + 1, f"{lang} {word}: {keys[l:g + 1]}"

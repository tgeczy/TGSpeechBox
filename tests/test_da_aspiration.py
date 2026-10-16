"""Danish p, t, k are aspirated; b, d, g are not (Fubsepude: "major issues").

In Danish both stop series are voiceless; what tells p/t/k from b/d/g is the
long aspiration after p/t/k, longer than English's (Fischer-Jørgensen 1972,
tape-cutting experiments: "the aspiration is decisive"; the American
English/Danish VOT study).  da.yaml had post-stop aspiration switched off, so
"pære" and "bære", "tale" and "dale" differed only in a burst listeners don't
use.  After s (spiser, stå, skal) Danish stops are unaspirated, as in English.

The IPA is eSpeak NG's for each word.
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


def _keys(ipa):
    return [e["phonemeKey"] for e in tgspeechbox.render(ipa, "da").frame_trace]


@pytest.mark.parametrize("word,ipa", [("pære", "pˈɛʔ͡ʌ"), ("tale", "tˈalə"), ("kaffe", "kˈ?͡ɑfə"), ("Tak", "tˈ?͡ɑk")])
def test_p_t_k_are_aspirated(word, ipa):
    keys = _keys(ipa)
    assert keys[1] == "h", f"{word}: {keys}"


@pytest.mark.parametrize("word,ipa", [("bære", "bˈɛʔ͡ʌ"), ("dale", "dˈalə"), ("gaffel", "ɡˈ?͡ɑfəl"),
                                      ("spiser", "spˈisʔ͡ʌ"), ("stå", "stˈɒ"), ("skal", "skˈal")])
def test_b_d_g_and_stops_after_s_are_not(word, ipa):
    assert "h" not in _keys(ipa), f"{word}: {_keys(ipa)}"

"""Brazilian Portuguese "oi" and the strong r (Cleverson, on Mastodon).

- "oi" ("foi", "boi", "dois") played the English "boy" diphthong: pt-br.yaml
  opens every "o" to "ɔ", so "oj" became "ɔj", the English CHOICE phoneme with
  its own glide built in.  It is now the close o plus the glide, the way "ai"
  and "ei" are a vowel plus j.
- The strong r ("resumo", "carro", "rua"), which eSpeak writes as x, was
  mapped to a breathy "h" and sounded close to "ch"; Cleverson, a native
  speaker, found the throat fricative he wanted in x_es.

Rendered through the frontend with the Python package on this tree's build.
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
    return [e["phonemeKey"] for e in tgspeechbox.render(ipa, "pt-br").frame_trace]


@pytest.mark.parametrize("ipa", ["fˈo͡ɪ", "bˈo͡ɪ", "dˈo͡ɪs"])  # foi, boi, dois (eSpeak NG's IPA)
def test_oi_is_close_o_plus_glide(ipa):
    keys = _keys(ipa)
    assert "ɔj" not in keys, f"{ipa}: the English 'boy' diphthong: {keys}"
    i = keys.index("o_es")
    assert keys[i + 1] == "j", f"{ipa}: {keys}"


@pytest.mark.parametrize("ipa", ["xˌezˈumʊ", "kˈaxʊ", "xˈuɐ"])  # resumo, carro, rua
def test_strong_r_is_the_throat_fricative(ipa):
    keys = _keys(ipa)
    assert "x_es" in keys and "h" not in keys, f"{ipa}: {keys}"


def test_ai_and_ei_unchanged():
    assert _keys("sˈa͡ɪ") == ["s", "a", "j"]
    assert _keys("sˈe͡ɪ") == ["s", "e", "j"]

"""Brazilian Portuguese "oi" and the strong r (Cleverson, on Mastodon).

- "oi" ("foi", "boi", "dois") played the English "boy" diphthong: pt-br.yaml
  opens every "o" to "ɔ", so "oj" became "ɔj", the English CHOICE phoneme with
  its own glide built in.  It is now the close o plus the glide, the way "ai"
  and "ei" are a vowel plus j.
- The strong r ("resumo", "carro", "rua"), which eSpeak writes as x, was
  mapped to a breathy "h" and sounded close to "ch"; Cleverson, a native
  speaker, found the throat fricative he wanted in x_es.  Then the "rr" in
  "carro" was too strong, and he made his own: x_ptbr, x_es voiced
  (voiceAmplitude 0.7) with lighter noise (fricationAmplitude 0.40, was 0.80).

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


@pytest.mark.parametrize("ipa", ["xˌezˈumʊ", "bˈaxʊ", "xˈuɐ"])  # resumo, barro, rua (no voiceless stop: those add an h puff)
def test_strong_r_is_the_throat_fricative(ipa):
    keys = _keys(ipa)
    assert "x_ptbr" in keys and "h" not in keys, f"{ipa}: {keys}"


@pytest.mark.parametrize("speed", [0.5, 1.0, 1.6, 2.0, 3.0])
@pytest.mark.parametrize("ipa", ["xˌezˈumʊ", "kˈaxʊ", "tˈɛxæ", "xˈatʊ"])  # resumo, carro, terra, rato
def test_strong_r_is_voiced_and_lighter(ipa, speed):
    u = tgspeechbox.render(ipa, "pt-br", speed=speed)
    tr = u.frame_trace
    n = next(i for i, e in enumerate(tr) if e["phonemeKey"] == "x_ptbr")
    end = tr[n + 1]["frameIndex"] if n + 1 < len(tr) else len(u.frames)
    body = [f.params for f in u.frames[tr[n]["frameIndex"]:end] if f.params]
    peak = max(body, key=lambda f: f["fricationAmplitude"])
    assert peak["voiceAmplitude"] >= 0.3, f"{ipa} @{speed}: the strong r is voiceless"
    assert peak["fricationAmplitude"] <= 0.5, f"{ipa} @{speed}: the strong r's noise is too strong"


def test_ai_and_ei_unchanged():
    assert _keys("sˈa͡ɪ") == ["s", "a", "j"]
    assert _keys("sˈe͡ɪ") == ["s", "e", "j"]

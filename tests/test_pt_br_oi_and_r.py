"""Brazilian Portuguese "oi" and the strong r (Cleverson, on Mastodon).

- "oi" ("foi", "boi", "dois") played the English "boy" diphthong: pt-br.yaml
  opens every "o" to "ɔ", so "oj" became "ɔj", the English CHOICE phoneme with
  its own glide built in.  It is the close o plus the glide, the way "ai" and
  "ei" are a vowel plus j (Cleverson's report, release candidate 2).  Edu then
  heard "dois" as "doois" (#150), and release candidate 3 made it one gliding
  vowel; in the listening round on #150 (November 2026) all three Brazilian
  listeners turned that one down, and two closer versions of it, for "too
  much like ói instead of ôi", and asked for the close o plus the glide back.
- The strong r ("resumo", "carro", "rua"), which eSpeak writes as x, was
  mapped to a breathy "h" and sounded close to "ch"; Cleverson, a native
  speaker, found the throat fricative he wanted in x_es.  Then the "rr" in
  "carro" was too strong, and he made his own: x_ptbr, x_es with
  lighter noise (fricationAmplitude 0.40, was 0.80).  He voiced it too, and
  "carro" became "cavo" (#150, Edu): it is voiceless, as the literature has it.

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


OI = {"oi": "ˈo͡ɪ", "foi": "fˈo͡ɪ", "boi": "bˈo͡ɪ", "dois": "dˈo͡ɪs", "pois": "pˈo͡ɪs"}  # eSpeak NG's IPA
TIE = chr(0x361)


@pytest.mark.parametrize("speed", [0.5, 1.0, 1.6, 2.0, 3.0])
@pytest.mark.parametrize("tied", [True, False])
@pytest.mark.parametrize("word", list(OI))
def test_oi_is_the_close_o_plus_the_glide(word, tied, speed):
    # The version the listeners on #150 chose (Cleverson, Thiago, and Edu's
    # "leave it as it was"): ôi, not the English "boy" and not ói.
    ipa = OI[word] if tied else OI[word].replace(TIE, "")
    u = tgspeechbox.render(ipa, "pt-br", speed=speed)
    keys = [e["phonemeKey"] for e in u.frame_trace]
    assert "ɔj" not in keys, f"{word} @{speed}: the English 'boy' diphthong: {keys}"
    n = keys.index("o_es")
    assert keys[n + 1] == "j", f"{word} @{speed}: {keys}"
    onset = u.frames[u.frame_trace[n]["frameIndex"]].params
    assert onset["cf1"] <= 500, f"{word} @{speed}: oi starts open ({onset['cf1']:.0f} Hz), like 'boy'"


@pytest.mark.parametrize("ipa", ["xˌezˈumʊ", "bˈaxʊ", "xˈuɐ"])  # resumo, barro, rua (no voiceless stop: those add an h puff)
def test_strong_r_is_the_throat_fricative(ipa):
    keys = _keys(ipa)
    assert "x_ptbr" in keys and "h" not in keys, f"{ipa}: {keys}"


@pytest.mark.parametrize("speed", [0.5, 1.0, 1.6, 2.0, 3.0])
@pytest.mark.parametrize("ipa", ["xˌezˈumʊ", "kˈaxʊ", "tˈɛxæ", "xˈatʊ"])  # resumo, carro, terra, rato
def test_strong_r_is_voiceless_and_lighter(ipa, speed):
    # #150 (Edu): voiced, "carro" was "cavo".  Between vowels and at the start
    # of a word the Brazilian strong r is voiceless, [x] or [h] ("carro"
    # [ˈkahu], Barbosa & Albano 2004); eSpeak NG writes x only there (a coda r
    # before a consonant is ɾ), so x_ptbr is never in the voiced context.
    # Cleverson's lighter noise stays: x_es's 0.80 was too strong.
    u = tgspeechbox.render(ipa, "pt-br", speed=speed)
    tr = u.frame_trace
    n = next(i for i, e in enumerate(tr) if e["phonemeKey"] == "x_ptbr")
    end = tr[n + 1]["frameIndex"] if n + 1 < len(tr) else len(u.frames)
    body = [f.params for f in u.frames[tr[n]["frameIndex"]:end] if f.params]
    peak = max(body, key=lambda f: f["fricationAmplitude"])
    assert peak["voiceAmplitude"] <= 0.1, f"{ipa} @{speed}: the strong r is voiced ({peak['voiceAmplitude']:.2f}), a v"
    assert 0.2 <= peak["fricationAmplitude"] <= 0.5, f"{ipa} @{speed}: the strong r's noise is {peak['fricationAmplitude']:.2f}"


def test_ai_and_ei_unchanged():
    assert _keys("sˈa͡ɪ") == ["s", "a", "j"]
    assert _keys("sˈe͡ɪ") == ["s", "e", "j"]

"""Brazilian Portuguese "oi" and the strong r (Cleverson, on Mastodon).

- "oi" ("foi", "boi", "dois") played the English "boy" diphthong: pt-br.yaml
  opens every "o" to "ɔ", so "oj" became "ɔj", the English CHOICE phoneme with
  its own glide built in.  It is now the close o plus the glide, the way "ai"
  and "ei" are a vowel plus j.
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


@pytest.mark.parametrize("ipa", ["fˈo͡ɪ", "bˈo͡ɪ", "dˈo͡ɪs"])  # foi, boi, dois (eSpeak NG's IPA)
def test_oi_starts_on_the_close_o(ipa):
    u = tgspeechbox.render(ipa, "pt-br")
    keys = [e["phonemeKey"] for e in u.frame_trace]
    assert "ɔj" not in keys, f"{ipa}: the English 'boy' diphthong: {keys}"
    n = keys.index("oj_ptbr")
    onset = u.frames[u.frame_trace[n]["frameIndex"]].params
    assert onset["cf1"] <= 500, f"{ipa}: oi starts open ({onset['cf1']:.0f} Hz), like 'boy'"


@pytest.mark.parametrize("speed", [0.5, 1.0, 1.6, 2.0, 3.0])
@pytest.mark.parametrize("ipa", ["dˈo͡ɪs", "pˈo͡ɪs"])  # dois, pois
def test_oi_is_one_diphthong_not_a_held_o(ipa, speed):
    # #150 (Edu): "dois" sounded like "doois".  A full o and then a separate
    # glide is a long o; "oi" is one gliding vowel, about as long as "ɔj" was.
    u = tgspeechbox.render(ipa, "pt-br", speed=speed)
    keys = [e["phonemeKey"] for e in u.frame_trace]
    assert "j" not in keys, f"{ipa} @{speed}: o and a separate glide: {keys}"
    ref = tgspeechbox.render(ipa.replace("o͡ɪ", "a"), "pt-br", speed=speed)
    tr = u.frame_trace
    n = keys.index("oj_ptbr")
    ms = sum(f.duration_ms for f in u.frames[tr[n]["frameIndex"]:tr[n + 1]["frameIndex"]])
    rtr = ref.frame_trace
    m = [e["phonemeKey"] for e in rtr].index("a")
    ref_ms = sum(f.duration_ms for f in ref.frames[rtr[m]["frameIndex"]:rtr[m + 1]["frameIndex"]])
    assert ms <= 1.25 * ref_ms, f"{ipa} @{speed}: oi {ms:.0f} ms vs a plain vowel {ref_ms:.0f} ms"


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

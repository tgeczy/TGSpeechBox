"""The Spanish tap is voiced, not a t ("seguro" said as "seguto", #149).

Greg: "seguro" and "segura" sounded like "seguto", "seguta".  A tap is one
quick touch of the tongue and builds up almost no air pressure, so it has
little or no release noise (Ladefoged & Maddieson 1996).  The tap carried
release noise at 0.25 from the start, under full voicing; since b801 the
tap's voicing dips to 0.55 (the intensity dip, its main cue), and the noise
left standing at nearly half the voicing reads as a t.  Whisper large-v2
heard "toro", "coro" as "todo" and "caro" as "cae"; with the noise at 0.10
it hears "toró" and "cargó", and still every "otra" in the carrier.

The word-final tap (ɾ_wf) keeps its release: before silence it needs one
(#53).  The IPA is eSpeak NG's.
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

WORDS = {"seguro": "seɣˈuɾo", "segura": "seɣˈuɾa", "puro": "pˈuɾo", "toro": "tˈoɾo",
         "pero": "pˈeɾo", "otra": "ˈotɾa", "primero": "pɾimˈeɾo"}


def _tap_frames(ipa, lang, speed):
    u = tgspeechbox.render(ipa, lang, speed=speed)
    tr = u.frame_trace
    out = []
    for n, e in enumerate(tr):
        if e["phonemeKey"] == "ɾ":
            end = tr[n + 1]["frameIndex"] if n + 1 < len(tr) else len(u.frames)
            out += [f.params for f in u.frames[e["frameIndex"]:end]]
    assert out, f"no tap in {ipa}"
    return out


def _p(f, k):
    return f[k] if isinstance(f, dict) else getattr(f, k)


@pytest.mark.parametrize("lang", ["es", "es-es", "es-mx"])
@pytest.mark.parametrize("speed", [0.5, 1.0, 1.6, 2.0, 3.0])
@pytest.mark.parametrize("word", list(WORDS))
def test_tap_noise_stays_well_under_its_voicing(lang, speed, word):
    for f in _tap_frames(WORDS[word], lang, speed):
        va, fa = _p(f, "voiceAmplitude"), _p(f, "fricationAmplitude")
        assert va > 0.3, f"{lang} {word} @{speed}: tap lost its voicing ({va:.2f})"
        assert fa <= 0.3 * va, f"{lang} {word} @{speed}: tap noise {fa:.2f} vs voicing {va:.2f}"


def test_the_word_final_tap_keeps_its_release():
    u = tgspeechbox.render("amˈoɾ", "es")
    assert any(e["phonemeKey"] == "ɾ_wf" for e in u.frame_trace)
    assert max(_p(f.params, "fricationAmplitude") for f in u.frames if f.params) >= 0.15

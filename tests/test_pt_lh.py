"""Portuguese "lh" is a lateral, not an "i" (#152, Edu).

"batalha" sounded like "bataia".  eSpeak NG writes "lh" as lj (Brazilian)
or ʎ (European), and both became our ʎ: 30 ms with a 20 ms fade in and the
next vowel's 25 ms fade out of it, so it never held its lateral shape and
glided through like the "i" of "bataia".  That "i" pronunciation is a real
one, but a stigmatized rural one that educated speakers avoid (Oliveira et
al. 2021, on "mulé"/"muié").

Brazilian /ʎ/ is a palatalized lateral, one sound, not l plus a glide, and
listeners confuse it with the glide j more than with anything else (Wong
2017, 10 São Paulo speakers); what keeps it apart is the lateral's coronal
contact (Charles et al. 2018), and what keeps that audible is its duration
(Owens 2023: a longer lateral resists the merger into j).  So ʎ_pt is ʎ's
shape held for longer: durationScale on a liquid, honoured since this test.

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

SPEEDS = [0.5, 1.0, 1.6, 2.0, 3.0]
# eSpeak NG's IPA: Brazilian writes "lh" as lj, European as ʎ.
LH = {"pt-br": ["bˌatˈaljæ", "fˈoljæ", "trˌabˈaljʊ", "fˈiljʊ"],  # batalha, folha, trabalho, filho
      "pt-pt": ["bˌɐtˈaʎɐ", "fˈoʎɐ", "fˈiʎʊ"]}
BALA = {"pt-br": "bˈalæ", "pt-pt": "bˈalɐ"}  # the plain l in the same frame


def _segments(ipa, lang, speed):
    u = tgspeechbox.render(ipa, lang, speed=speed)
    tr = u.frame_trace
    segs = []
    for n, e in enumerate(tr):
        end = tr[n + 1]["frameIndex"] if n + 1 < len(tr) else len(u.frames)
        segs.append((e["phonemeKey"], sum(f.duration_ms for f in u.frames[e["frameIndex"]:end])))
    return segs


def _ms(segs, key):
    return next(ms for k, ms in segs if k == key)


@pytest.mark.parametrize("lang,ipa", [(lang, w) for lang, ws in LH.items() for w in ws])
def test_lh_is_the_portuguese_lateral(lang, ipa):
    keys = [k for k, _ in _segments(ipa, lang, 1.0)]
    assert "ʎ_pt" in keys, f"{lang} {ipa}: {keys}"
    assert "j" not in keys and "ʎ" not in keys, f"{lang} {ipa}: {keys}"


@pytest.mark.parametrize("speed", SPEEDS)
@pytest.mark.parametrize("lang,ipa", [(lang, w) for lang, ws in LH.items() for w in ws])
def test_lh_holds_longer_than_a_plain_l(lang, ipa, speed):
    # The lateral needs time to be heard as one (Owens 2023).  At 30 ms it
    # was all fade; twice the plain l leaves it a held middle at every rate.
    lh = _ms(_segments(ipa, lang, speed), "ʎ_pt")
    l = _ms(_segments(BALA[lang], lang, speed), "l")
    assert lh >= 1.8 * l, f"{lang} {ipa} @{speed}: lh {lh:.0f} ms vs l {l:.0f} ms"


@pytest.mark.parametrize("speed", SPEEDS)
def test_lh_keeps_the_word_from_stretching(speed):
    # A longer lh, not a slower word: "batalha" grows by the lh alone.
    plain = sum(ms for _, ms in _segments("bˌatˈalæ", "pt-br", speed))
    lh = sum(ms for _, ms in _segments("bˌatˈaljæ", "pt-br", speed))
    l = _ms(_segments("bˌatˈalæ", "pt-br", speed), "l")
    assert lh - plain <= 1.5 * l, f"@{speed}: batalha {lh:.0f} ms vs batala {plain:.0f} ms"


@pytest.mark.parametrize("speed", SPEEDS)
def test_spanish_ll_keeps_its_timing(speed):
    # Only a liquid that asks for a durationScale changes.  Spanish ʎ_es
    # (Castilian "calle") keeps the plain voiced-consonant length of its l.
    ll = _ms(_segments("kˈaʎe", "es", speed), "ʎ_es")
    l = _ms(_segments("ˈala", "es", speed), "l_es")
    assert ll == pytest.approx(l, abs=0.1), f"es @{speed}: ʎ_es {ll:.1f} ms vs l_es {l:.1f} ms"

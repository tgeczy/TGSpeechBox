"""Portuguese "lh" is a lateral, not an "i" (#152, Edu).

"batalha" sounded like "bataia".  eSpeak NG writes "lh" as lj (Brazilian)
or ʎ (European), and both became our ʎ: 30 ms with a 20 ms fade in and the
next vowel's 25 ms fade out of it, so it never held its lateral shape and
glided through like the "i" of "bataia".  That "i" pronunciation is a real
one, but a stigmatized rural one that educated speakers avoid (Oliveira et
al. 2021, on "mulé"/"muié").

European Portuguese /ʎ/ is one palatal lateral, and what keeps it apart from
the glide j is the lateral's coronal contact (Charles et al. 2018) and its
duration (Owens 2023: a longer lateral resists the merger into j).  So ʎ_pt
is ʎ's shape held for longer: durationScale on a liquid, honoured since this
test.

Brazilian Portuguese went the other way.  Held long, the single palatal still
sounded "soft" to Brazilian listeners next to eSpeak NG and RHVoice's Letícia
(#152, Thiago), and eSpeak NG's own Brazilian "lh" is a full l followed by the
glide j.  In the listening round on #150 (November 2026) all three Brazilian
listeners chose that one, so pt-br keeps eSpeak's l + j as it is written.

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
# eSpeak NG's IPA: Brazilian "lh" is lj in 1.52 and ʎ in 1.53; European is ʎ.
LH = {"pt-br": ["bˌatˈaljæ", "fˈoljæ", "trˌabˈaljʊ", "fˈiljʊ",   # batalha, folha, trabalho, filho (1.52)
                "bˌatˈaʎɐ", "fˈoʎɐ", "tɾˌabˈaʎʊ", "fˈiʎʊ"],      # the same words from 1.53
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


@pytest.mark.parametrize("speed", SPEEDS)
@pytest.mark.parametrize("ipa", LH["pt-br"])
def test_brazilian_lh_is_a_full_l_then_the_glide(ipa, speed):
    # The listeners' choice: eSpeak NG's l + j, the l as long as any other l.
    segs = _segments(ipa, "pt-br", speed)
    keys = [k for k, _ in segs]
    n = keys.index("l")
    assert keys[n + 1] == "j", f"pt-br {ipa} @{speed}: {keys}"
    assert "ʎ_pt" not in keys and "ʎ" not in keys, f"pt-br {ipa} @{speed}: {keys}"
    plain = _ms(_segments(BALA["pt-br"], "pt-br", speed), "l")
    assert segs[n][1] == pytest.approx(plain, rel=0.25), f"pt-br {ipa} @{speed}: l {segs[n][1]:.0f} ms vs {plain:.0f} ms"


@pytest.mark.parametrize("speed", SPEEDS)
@pytest.mark.parametrize("old,new", [("fˈiljʊ", "fˈiʎʊ"), ("ˈɔljʊ", "ˈɔʎʊ"), ("muljˈɛɾ", "muʎˈɛɾ")])
def test_brazilian_lh_is_the_same_from_either_espeak(old, new, speed):
    # filho, olho, mulher: eSpeak NG 1.52's spelling and 1.53's.
    assert _segments(old, "pt-br", speed) == _segments(new, "pt-br", speed)


@pytest.mark.parametrize("ipa", ["fˌæmˈiljæ", "ˌitˈaljæ"])  # família, Itália
def test_li_before_a_vowel_is_not_lh(ipa):
    for lang in ("pt-br", "pt-pt"):
        keys = [k for k, _ in _segments(ipa, lang, 1.0)]
        assert "ʎ_pt" not in keys, f"{lang} {ipa}: {keys}"


@pytest.mark.parametrize("ipa", LH["pt-pt"])
def test_european_lh_is_the_portuguese_lateral(ipa):
    keys = [k for k, _ in _segments(ipa, "pt-pt", 1.0)]
    assert "ʎ_pt" in keys, f"pt-pt {ipa}: {keys}"
    assert "j" not in keys and "ʎ" not in keys, f"pt-pt {ipa}: {keys}"


@pytest.mark.parametrize("speed", SPEEDS)
@pytest.mark.parametrize("ipa", LH["pt-pt"])
def test_european_lh_holds_longer_than_a_plain_l(ipa, speed):
    # The lateral needs time to be heard as one (Owens 2023).  At 30 ms it
    # was all fade; twice the plain l leaves it a held middle at every rate.
    lh = _ms(_segments(ipa, "pt-pt", speed), "ʎ_pt")
    l = _ms(_segments(BALA["pt-pt"], "pt-pt", speed), "l")
    assert lh >= 1.8 * l, f"pt-pt {ipa} @{speed}: lh {lh:.0f} ms vs l {l:.0f} ms"


@pytest.mark.parametrize("speed", SPEEDS)
def test_spanish_ll_keeps_its_timing(speed):
    # Only a liquid that asks for a durationScale changes.  Spanish ʎ_es
    # (Castilian "calle") keeps the plain voiced-consonant length of its l.
    ll = _ms(_segments("kˈaʎe", "es", speed), "ʎ_es")
    l = _ms(_segments("ˈala", "es", speed), "l_es")
    assert ll == pytest.approx(l, abs=0.1), f"es @{speed}: ʎ_es {ll:.1f} ms vs l_es {l:.1f} ms"

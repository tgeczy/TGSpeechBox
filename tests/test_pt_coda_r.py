"""Portuguese r before a consonant has no vowel after it (#156, Edu).

"porta", "aguarde", "inferno", "português" sounded "like a Japanese person
trying to pronounce these words": pó-rə-ta.  eSpeak NG's Portuguese gives the
weak r before a consonant a very short schwa (ph_pt_brazil: `AppendPhoneme(@-)`,
"very short schwa used along with weak /r/ only", length 20, non-syllabic).
Older eSpeak builds, the one NVDA went back to in its 2026.3 bug-fix release
among them, print that schwa in the IPA (pˈɔɾətæ); newer ones print nothing
(`ipa NULL`, eSpeak 932fe546, January 2026).  We played the printed ə as a full
schwa, a vowel of its own after the r: the epenthetic vowel Japanese speakers
put after a coda consonant.

It is not a vowel in either Portuguese: the coda r closes the syllable.  The
language pack drops the printed schwa, so TGSpeechBox says these words the same
whichever eSpeak spells them, and the same as with the newer spelling Edu
preferred.  Rendered through the frontend with the Python package on this
tree's build.
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
from tgspeechbox.engine import Frontend  # noqa: E402

SPEEDS = [0.5, 1.0, 1.6, 2.0, 3.0]

# eSpeak NG 1.52's IPA (printed schwa) for Edu's words and a few more.
OLD = {
    "pt-br": {"porta": "pˈɔɾətæ", "aguarde": "ˌaɡwˈaɾədʒy", "inferno": "ˌiŋfˈɛɾənʊ",
              "português": "pˌoɾətuɡˈes", "Portugal": "pˌoɾətuɡˈaʊ", "porteiro": "pˌoɾətˈeɪɾʊ",
              "verde": "vˈeɾədʒy", "carta": "kˈaɾətæ", "perto": "pˈɛɾətʊ", "porque": "pˈoɾəky"},
    "pt-pt": {"porta": "pˈɔɾətɐ", "verde": "vˈeɾədɨ", "carta": "kˈaɾətɐ", "perto": "pˈɛɾətʊ"},
}
LANGS = [(lang, word) for lang, words in OLD.items() for word in words]


def _new_spelling(ipa):
    # What eSpeak writes once the short schwa prints nothing.
    return ipa.replace("ɾə", "ɾ")


def _plain(d):
    # FrameEx marks unused end targets NaN, and NaN != NaN.
    return None if d is None else {k: (None if v != v else v) for k, v in d.items()}


def _frames(ipa, lang, speed):
    with Frontend(lang) as fe:
        return [(_plain(f.params), _plain(f.ex), f.duration_ms, f.fade_ms)
                for f in fe.frames(ipa, speed=speed)]


def _keys(ipa, lang, speed=1.0):
    return [e["phonemeKey"] for e in tgspeechbox.render(ipa, lang, speed=speed).frame_trace]


@pytest.mark.parametrize("speed", SPEEDS)
@pytest.mark.parametrize("lang,word", LANGS)
def test_no_vowel_after_the_r(lang, word, speed):
    keys = _keys(OLD[lang][word], lang, speed)
    assert "ə" not in keys, f"{lang} {word} @{speed}: {keys}"


@pytest.mark.parametrize("speed", SPEEDS)
@pytest.mark.parametrize("lang,word", LANGS)
def test_either_espeak_spelling_sounds_the_same(lang, word, speed):
    old = OLD[lang][word]
    assert _frames(old, lang, speed) == _frames(_new_spelling(old), lang, speed), (
        f"{lang} {word} @{speed}: eSpeak's printed short schwa changes the sound")


@pytest.mark.parametrize("lang", ["pt-br", "pt-pt"])
def test_the_r_is_still_there(lang):
    for word, ipa in OLD[lang].items():
        keys = _keys(ipa, lang)
        assert any(k.startswith("ɾ") for k in keys), f"{lang} {word}: {keys}"

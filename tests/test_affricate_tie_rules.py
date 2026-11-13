"""A tied affricate reaches the language's own rule for it (#189).

Spanish "ch" and Polish "cz" and "dż" played the shared base sounds on
Android, iOS, macOS and Linux: those hosts asked eSpeak NG for IPA without
tie bars (mˈutʃo), default.yaml then tied the pair itself (tʃ -> t͡ʃ), and the
output of a rule that grows the text is hidden from later rules, so
es.yaml's t͡ʃ -> t͡ʃ_es and pl.yaml's t͡ʃ -> t͡ʂ, d͡ʒ -> d͡ʐ never saw it.

Every host now asks for the tied form, as the NVDA add-on always did
(tests/test_espeak_ipa_request.py), and the tied form never went through
default.yaml's rule.  This file pins the other half: for the IPA eSpeak NG
writes with tie bars, the rules the issue names do fire, at every rate.

The IPA is eSpeak NG 1.53's, asked for with tie bars.
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

from tgspeechbox.engine import Frontend  # noqa: E402

SPEEDS = [0.5, 1.0, 1.6, 2.0, 3.0]

# (language, word, IPA, the affricate that must play, the base ones that must not)
CASES = [
    ("es", "mucho", "mˈut͡ʃo", "t͡ʃ_es", ("t͡ʃ",)),
    ("es", "chico", "t͡ʃˈiko", "t͡ʃ_es", ("t͡ʃ",)),
    ("es", "noche", "nˈot͡ʃe", "t͡ʃ_es", ("t͡ʃ",)),
    ("es-mx", "mucho", "mˈut͡ʃo", "t͡ʃ_es", ("t͡ʃ",)),
    ("es-mx", "chico", "t͡ʃˈiko", "t͡ʃ_es", ("t͡ʃ",)),
    ("es-es", "mucho", "mˈut͡ʃo", "t͡ʃ_es", ("t͡ʃ",)),
    ("pl", "czas", "t͡ʃˈas", "t͡ʂ", ("t͡ʃ",)),
    ("pl", "czy", "t͡ʃˈɨ", "t͡ʂ", ("t͡ʃ",)),
    ("pl", "wszcząć", "fʃt͡ʃˈɔɲt͡ɕ", "t͡ʂ", ("t͡ʃ",)),
    ("pl", "dżem", "d͡ʒˈɛm", "d͡ʐ", ("d͡ʒ",)),
    ("pl", "dżungla", "d͡ʒˈuŋɡla", "d͡ʐ", ("d͡ʒ",)),
    ("pl", "challenge", "t͡ʃˈɛlɛnd͡ʒ", "t͡ʂ", ("t͡ʃ", "d͡ʒ")),
]


def _keys(lang, ipa, speed):
    with Frontend(lang) as fe:
        fe.frames(ipa, speed)
        return [e["phonemeKey"] for e in fe.frame_trace()]


@pytest.mark.parametrize("speed", SPEEDS)
@pytest.mark.parametrize("lang,word,ipa,want,never", CASES,
                         ids=[f"{c[0]}-{c[1]}" for c in CASES])
def test_tied_affricate_is_the_languages_own(lang, word, ipa, want, never, speed):
    keys = _keys(lang, ipa, speed)
    assert want in keys, f"{word} ({lang}) at {speed}: {keys}"
    for base in never:
        assert base not in keys, f"{word} ({lang}) at {speed}: base {base} in {keys}"


@pytest.mark.parametrize("speed", SPEEDS)
def test_polish_dz_with_dot_in_challenge(speed):
    # Both of the word's affricates, in order: cz at the start, dż at the end.
    keys = [k for k in _keys("pl", "t͡ʃˈɛlɛnd͡ʒ", speed) if "͡" in k]
    assert keys[0] == "t͡ʂ" and keys[-1] == "d͡ʐ", keys

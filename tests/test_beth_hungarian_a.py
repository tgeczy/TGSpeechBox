"""Beth's Hungarian á has its own third and fourth formant.

Beth's class scales raise a vowel's F3 by 10% and lower its F4 and F5, which
put á's F3 at 3025 Hz beside F4 and F5 (about 3290 and 3500 Hz): one hard peak
near 3 kHz, where hearing is most sensitive.  The maintainer, a native
speaker, called it ear-splitting, more so once her vowels kept their own
formants after a consonant (#173), and chose a lower, wider F3 and a wider F4
by ear from two candidates.  It is a phonemeOverride in Beth's profile: only
her á, in the one language that has that phoneme.
"""
from __future__ import annotations

import os
import pathlib
import shutil
import sys

import pytest

REPO = pathlib.Path(__file__).resolve().parent.parent
_ARCH = "x86" if sys.maxsize <= 2 ** 32 else "x64"
os.environ.setdefault("TGSB_LIB_DIR", str(REPO / f"build-{_ARCH}-nvda" / "MinSizeRel"))
os.environ.setdefault("TGSB_PACKS_ROOT", str(REPO))
sys.path.insert(0, str(REPO / "python"))

from tgspeechbox.engine import Frontend  # noqa: E402

RATES = [0.5, 1.0, 1.6, 2.0, 3.0]
# kívánok, Európába, játszik, "a vár már áll", as eSpeak NG writes them.
WORDS = ["kˈiːvaːnok", "ˈɛuroːpaːbɑ", "jˈaːt͡sːik", "ˌɑ vˈaːr mˈaːr ˌaːll"]


def _phoneme_frames(voice, ipa, speed, key):
    with Frontend("hu") as fe:
        if voice:
            fe.set_voice_profile(voice)
        frames = fe.frames(ipa, speed)
        trace = fe.frame_trace()
    out = []
    for n, e in enumerate(trace):
        if e["phonemeKey"] == key:
            end = trace[n + 1]["frameIndex"] if n + 1 < len(trace) else len(frames)
            out += [f.params for f in frames[e["frameIndex"]:end] if f.params is not None]
    return out


@pytest.mark.parametrize("speed", RATES)
@pytest.mark.parametrize("ipa", WORDS)
def test_beths_a_acute_has_the_chosen_formants(ipa, speed):
    frames = _phoneme_frames("Beth", ipa, speed, "á")
    assert frames, f"no á in {ipa}"
    for p in frames:
        assert (round(p["cf3"]), round(p["cb3"]), round(p["cb4"])) == (2860, 300, 380), f"{ipa} @{speed}"
        # F4 stays below her F5 and above the new F3: the three no longer sit in one peak.
        assert p["cf4"] - p["cf3"] > 400


@pytest.mark.parametrize("speed", RATES)
def test_only_that_vowel_is_overridden(speed):
    # Her í in the same word keeps the class scale on F3 (x 1.10 of the default voice's).
    beth = _phoneme_frames("Beth", "kˈiːvaːnok", speed, "í")
    plain = _phoneme_frames("", "kˈiːvaːnok", speed, "í")
    assert beth and len(beth) == len(plain)
    assert beth[0]["cf3"] == pytest.approx(plain[0]["cf3"] * 1.10, rel=0.005)


@pytest.fixture(scope="module")
def packs_without_the_override(tmp_path_factory):
    root = tmp_path_factory.mktemp("beth_plain")
    shutil.copytree(REPO / "packs", root / "packs")
    path = root / "packs" / "phonemes.yaml"
    lines = path.read_text(encoding="utf-8").splitlines(keepends=True)
    start = next(i for i, line in enumerate(lines) if line.rstrip() == "  Beth:")
    first = next(i for i in range(start, len(lines)) if lines[i].rstrip() == "    phonemeOverrides:")
    last = next(i for i in range(first + 1, len(lines)) if lines[i].strip() == "cb4: 380")
    lines[first:last + 1] = ["    phonemeOverrides: {}\n"]
    path.write_text("".join(lines), encoding="utf-8", newline="")
    return str(root)


def _all_frames(voice, ipa, speed, packs_root=None):
    with Frontend("hu", packs_root) as fe:
        if voice:
            fe.set_voice_profile(voice)
        return [(None if f.params is None else {k: (None if v != v else v) for k, v in f.params.items()},
                 f.duration_ms, f.fade_ms) for f in fe.frames(ipa, speed)]


@pytest.mark.parametrize("speed", RATES)
@pytest.mark.parametrize("voice", ["", "Bobby"])
def test_other_voices_are_what_they_were(voice, speed, packs_without_the_override):
    for ipa in WORDS:
        assert _all_frames(voice, ipa, speed) == _all_frames(voice, ipa, speed, packs_without_the_override)


def test_the_override_is_what_changes_beth(packs_without_the_override):
    assert _all_frames("Beth", WORDS[0], 1.0) != _all_frames("Beth", WORDS[0], 1.0, packs_without_the_override)
    # A word without that vowel is the same Beth.
    assert _all_frames("Beth", "kˈit͡ʃi", 1.0) == _all_frames("Beth", "kˈit͡ʃi", 1.0, packs_without_the_override)

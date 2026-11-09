"""A one-word clause before a comma does not fall like a full stop (#155).

29-Bloo: "English, U.S." was read "English. U.S.", in every pitch mode.
The clause splitter (#133) splits at the comma, so "English" arrives as a
clause of its own with clause type ','.  Eight language packs (es, fi, fr,
hu, it, pt, pt-br, ro) had singleWordClauseTypeOverride: "." with
CommaOnly, which turned every one-word comma clause into a statement: the
first word of "Inglés, Estados Unidos", of "Sí, claro", of a list.  English
took the override out in February for the same reason ("Hi, How, Are, You":
"Hi" fell like a statement); the others kept it.

The comma clause is a continuation: it stays up, as eSpeak NG and the
Eloquence-lineage reference do, and the statement still falls.
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

LANGS = sorted(p.stem for p in (REPO / "packs" / "lang").glob("*.yaml") if p.stem != "default")
WORD = "ˈmana"  # a one-word clause every pack can say


def _end_pitch(lang, clause, speed):
    with Frontend(lang) as fe:
        frames = fe.frames(WORD, speed=speed, pitch=110.0, inflection=0.5, clause=clause)
    voiced = [f for f in frames if not f.is_silence and f.params["voiceAmplitude"] > 0]
    assert voiced, f"{lang}: no voiced frames"
    return voiced[-1].params["endVoicePitch"]


@pytest.mark.parametrize("speed", [0.5, 1.0, 1.6, 2.0, 3.0])
@pytest.mark.parametrize("lang", LANGS)
def test_a_one_word_comma_clause_stays_up(lang, speed):
    comma, stop = _end_pitch(lang, ",", speed), _end_pitch(lang, ".", speed)
    assert comma > stop * 1.1, (
        f"{lang} @{speed}: '{WORD},' ends at {comma:.0f} Hz, like '{WORD}.' at {stop:.0f} Hz")


@pytest.mark.parametrize("lang", LANGS)
def test_no_pack_turns_a_comma_into_a_full_stop(lang):
    text = (REPO / "packs" / "lang" / f"{lang}.yaml").read_text(encoding="utf-8")
    live = [ln for ln in text.splitlines()
            if ln.strip().startswith("singleWordClauseTypeOverride:")]
    assert not live, f"{lang}.yaml: {live}"

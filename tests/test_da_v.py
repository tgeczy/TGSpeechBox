"""Danish v is spoken (Fubsepude, a Danish speaker, on Mastodon: "major issues").

eSpeak NG writes Danish v as the labiodental approximant ʋ, which no pack
defined, so the frontend dropped it: "vi" was "i", "hvad" was "a", "vej"
lost its v, "torvet" and "haven" theirs mid-word.  The same class as the
Swedish ʈ/ɖ/ɳ that went undefined until beta 10.  Dutch has always mapped ʋ
to v; Danish now does too.

The IPA is eSpeak NG's for each word.
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


@pytest.mark.parametrize("word,ipa", [
    ("vi", "ʋi"), ("hvad", "ʋˈa"), ("vej", "ʋˈ?͡ɑj"), ("hver", "ʋˈεɐ̯"),
    ("torvet", "tˈɒʋəð"), ("haven", "hˈaʋən"),
])
def test_danish_v_is_spoken(word, ipa):
    keys = [e["phonemeKey"] for e in tgspeechbox.render(ipa, "da").frame_trace]
    assert "v" in keys, f"{word} ({ipa}) lost its v: {keys}"


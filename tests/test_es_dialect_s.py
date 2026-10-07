"""Every Spanish /s/ is the dialect's own /s/, after a tap too (#143, #81).

Greg and 29-Bloo: in Spanish from Spain, "versión", "persona", "torso" and
"pizza" still sounded Mexican.  They were neither Castilian nor Mexican: the
/s/ after a tap came from es.yaml's ɾs → ɾᵊs cluster rule, and a replacement's
output is protected from later replacements, so s → s_es (s_mx in es-mx)
never reached it and the plain base /s/ played.  "pizza" came from eSpeak
as the affricate t͡s, which Spanish doesn't have (es.yaml says so) and which
skipped the dialect /s/ the same way.

The IPA is eSpeak NG's for each word (the same in es and es-mx).
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

WORDS = {
    "casa": "kˈasa",
    "versión": "beɾsjˈon",
    "persona": "peɾsˈona",
    "torso": "tˈoɾso",
    "dorsal": "doɾsˈal",
    "persiste": "peɾsˈiste",
    "agregarse": "ˌaɣɾeɣˈaɾse",
    "pizza": "pˈit͡sa",
}
DIALECT_S = {"es": "s_es", "es-mx": "s_mx"}


@pytest.mark.parametrize("lang", ["es", "es-mx"])
@pytest.mark.parametrize("word", list(WORDS))
def test_every_s_is_the_dialect_s(lang, word):
    keys = [e["phonemeKey"] for e in tgspeechbox.render(WORDS[word], lang).frame_trace]
    sibilants = [k for k in keys if k == "s" or k.startswith("s_") or "͡s" in k]
    assert sibilants, f"{word}: no /s/ at all: {keys}"
    assert set(sibilants) == {DIALECT_S[lang]}, f"{word} ({lang}): {keys}"

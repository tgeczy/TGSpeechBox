"""A voice profile's phonemeOverrides are written one phoneme to a line.

The phoneme editor reads an override only in that form,

    phonemeOverrides:
      á: {cf3: 2860, cb3: 300, cb4: 380}

and "Save to Profile" and "Edit voice profiles" write the whole voiceProfiles
section back from what the editor read (tools/tgsbPhonemeEditorWin32/
VoiceProfileEditor.cpp).  An override written as a block,

      "á":
        cf3: 2860

is read as an override with no fields, and the next save of ANY profile
writes it back empty.  Beth's Hungarian á was added in that form during the
3.10 release candidates, so one save in the editor would have put her á back
on the formants it was moved off.  Until the editor reads both forms, the
shipped file keeps to the one it can read, and the frontend must read that
form as the same override.
"""
from __future__ import annotations

import os
import pathlib
import re
import sys

import pytest

REPO = pathlib.Path(__file__).resolve().parent.parent
_ARCH = "x86" if sys.maxsize <= 2 ** 32 else "x64"
os.environ.setdefault("TGSB_LIB_DIR", str(REPO / f"build-{_ARCH}-nvda" / "MinSizeRel"))
os.environ.setdefault("TGSB_PACKS_ROOT", str(REPO))
sys.path.insert(0, str(REPO / "python"))

from tgspeechbox.engine import Frontend  # noqa: E402

RATES = [0.5, 1.0, 1.6, 2.0, 3.0]


def _override_lines():
    """(profile, line number, text) for every line under a phonemeOverrides key."""
    lines = (REPO / "packs" / "phonemes.yaml").read_text(encoding="utf-8").splitlines()
    start = next(n for n, l in enumerate(lines) if l.startswith("voiceProfiles:"))
    out, profile, inside = [], None, False
    for n in range(start + 1, len(lines)):
        line = lines[n]
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        indent = len(line) - len(line.lstrip())
        if indent == 0:
            break
        if indent == 2:
            profile, inside = line.strip().rstrip(":"), False
        elif indent == 4:
            inside = line.strip() == "phonemeOverrides:"
        elif inside:
            out.append((profile, n + 1, line))
    return out


def test_the_shipped_overrides_are_found():
    profiles = {p for p, _, _ in _override_lines()}
    assert {"Beth", "Bobby"} <= profiles, profiles


def test_every_shipped_override_is_one_line_the_editor_reads():
    one_line = re.compile(r"^ {6}[^\s\"'#][^\s:]*: \{[^{}]+\}\s*$")
    bad = [f"line {n} ({profile}): {text.strip()}" for profile, n, text in _override_lines() if not one_line.match(text)]
    assert not bad, "phonemeOverrides the phoneme editor would empty on its next save:\n" + "\n".join(bad)


@pytest.mark.parametrize("speed", RATES)
def test_the_frontend_reads_beths_one_line_override(speed):
    """The values of tests/test_beth_hungarian_a.py, read from the one-line form."""
    with Frontend("hu") as fe:
        fe.set_voice_profile("Beth")
        frames = fe.frames("jˈaːt͡sːik", speed)
        trace = fe.frame_trace()
    starts = [e["frameIndex"] for e in trace] + [len(frames)]
    seen = 0
    for e, a, b in zip(trace, starts, starts[1:]):
        if e["phonemeKey"] != "á":
            continue
        for f in frames[a:b]:
            if f.params is None:
                continue
            seen += 1
            got = (f.params["cf3"], f.params["cb3"], f.params["cb4"])
            assert got == pytest.approx((2860.0, 300.0, 380.0)), f"@{speed}: {got}"
    assert seen, "no á frame"

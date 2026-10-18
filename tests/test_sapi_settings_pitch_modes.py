"""The SAPI settings app offers, keeps and saves every pitch mode (#148).

Edu: Arató "is not selected" after OK, and reopening shows the last mode.
The OK handler accepted pitch-mode indexes 0-5 only, so Arató (6, added
later) was dropped without a word.  The modes now come from one table that
filling the list, OK, loading and saving all use, and no bound may be
written by hand.
"""
from __future__ import annotations

import pathlib
import re

REPO = pathlib.Path(__file__).resolve().parent.parent
SRC = (REPO / "tools" / "tgsbSapiSettings" / "settings_app.cpp").read_text(encoding="utf-8")

# Every legacyPitchMode string the frontend understands (pack.cpp).
FRONTEND_MODES = ["espeak_style", "fujisaki_style", "impulse_style", "klatt_style", "legacy", "arato_style"]


def _table():
    m = re.search(r"kPitchModes\[\]\s*=\s*\{(.*?)\};", SRC, re.S)
    assert m, "no kPitchModes table in settings_app.cpp"
    return re.findall(r'\{\s*L"([^"]*)"\s*,\s*L"([^"]*)"\s*\}', m.group(1))


def test_every_frontend_pitch_mode_is_in_the_table():
    ini_names = [ini for ini, _label in _table()]
    assert ini_names[0] == "", "index 0 must be the language default"
    for mode in FRONTEND_MODES:
        assert mode in ini_names, f"{mode} missing from the SAPI settings"


def test_no_hand_written_pitch_mode_bounds():
    for pattern in (r"pitchSel\s*[<>]=?\s*[1-9]", r"pitchMode\s*=\s*[1-9]\b", r"case\s+\d+:\s*pmStr"):
        assert not re.search(pattern, SRC), f"hand-written pitch-mode bound left: {pattern}"
    assert "pitchSel < kPitchModeCount" in SRC, "OK must accept every mode in the table"
    assert "pmSel >= kPitchModeCount" in SRC, "opening the dialog must show every mode in the table"


def test_the_list_is_filled_from_the_table():
    assert 'CB_ADDSTRING, 0, (LPARAM)L"Arat' not in SRC

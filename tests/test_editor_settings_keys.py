"""The phoneme editor's settings key list keeps up with the frontend (#225).

The Edit Setting dialog of the Windows phoneme editor offers the language
pack settings in a drop-down.  The list behind it (knownLanguageSettingKeys
in the editor's AppController) is written by hand, and it had fallen 63
settings behind what the frontend reads: amplitudeContourEnabled,
initialYBeforeConsonantAsI and pronounceableCapsAsWords, which shipped packs
set, the Arató pitch mode's settings, formantPulseMode and more.  A pack
author could still type them, but could not find them.

The frontend reads a setting with getNum, getBool or getStr in mergeSettings
(src/frontend/pack.cpp).  This test reads both sources: every setting read
there must be offered, every key offered must be one the frontend reads, and
the list is sorted as its comment says.  Add a setting to pack.cpp and this
fails until the editor offers it.
"""
from __future__ import annotations

import pathlib
import re

import pytest

REPO = pathlib.Path(__file__).resolve().parent.parent
PACK_CPP = REPO / "src" / "frontend" / "pack.cpp"
# The file is tracked under this lower-case name (its CMake entry says
# AppController.cpp, which only a case-insensitive file system forgives).
APP_CPP = REPO / "tools" / "tgsbPhonemeEditorWin32" / "appcontroller.cpp"


def merge_settings_source() -> str:
    """The text of mergeSettings(): from its first line to the next function."""
    text = PACK_CPP.read_text(encoding="utf-8")
    start = text.index("static void mergeSettings(")
    end = text.index("\nstatic ", start + 1)
    return text[start:end]


def frontend_setting_keys() -> list[str]:
    """Every key mergeSettings reads straight from `settings:` as one value."""
    return re.findall(r'\bget(?:Num|Bool|Str)\(\s*"([A-Za-z0-9_]+)"', merge_settings_source())


def editor_setting_keys() -> list[str]:
    """The keys of knownLanguageSettingKeys(), in the order they are written."""
    text = APP_CPP.read_text(encoding="utf-8")
    start = text.index("static std::vector<std::string> knownLanguageSettingKeys()")
    end = text.index("};", start)
    return re.findall(r'^\s*"([A-Za-z0-9_]+)",?\s*$', text[start:end], flags=re.M)


def test_both_lists_are_found():
    # A regex that matches nothing would make every check below pass.
    assert len(frontend_setting_keys()) > 300
    assert len(editor_setting_keys()) > 300


def test_every_setting_the_frontend_reads_is_offered():
    missing = sorted(set(frontend_setting_keys()) - set(editor_setting_keys()))
    assert missing == [], (
        f"{len(missing)} settings read by src/frontend/pack.cpp are not in the "
        f"editor's knownLanguageSettingKeys(): {missing}"
    )


@pytest.mark.parametrize("key", [
    # Named in #225; the first three are set by shipped packs (en-us, es).
    "amplitudeContourEnabled",
    "initialYBeforeConsonantAsI",
    "pronounceableCapsAsWords",
    "aratoReferencePitchHz",
    "formantPulseMode",
    "twBeforeVowelAsTu",
    "trillCloseVoicingFactor",
    "rateReductionEnabled",
    "nasalDiphthongOffglideScale",
])
def test_the_settings_named_in_the_issue_are_offered(key):
    assert key in frontend_setting_keys(), "the frontend no longer reads this; drop it from the test"
    assert key in editor_setting_keys()


def test_every_key_offered_is_one_the_frontend_reads():
    # A stale or misspelled key in the drop-down would be saved and ignored.
    source = merge_settings_source()
    unknown = [k for k in editor_setting_keys() if f'"{k}"' not in source]
    assert unknown == []


def test_the_list_is_sorted_and_names_each_key_once():
    keys = editor_setting_keys()
    assert keys == sorted(keys)
    assert len(keys) == len(set(keys))

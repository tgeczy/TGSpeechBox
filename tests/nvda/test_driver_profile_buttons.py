"""Saving or importing voice profile sliders keeps the language pack (#194).

"Save Voice Profile Sliders to YAML..." and "Import Voice Profile Sliders"
in the TGSpeechBox settings panel change phonemes.yaml and then reload the
frontend.  They reloaded it with the driver's language *setting*, which is
the literal "auto" unless the user picked a language; the frontend finds no
auto.yaml, loads default.yaml alone and reports success.  From then on every
utterance lost its language: no year splitting, no number reading, none of
the language's timing, until the language was changed or NVDA restarted.

The panel's handlers are run here as wx would run them, with a wx that
answers Yes to every question (conftest.py's `panel`), on the real driver.
"""
from __future__ import annotations

import pathlib
import shutil

import pytest

TEXT = "The 21st of March, 1999."


def _spoken(harness, record_frames):
    d = harness.driver
    log = record_frames(d)
    d.cancel()
    harness.speak([TEXT])
    return d._frontend.prepareText(TEXT), list(log)


def _save(harness, panel, tmp_path):
    d = harness.driver
    staged = pathlib.Path(d._packsDir) / "phonemes.yaml"
    before = staged.read_bytes()
    if d.voice.startswith("profile:"):
        d.voiceTilt = 60  # a profile with no slider moved has nothing to save (#195)
    wx = panel("_onSaveVoiceProfileClick")
    assert "Save Complete" in wx.shown
    assert staged.read_bytes() != before  # the save happened, so the pack was reloaded


def _import(harness, panel, tmp_path):
    packs = pathlib.Path(harness.driver._packsDir)
    picked = tmp_path / "phonemes.yaml"
    shutil.copy2(packs / "phonemes.yaml", picked)  # the file as it is: the import changes nothing
    wx = panel("_onImportPhonemeFileClick", picked)
    assert "Import Complete" in wx.shown
    assert (packs / "phonemes.yaml").read_bytes() == picked.read_bytes()


@pytest.mark.parametrize("button", [_save, _import], ids=["save", "import"])
@pytest.mark.parametrize("voice", ["Adam", "profile:Beth"])
@pytest.mark.parametrize("language", ["auto", "en-us"])
def test_the_language_pack_survives_the_button(harness, record_frames, panel, tmp_path, monkeypatch,
                                               language, voice, button):
    import languageHandler
    d = harness.driver
    monkeypatch.setattr(languageHandler, "uiLanguage", "en")
    monkeypatch.setattr(languageHandler, "windowsLanguage", "en_US")
    d.language = language  # "auto" resolves to en-us here
    d.voice = voice
    assert d._resolvedLang == "en-us"
    text_before, frames_before = _spoken(harness, record_frames)
    assert "19 99" in text_before  # English splits the year

    button(harness, panel, tmp_path)

    text_after, frames_after = _spoken(harness, record_frames)
    assert text_after == text_before, "the language pack was dropped: the year is no longer split"
    assert frames_after == frames_before
    assert d._frontendLangTag == "en-us"
    assert d.voice == voice

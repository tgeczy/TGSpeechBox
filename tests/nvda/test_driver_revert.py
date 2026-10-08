"""Reverting NVDA's configuration takes back unsaved language-file changes (#140).

Edu: "When I press NVDA+control+r ... it reverts to settings I made that I
didn't want to save ... I always leave the automatic saving option
disabled."  The voice-panel settings that live in the language files (stop
closure mode, pitch mode, the inflection scale, year splitting and so on) were
written into the files the moment they changed, and the files are what the
driver trusts (#127), so NVDA's "revert to saved configuration" brought
NVDA's own copy back but left the files changed, and the driver then
synced the unsaved values right back into NVDA's config.

Now a language-file change is undone when NVDA reverts its configuration
(config.post_configReset) unless NVDA saved its configuration since
(config.post_configSave), the same rule as every other NVDA setting.
"""
from __future__ import annotations

import pathlib


def _lang_file(driver, tag):
    from synthDrivers.tgSpeechBox import langPackYaml
    return pathlib.Path(langPackYaml.langYamlPath(driver._packsDir, tag))


def _revert():
    """NVDA+Ctrl+R: NVDA reloads the saved configuration."""
    import config
    config.post_configReset.notify(factoryDefaults=False)


def _save():
    """NVDA+Ctrl+C, or saving on exit."""
    import config
    config.post_configSave.notify()


def _agota(harness, record_frames):
    log = record_frames(harness.driver)
    harness.driver.cancel()
    harness.speak(["agota"])
    return list(log)


def test_revert_takes_back_an_unsaved_language_file_change(harness, record_frames, driver_config_section):
    d = harness.driver
    d.language = "pt-br"
    before = d.stopClosureMode
    path = _lang_file(d, "pt-br")
    text_before = path.read_text(encoding="utf-8")
    speech_before = _agota(harness, record_frames)

    d.stopClosureMode = "none"
    assert d.stopClosureMode == "none"
    assert _agota(harness, record_frames) != speech_before  # the change is heard

    _revert()
    assert d.stopClosureMode == before, "NVDA+Ctrl+R left the unsaved stop closure mode in place"
    assert path.read_text(encoding="utf-8") == text_before
    assert driver_config_section()["stopClosureMode"] == before
    assert _agota(harness, record_frames) == speech_before, "the voice still speaks with the reverted setting"


def test_a_saved_change_survives_a_revert(harness):
    d = harness.driver
    d.language = "pt-br"
    d.stopClosureMode = "none"
    _save()
    _revert()
    assert d.stopClosureMode == "none", "a change NVDA saved was taken back"


def test_only_changes_since_the_last_save_are_taken_back(harness):
    d = harness.driver
    d.language = "pt-br"
    d.stopClosureMode = "none"
    _save()
    d.stopClosureMode = "always"
    _revert()
    assert d.stopClosureMode == "none"

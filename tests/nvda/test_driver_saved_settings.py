"""NVDA's saved copy of a language-file setting can't overwrite the file (#127).

A few voice-panel settings live in the language files (stop closure mode,
spelling diphthong mode, pitch mode, the legacy inflection scale, year
splitting, the thousands separator), and NVDA also keeps them in its config,
because they are in the voice panel.  NVDA replays that copy into the driver
when it re-applies settings (a config profile switch, loadSettings with
onlyChanged).  The copy could be stale: NVDA refreshes it from our getters
when it saves, but Edu has "save configuration on exit" off, and his copy
still said "stopClosureMode: none" from the NV Speech Player days.  The
first replay after start-up wrote "none" into Brazilian Portuguese's file:
no closure before stops, "agota" heard as "agora", b/d/t/p running together,
"right once, then wrong" on NVDA only.

The language files are authoritative.  After start-up and after a language
change the driver brings NVDA's copy in line with them, so a replay sets the
value they already hold.
"""
from __future__ import annotations

import pytest


def _fresh_driver(saved):
    """A driver started with `saved` in NVDA's config, as NVDA starts it:
    constructed (the start-up replay is ignored by design), then the main
    loop runs what the driver scheduled."""
    import config
    from synthDrivers import tgSpeechBox
    import conftest as H
    config.conf["speech"]["tgSpeechBox"] = dict(saved)
    d = tgSpeechBox.SynthDriver()
    H.pump_main_loop()
    return d


def _agota_frames(harness, driver, record_frames):
    log = record_frames(driver)
    driver.cancel()
    harness.driver = driver
    harness.speak(["agota"])
    return list(log)


EDU = {"language": "pt-br", "stopClosureMode": "none", "spellingDiphthongMode": "none",
       "legacyPitchMode": "espeak_style", "legacyPitchInflectionScale": 0}


def test_a_stale_saved_value_does_not_overwrite_the_language_file(harness, record_frames,
                                                                  nvda_replay_settings, driver_config_section):
    harness.driver.language = "pt-br"
    clean = _agota_frames(harness, harness.driver, record_frames)
    assert harness.driver.stopClosureMode == "vowel-and-cluster"

    d = _fresh_driver(EDU)
    try:
        d.language = "pt-br"
        nvda_replay_settings(d)  # a config profile switch, or OK in a settings dialog
        assert d.stopClosureMode == "vowel-and-cluster", (
            "NVDA's stale saved 'none' was written into the pt-br language file")
        assert driver_config_section()["stopClosureMode"] == "vowel-and-cluster"
        assert _agota_frames(harness, d, record_frames) == clean, (
            "'agota' lost its stop closures after NVDA re-applied the saved settings")
    finally:
        d.terminate()


def test_a_language_change_brings_the_saved_copy_along(harness, nvda_replay_settings, driver_config_section):
    """NVDA keeps one copy of each setting; after a language change it must
    hold the new language's values, or a replay writes the old language's
    into the new language's file."""
    d = harness.driver
    d.language = "pt-br"
    d.stopClosureMode = "none"  # the user's own choice, for Brazilian Portuguese
    assert d.stopClosureMode == "none"
    d.language = "en-us"
    en_value = d.stopClosureMode
    assert en_value != "none"
    assert driver_config_section()["stopClosureMode"] == en_value
    nvda_replay_settings(d)
    assert d.stopClosureMode == en_value, "pt-br's 'none' followed the user into en-us"


def test_a_change_made_in_the_voice_panel_still_sticks(harness, nvda_replay_settings, driver_config_section):
    d = harness.driver
    d.language = "pt-br"
    d.stopClosureMode = "none"  # the voice panel sets the property
    driver_config_section()["stopClosureMode"] = "none"  # and NVDA saves it
    nvda_replay_settings(d)
    assert d.stopClosureMode == "none"

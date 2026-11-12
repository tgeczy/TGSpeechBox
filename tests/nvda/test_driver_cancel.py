"""Cancel in NVDA's voice settings takes back language-file settings too (#160).

Six voice-panel settings live in the language files: stop closure mode,
pitch mode, spelling diphthong mode, the inflection scale, year splitting and
the thousands separator.  The panel sets a setting on the driver the moment
its control changes, and the driver writes it into the language file there
and then.  Cancel or Escape (VoiceSettingsPanel.onDiscard) undoes a change by
replaying NVDA's saved copy of every setting onto the driver: loadSettings(),
all of them, the language among the first.

Since #140 the driver also brought NVDA's copy in line with the file after
every write, and it did so again whenever the language was set, the same
language included.  So by the time Cancel replayed NVDA's copy it already held
the new value, and the change stayed, in the file, across restarts.  A blind
user arrows through a combo box to hear the choices and presses Escape to
leave it alone: the last choice heard was the one kept.

NVDA's copy of a setting the panel changed is NVDA's to update: OK saves it,
Cancel replays it.  The driver brings the copy in line only when the files
changed behind NVDA's back: at start-up, on a change of language, after the
TGSpeechBox panel saved, after a revert.
"""
from __future__ import annotations

import pathlib

import pytest

#: voice-panel setting -> (its key in the language file, a text it changes)
SETTINGS = {
    "stopClosureMode": ("stopClosureMode", "agota"),
    "legacyPitchMode": ("legacyPitchMode", "agota"),
    "spellingDiphthongMode": ("spellingDiphthongMode", "agota"),
    "yearSplitting": ("yearSplittingEnabled", "1999"),
    "thousandsSeparatorCommaToSpace": ("thousandsSeparatorCommaToSpace", "1,234"),
}
#: the ones whose change is heard in that text, in both languages below
HEARD = ("stopClosureMode", "legacyPitchMode", "yearSplitting")
LANGS = ["en-us", "pt-br"]


def _another(setting, value):
    """A value the panel could change `setting` to."""
    if isinstance(value, bool):
        return not value
    choices = {"stopClosureMode": ("none", "always"),
               "legacyPitchMode": ("klatt_style", "espeak_style"),
               "spellingDiphthongMode": ("monophthong", "none")}[setting]
    return choices[0] if value != choices[0] else choices[1]


def _lang_dir(driver):
    return pathlib.Path(driver._packsDir) / "lang"


def _in_file(driver, lang, key):
    """What the language files say for `key`: None when none of them has it,
    and pitch mode's old true/false as the mode each meant."""
    from synthDrivers.tgSpeechBox import langPackYaml
    raw = langPackYaml.getEffectiveSettingValue(driver._packsDir, lang, key)
    if raw is None:
        return None
    value = raw.strip().strip("\"'").lower()
    if key == "legacyPitchMode":
        value = {"false": "espeak_style", "true": "legacy"}.get(value, value)
    return value


def _other_files(driver, lang):
    return {p.name: p.read_bytes() for p in sorted(_lang_dir(driver).glob("*.yaml")) if p.stem != lang}


def _frames(harness, record_frames, text):
    log = record_frames(harness.driver)
    harness.driver.cancel()
    harness.speak([text])
    return list(log)


def _ok(driver):
    """OK or Apply: NVDA reads every setting back from the driver and keeps it."""
    driver.saveSettings()


def _cancel(driver):
    """Cancel or Escape: NVDA replays its saved copy of every setting."""
    driver.loadSettings()


@pytest.mark.parametrize("setting", sorted(SETTINGS))
@pytest.mark.parametrize("lang", LANGS)
def test_cancel_takes_back_a_language_file_setting(harness, record_frames, driver_config_section, lang, setting):
    key, text = SETTINGS[setting]
    d = harness.driver
    d.language = lang
    _ok(d)  # the settings as NVDA has them saved when the dialog opens
    before = getattr(d, setting)
    file_before = _in_file(d, lang, key)
    others_before = _other_files(d, lang)
    rate_before = d.rate
    speech_before = _frames(harness, record_frames, text)

    # The panel sets the driver as each control changes.
    d.rate = rate_before + 20
    setattr(d, setting, _another(setting, before))
    assert getattr(d, setting) != before
    assert _in_file(d, lang, key) != file_before
    if setting in HEARD:
        d.rate = rate_before
        assert _frames(harness, record_frames, text) != speech_before  # the change is heard
        d.rate = rate_before + 20

    _cancel(d)
    assert d.rate == rate_before
    assert getattr(d, setting) == before, f"{lang}: Cancel left the changed {setting} in place"
    # A key no file had is written out by the replay, with the value the
    # driver read for it before.
    was = file_before if file_before is not None else str(before).lower()
    assert _in_file(d, lang, key) == was, f"{lang}: Cancel left {key} changed in the language file"
    assert str(driver_config_section()[setting]) == str(before)
    assert _frames(harness, record_frames, text) == speech_before, "the voice still speaks with the cancelled setting"
    assert _other_files(d, lang) == others_before, "another language's file changed"


@pytest.mark.parametrize("setting", sorted(SETTINGS))
def test_ok_keeps_the_change(harness, driver_config_section, setting):
    key, _text = SETTINGS[setting]
    d = harness.driver
    d.language = "pt-br"
    _ok(d)
    want = _another(setting, getattr(d, setting))
    setattr(d, setting, want)
    _ok(d)
    assert str(driver_config_section()[setting]) == str(want)
    d.loadSettings(onlyChanged=True)  # a config profile switch
    assert getattr(d, setting) == want
    _cancel(d)  # the dialog opened again and left alone
    assert getattr(d, setting) == want, f"a saved {setting} was taken back by a later Cancel"
    assert _in_file(d, "pt-br", key) == str(want).lower()


def test_a_settings_ring_change_survives_a_later_cancel(harness, driver_config_section):
    """The settings ring sets the driver and then writes NVDA's copy itself
    (synthSettingsRing.SynthSetting._set_value); nothing else saves it."""
    d = harness.driver
    d.language = "en-us"
    _ok(d)
    want = _another("legacyPitchMode", d.legacyPitchMode)
    d.legacyPitchMode = want
    driver_config_section()["legacyPitchMode"] = want
    _cancel(d)
    assert d.legacyPitchMode == want
    assert _in_file(d, "en-us", "legacyPitchMode") == want


def test_cancel_after_trying_another_language_brings_nothing_along(harness, driver_config_section):
    """NVDA keeps one copy of each setting and the files one per language
    (#127): a language tried in the dialog and cancelled must not leave its
    values in the saved language's file, nor the other way round."""
    d = harness.driver
    d.language = "en-us"
    d.stopClosureMode = "always"
    _ok(d)
    pt_before = (_lang_dir(d) / "pt-br.yaml").read_bytes()

    d.language = "pt-br"  # the language combo box, in the dialog
    pt_value = d.stopClosureMode
    assert pt_value != "always"
    _cancel(d)
    assert d.language == "en-us"
    assert d.stopClosureMode == "always"
    assert driver_config_section()["stopClosureMode"] == "always"
    assert _in_file(d, "en-us", "stopClosureMode") == "always"
    assert (_lang_dir(d) / "pt-br.yaml").read_bytes() == pt_before
    d.language = "pt-br"
    assert d.stopClosureMode == pt_value

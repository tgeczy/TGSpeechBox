"""The legacy pitch inflection scale slider writes what it is set to and
reads what the file holds (#196).

The slider goes from 0 to 100; each language's file holds the value as 0.0
to 2.0 (legacyPitchInflectionScale).  Two faults:

* The setter skipped a value equal to the one it last wrote, and it
  remembered one such value for all languages.  40 set in English, then 40
  set in Hungarian: nothing was written for Hungarian.
* The getter cut the decimals off instead of rounding: the pack default
  0.58 is 28.999... on the slider and read as 28, and the slider values 29,
  57 and 58 read back one lower than set.

Together they changed a file nobody touched: NVDA's saved copy said 28, the
driver remembered 29, and Escape in the voice panel (a replay of every saved
setting) wrote 0.56 over the language's 0.58.
"""
from __future__ import annotations

import pytest

KEY = "legacyPitchInflectionScale"


def _in_file(driver, lang):
    from synthDrivers.tgSpeechBox import langPackYaml
    raw = langPackYaml.getEffectiveSettingValue(driver._packsDir, lang, KEY)
    return None if raw is None else float(raw)


def _write(driver, lang, value):
    """The file edited by hand, or by the TGSpeechBox panel."""
    from synthDrivers.tgSpeechBox import langPackYaml
    langPackYaml.upsertSetting(driver._packsDir, lang, KEY, value)
    driver.reloadLanguagePack()


def test_the_same_value_is_written_for_a_second_language(harness):
    d = harness.driver
    d.language = "en-us"
    d.legacyPitchInflectionScale = 40
    assert _in_file(d, "en-us") == 0.8
    d.language = "hu"
    assert _in_file(d, "hu") == 0.58 and d.legacyPitchInflectionScale == 29
    d.legacyPitchInflectionScale = 40
    assert _in_file(d, "hu") == 0.8, "40 was dropped for Hungarian because English was set to 40 last"
    assert d.legacyPitchInflectionScale == 40
    assert _in_file(d, "en-us") == 0.8


def test_a_value_changed_in_the_file_can_be_set_back(harness):
    d = harness.driver
    d.language = "en-us"
    d.legacyPitchInflectionScale = 40
    _write(d, "en-us", 1.2)
    assert d.legacyPitchInflectionScale == 60
    d.legacyPitchInflectionScale = 40
    assert _in_file(d, "en-us") == 0.8


def test_every_slider_value_reads_back_as_set(harness):
    d = harness.driver
    d.language = "en-us"
    wrong = {}
    for v in list(range(101)) + list(range(99, -1, -1)):
        d.legacyPitchInflectionScale = v
        if d.legacyPitchInflectionScale != v or _in_file(d, "en-us") != round(v * 0.02, 3):
            wrong[v] = (d.legacyPitchInflectionScale, _in_file(d, "en-us"))
    assert not wrong, f"set -> (read back, in the file): {wrong}"


def test_the_pack_default_reads_as_29(harness):
    d = harness.driver
    d.language = "de"  # no value of its own: default.yaml's 0.58
    assert _in_file(d, "de") == 0.58
    assert d.legacyPitchInflectionScale == 29


@pytest.mark.parametrize("lang,held", [("de", 0.58), ("en-us", 0.58), ("hu", 0.58), ("en-us", 0.56), ("hu", 0.56)])
def test_escape_leaves_the_value_in_the_file(harness, driver_config_section, lang, held):
    """NVDA's saved settings replayed, as Escape in the voice panel does,
    with nothing changed in the dialog."""
    d = harness.driver
    d.language = lang
    if held != 0.58:
        # Every shipped pack has 0.58 since #177; 0.56 is what the old
        # rounding left in five of them, and what a user's file may still hold.
        _write(d, lang, held)
    assert _in_file(d, lang) == held
    d.saveSettings()  # as NVDA saves them on a first load, or on OK
    d.loadSettings()  # Escape
    assert _in_file(d, lang) == held, f"{lang}: Escape rewrote the inflection scale"
    assert d.legacyPitchInflectionScale == round(held * 50)
    assert driver_config_section()[KEY] == round(held * 50)


def test_a_fresh_start_and_escape_leave_the_value_in_the_file(harness, driver_config_section):
    """The same with "save configuration on exit" off: NVDA's copy is the
    one the driver synced at the language change, nothing was ever saved."""
    d = harness.driver
    d.language = "de"
    d.loadSettings()
    assert _in_file(d, "de") == 0.58
    assert d.legacyPitchInflectionScale == 29


@pytest.mark.parametrize("held", [0.575, 0.585, 1.111, 0.01])
def test_a_value_between_two_slider_steps_is_left_alone(harness, held):
    """A file edited by hand can hold a value the slider has no step for.
    Replaying what the slider shows must not snap it to the step."""
    d = harness.driver
    d.language = "pt-br"
    _write(d, "pt-br", held)
    shown = d.legacyPitchInflectionScale
    assert abs(shown - held * 50) <= 0.5
    d.saveSettings()
    d.loadSettings()
    d.legacyPitchInflectionScale = shown
    assert _in_file(d, "pt-br") == held

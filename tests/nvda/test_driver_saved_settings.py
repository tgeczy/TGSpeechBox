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
    constructed, initSettings (the start-up replay is ignored by design),
    then the main loop runs what the driver scheduled."""
    import config
    from synthDrivers import tgSpeechBox
    import conftest as H
    config.conf["speech"]["tgSpeechBox"] = dict(saved)
    d = tgSpeechBox.SynthDriver()
    d.initSettings()
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


@pytest.mark.parametrize("lang", ["es", "en-us"])
def test_a_change_saved_in_the_language_pack_panel_sticks(harness, nvda_replay_settings,
                                                          driver_config_section, lang):
    """Edu (#140): "this option always re-enables itself".  The TGSpeechBox
    settings panel saves year splitting into the language file, but NVDA's
    copy still said on, and the next replay (a profile switch, a restart)
    wrote it back."""
    from synthDrivers.tgSpeechBox import langPackYaml
    from synthDrivers.tgSpeechBox.langPackSettingsPanel import _getPacksDir
    d = harness.driver
    d.language = lang
    d.yearSplitting = True
    driver_config_section()["yearSplitting"] = True  # saved while it was on

    # The panel's Save: write the file, reload the pack.
    langPackYaml.upsertSetting(_getPacksDir(), lang, "yearSplittingEnabled", False)
    d.reloadLanguagePack()
    assert d.yearSplitting is False

    nvda_replay_settings(d)
    assert d.yearSplitting is False, f"{lang}: NVDA's old copy turned year splitting back on"
    assert driver_config_section()["yearSplitting"] is False


def _nvda_save(driver, section):
    """NVDA+Ctrl+C, or saving on exit: the voice panel's saveSettings reads
    every setting back from the driver, then config.save notifies
    post_configSave (NVDA 2026.2, autoSettings._saveSpecificSettings)."""
    import config
    for s in driver.supportedSettings:
        section[s.id] = getattr(driver, s.id)
    config.post_configSave.notify()


class _NvdaIniSection(dict):
    """config.conf["speech"]["tgSpeechBox"] as NVDA starts with it: the values
    are the strings nvda.ini holds ("False", "70").  NVDA turns a value into
    its type when it is first read, if it knows the setting's type by then,
    and caches what it read (config.AggregatedSection._cacheLeaf).  It learns
    the types in initSettings, and learning them does not clear the cache, so
    a value read before initSettings stays a string."""

    def __init__(self, saved):
        super().__init__({k: str(v) for k, v in saved.items()})
        self._types = {}
        self._cache = {}

    def learnTypes(self, settings):
        for s in settings:
            if isinstance(s.defaultVal, bool):
                self._types[s.id] = lambda v: v.strip().lower() in ("true", "1", "yes", "on")
            elif isinstance(s.defaultVal, int):
                self._types[s.id] = int

    def __getitem__(self, key):
        if key not in self._cache:
            val = super().__getitem__(key)
            if key in self._types and isinstance(val, str):
                val = self._types[key](val)
            self._cache[key] = val
        return self._cache[key]

    def get(self, key, default=None):
        try:
            return self[key]
        except KeyError:
            return default

    def __setitem__(self, key, val):
        super().__setitem__(key, val)
        self._cache[key] = val


def _wx_not_up_yet(*a, **k):
    # core.callLater before NVDA has made its wx.App: NVDANotInitializedError.
    raise RuntimeError("Cannot schedule callable, wx.App is not initialized")


@pytest.mark.parametrize("start", ["nvda_start", "synth_switch"])
@pytest.mark.parametrize("want", [False, True])
@pytest.mark.parametrize("setting", ["yearSplitting", "thousandsSeparatorCommaToSpace"])
@pytest.mark.parametrize("lang", ["es-mx", "es-es", "es", "en-us"])
def test_a_voice_panel_checkbox_survives_a_restart(harness, driver_config_section, monkeypatch,
                                                   setting, lang, want, start):
    """Greg (#151): a "Year splitting" or "Thousands separator" checkbox
    unchecked in NVDA's voice panel was checked again after NVDA restarted.
    His log: nvda.ini said yearSplitting = False, and during the start the
    driver was handed True and wrote it into es-mx.yaml.

    NVDA starts the synthesizer before it makes its wx.App (core.main:
    speech.initialize, then _setUpWxApp), so core.callLater fails there.
    The driver took that as "turn language-file writes on now", inside its
    constructor; turning them on also reads NVDA's copy of the settings,
    before initSettings has told the config their types, and the "False"
    read then stayed a string: bool("False") is True.  A synthesizer
    switch (the earlier version of this test) has wx up, and never failed."""
    import conftest as H
    d = harness.driver
    d.language = lang
    setattr(d, setting, want)  # the checkbox sets the property as it changes
    _nvda_save(d, driver_config_section())
    saved = dict(driver_config_section())
    d.terminate()
    H.pump_main_loop()

    # The restart, as NVDA's getSynthInstance does it: construct, then
    # initSettings replays every saved setting (language first), then the
    # main loop runs what the driver scheduled.
    import config
    import core
    from synthDrivers import tgSpeechBox
    config.conf["speech"]["tgSpeechBox"] = _NvdaIniSection(saved) if start == "nvda_start" else dict(saved)
    if start == "nvda_start":
        monkeypatch.setattr(core, "callLater", _wx_not_up_yet)
    again = tgSpeechBox.SynthDriver()
    again.initSettings()
    monkeypatch.undo()
    H.pump_main_loop()
    try:
        assert getattr(again, setting) is want, f"{lang}: {setting} came back {not want} after {start}"
        assert str(config.conf["speech"]["tgSpeechBox"].get(setting)) == str(want)
    finally:
        again.terminate()
        H.pump_main_loop()


@pytest.mark.parametrize("setting", ["yearSplitting", "thousandsSeparatorCommaToSpace"])
@pytest.mark.parametrize("text,want", [("False", False), ("false", False), ("0", False),
                                       ("True", True), ("1", True), (False, False), (True, True)])
def test_a_checkbox_handed_a_string_reads_it(harness, setting, text, want):
    """NVDA hands a setting over as nvda.ini's string when it has not been
    told the type yet.  "False" is not a reason to check a box."""
    d = harness.driver
    d.language = "es-mx"
    setattr(d, setting, not want)
    setattr(d, setting, text)
    assert getattr(d, setting) is want

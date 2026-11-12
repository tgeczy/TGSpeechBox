"""Saving or importing voice profile sliders keeps the language pack (#194).

"Save Voice Profile Sliders to YAML..." and "Import Voice Profile Sliders"
in the TGSpeechBox settings panel change phonemes.yaml and then reload the
frontend.  They reloaded it with the driver's language *setting*, which is
the literal "auto" unless the user picked a language; the frontend finds no
auto.yaml, loads default.yaml alone and reports success.  From then on every
utterance lost its language: no year splitting, no number reading, none of
the language's timing, until the language was changed or NVDA restarted.

The panel's handlers are run here as wx would run them, with a wx that
answers Yes to every question, on the real driver.
"""
from __future__ import annotations

import pathlib
import shutil
import sys
import types

import pytest

TEXT = "The 21st of March, 1999."


class _FakeWx(types.ModuleType):
    """wx, as far as the two handlers go: message boxes that are answered
    Yes and a file dialog that picks `path`."""

    OK = ICON_WARNING = ICON_ERROR = ICON_INFORMATION = ICON_QUESTION = 0
    YES_NO = FD_OPEN = FD_FILE_MUST_EXIST = FD_SAVE = FD_OVERWRITE_PROMPT = 0
    YES, ID_OK = 2, 5100

    def __init__(self, path=""):
        super().__init__("wx")
        self.shown = []  # the titles of the message boxes
        outer = self

        class FileDialog:
            def __init__(self, *a, **k): pass
            def ShowModal(self): return outer.ID_OK
            def GetPath(self): return str(path)
            def Destroy(self): pass

        self.FileDialog = FileDialog

    def MessageBox(self, message, caption="", style=0, parent=None):
        self.shown.append(caption)
        return self.YES

    def CallAfter(self, fn, *a, **k):
        pass


@pytest.fixture
def panel(monkeypatch):
    """Returns run(handler name, picked file): the panel class's handler,
    called with a fake wx in place; gives back that wx."""
    from synthDrivers.tgSpeechBox import langPackSettingsPanel as mod

    gui = types.ModuleType("gui")
    dialogs = types.ModuleType("gui.settingsDialogs")
    dialogs.SettingsPanel = type("SettingsPanel", (), {})
    gui.settingsDialogs = dialogs
    monkeypatch.setitem(sys.modules, "gui", gui)
    monkeypatch.setitem(sys.modules, "gui.settingsDialogs", dialogs)
    monkeypatch.setattr(mod, "_PANEL_CLS", None)

    def run(handler, path=""):
        wx = _FakeWx(path)
        monkeypatch.setitem(sys.modules, "wx", wx)
        cls = mod._getPanelClass()
        assert cls is not None
        getattr(cls, handler)(object(), types.SimpleNamespace(Skip=lambda: None))
        return wx

    yield run
    monkeypatch.setattr(mod, "_PANEL_CLS", None)


def _spoken(harness, record_frames):
    d = harness.driver
    log = record_frames(d)
    d.cancel()
    harness.speak([TEXT])
    return d._frontend.prepareText(TEXT), list(log)


def _save(harness, panel, tmp_path):
    saved = []
    # The save itself is #195's subject; here it must not touch the staged
    # phonemes.yaml the other tests read.
    harness.driver._frontend.saveVoiceProfileSliders = lambda **kw: saved.append(kw["profileName"]) or True
    wx = panel("_onSaveVoiceProfileClick")
    assert len(saved) == 1 and "Save Complete" in wx.shown


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

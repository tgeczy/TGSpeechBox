"""The settings migration notice can be translated (#201).

Someone moving from the old NV Speech Player driver is told once, in a
dialog, that their settings were copied over.  The dialog's text and its
title were plain string literals in a module that never loaded the add-on's
translations, so they were English in every NVDA language, and no
translator had ever been shown them.

No NVDA is needed: the module is loaded by itself with stand-ins for the
few NVDA modules it touches.  addonHandler.initTranslation() is modelled as
NVDA's does it: it puts the add-on's gettext into the calling module as _.
tests/test_nvda_translations.py then holds every _() string to being in
every .po file.
"""
from __future__ import annotations

import ast
import importlib.util
import pathlib
import sys
import types

REPO = pathlib.Path(__file__).resolve().parent.parent
MODULE = REPO / "nvdaAddon" / "synthDrivers" / "tgSpeechBox" / "migrate_config.py"


def _load(monkeypatch, translate):
    """migrate_config, loaded in an NVDA whose add-on catalog is `translate`.
    Returns (module, the message boxes shown)."""
    shown = []

    logh = types.ModuleType("logHandler")
    logh.log = types.SimpleNamespace(**{name: (lambda *a, **k: None)
                                       for name in ("info", "debug", "debugWarning", "warning", "error")})
    addon_handler = types.ModuleType("addonHandler")

    def initTranslation():
        sys._getframe(1).f_globals["_"] = translate

    addon_handler.initTranslation = initTranslation
    wx = types.ModuleType("wx")
    wx.OK, wx.ICON_INFORMATION = 4, 2048
    wx.CallAfter = lambda fn, *a, **k: fn(*a, **k)
    gui = types.ModuleType("gui")
    gui.messageBox = lambda message, caption="", style=0, parent=None: shown.append((message, caption))
    for name, mod in (("logHandler", logh), ("addonHandler", addon_handler), ("wx", wx), ("gui", gui)):
        monkeypatch.setitem(sys.modules, name, mod)

    spec = importlib.util.spec_from_file_location("_tgsb_migrate_config", MODULE)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module, shown


def test_the_notice_is_shown_in_the_add_ons_translation(monkeypatch):
    module, shown = _load(monkeypatch, lambda s: "[hu] " + s)
    module._show_notice_deferred()
    assert len(shown) == 1
    message, title = shown[0]
    assert message.startswith("[hu] "), f"the notice's text was not translated: {message[:40]!r}"
    assert title.startswith("[hu] "), f"the notice's title was not translated: {title!r}"


def test_the_notice_says_what_it_said_in_english(monkeypatch):
    module, shown = _load(monkeypatch, lambda s: s)
    module._show_notice_deferred()
    message, title = shown[0]
    assert title == "TGSpeechBox — Settings Migrated"
    assert message.startswith("TGSpeechBox has migrated your settings from the old NV Speech Player configuration.\n\n")
    assert message.endswith("https://github.com/nvaccess/nvSpeechPlayer")
    assert "NVDA menu > Tools > Manage add-ons" in message


def test_the_notice_still_shows_without_translations(monkeypatch):
    """An NVDA whose addonHandler cannot load a catalog: English, not an error."""
    module, shown = _load(monkeypatch, lambda s: s)
    broken = types.ModuleType("addonHandler")

    def initTranslation():
        raise RuntimeError("no catalog")

    broken.initTranslation = initTranslation
    monkeypatch.setitem(sys.modules, "addonHandler", broken)
    spec = importlib.util.spec_from_file_location("_tgsb_migrate_config_plain", MODULE)
    plain = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(plain)
    plain._show_notice_deferred()
    assert shown and shown[-1][1] == "TGSpeechBox — Settings Migrated"


def test_every_text_the_module_shows_goes_through_gettext():
    """The static side: initTranslation is called, and gui.messageBox is
    never handed a bare string."""
    tree = ast.parse(MODULE.read_text(encoding="utf-8"))
    calls = [n for n in ast.walk(tree) if isinstance(n, ast.Call) and isinstance(n.func, ast.Attribute)]
    assert any(c.func.attr == "initTranslation" and getattr(c.func.value, "id", "") == "addonHandler"
               for c in calls), "migrate_config.py never calls addonHandler.initTranslation()"
    boxes = [c for c in calls if c.func.attr == "messageBox"]
    assert boxes, "the notice's gui.messageBox call was not found"
    for box in boxes:
        for arg in box.args[:2]:
            assert (isinstance(arg, ast.Call) and getattr(arg.func, "id", "") == "_"), (
                f"line {arg.lineno}: text handed to gui.messageBox without _()")

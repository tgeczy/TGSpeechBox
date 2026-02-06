# -*- coding: utf-8 -*-
"""TGSpeechBox — global plugin.

This registers the TGSpeechBox language-pack settings panel as a category
inside NVDA's Settings dialog.

Important:
We do this from a global plugin (instead of from the synth driver) to match the
approach used by the IBMTTS add-on. It is reliable across NVDA 2024.1 .. 2026.1
because global plugins are initialized when NVDA's GUI is available.
"""

from __future__ import annotations

import globalPluginHandler
import globalVars

from logHandler import log

try:
    import addonHandler

    addonHandler.initTranslation()
except Exception:
    # Translation isn't critical for registration.
    pass


_panelClass = None


def _patchLanguageChangeRefreshInVoicePanel() -> None:
    """Refresh YAML-backed controls when the user changes TGSpeechBox's language.

    NVDA updates synth settings immediately as the user changes them, but the
    Voice settings panel only triggers a full driver-settings refresh when the
    *voice* setting changes.

    TGSpeechBox has several settings whose values come from the currently
    selected language-pack YAML. When the user changes the "Language" combo box,
    those other controls should be repopulated from the newly selected pack.

    We accomplish this by monkey-patching NVDA's StringDriverSettingChanger so
    that, for TGSpeechBox only, changing the "language" setting calls
    updateDriverSettings(...).

    This keeps compatibility across NVDA 2024.1 .. 2026.1 without requiring
    changes to NVDA core.
    """

    try:
        import gui

        sd = getattr(gui, "settingsDialogs", None)
        if not sd:
            return
        changerCls = getattr(sd, "StringDriverSettingChanger", None)
        if changerCls is None:
            return

        # Avoid double patching.
        if getattr(changerCls, "_tgSpeechBox_languageRefreshPatched", False):
            return

        origCall = getattr(changerCls, "__call__", None)
        if not callable(origCall):
            return

        def _patchedCall(self, evt):
            # First run NVDA's original handler.
            origCall(self, evt)

            # Then refresh other controls if the user just changed the language
            # for TGSpeechBox.
            try:
                if getattr(getattr(self, "setting", None), "id", None) != "language":
                    return
                drv = getattr(self, "driver", None)
                if drv is None:
                    return
                if getattr(drv, "name", None) != "tgSpeechBox":
                    return
                container = getattr(self, "container", None)
                if container is None:
                    return
                updateFn = getattr(container, "updateDriverSettings", None)
                if callable(updateFn):
                    updateFn(changedSetting="language")
            except Exception:
                # Never let GUI-refresh failures break the settings dialog.
                log.debug("TGSpeechBox: could not refresh voice panel after language change", exc_info=True)

        changerCls.__call__ = _patchedCall  # type: ignore[assignment]
        changerCls._tgSpeechBox_languageRefreshPatched = True
        changerCls._tgSpeechBox_languageRefreshOrigCall = origCall

        log.debug("TGSpeechBox: patched StringDriverSettingChanger for language refresh")
    except Exception:
        log.debug("TGSpeechBox: failed to patch voice panel language refresh", exc_info=True)


def _getSettingsDialogClass():
    """Return the settings dialog class (varies slightly across NVDA versions)."""
    try:
        import gui

        return getattr(gui.settingsDialogs, "NVDASettingsDialog", None) or getattr(gui.settingsDialogs, "SettingsDialog", None)
    except Exception:
        return None


def _registerPanel() -> None:
    """Register the language-pack settings panel.

    This intentionally mirrors what IBMTTS does:
      gui.settingsDialogs.NVDASettingsDialog.categoryClasses.append(...)
    """
    global _panelClass

    try:
        # Import here so the module isn't imported at add-on load time in secure mode.
        from synthDrivers.tgSpeechBox import langPackSettingsPanel

        getPanelCls = getattr(langPackSettingsPanel, "_getPanelClass", None)
        panelCls = getPanelCls() if callable(getPanelCls) else None
        if panelCls is None:
            log.error("TGSpeechBox: settings panel class could not be created; GUI may not be ready")
            return

        dlgCls = _getSettingsDialogClass()
        if dlgCls is None:
            log.error("TGSpeechBox: could not locate NVDA Settings dialog class")
            return

        cats = getattr(dlgCls, "categoryClasses", None)
        if isinstance(cats, list):
            if panelCls not in cats:
                cats.append(panelCls)
            _panelClass = panelCls
            return

        # Fallback: try registerCategory if categoryClasses isn't a mutable list.
        fn = getattr(dlgCls, "registerCategory", None)
        if callable(fn):
            fn(panelCls)
            _panelClass = panelCls
            return

        log.error("TGSpeechBox: unable to register settings panel (no supported registration API)")
    except Exception:
        log.error("TGSpeechBox: unable to register language-pack settings panel", exc_info=True)


def _unregisterPanel() -> None:
    """Best-effort unregister.

    NVDA doesn't normally hot-reload add-ons during a session, but removing the
    panel on terminate keeps behavior consistent with other add-ons.
    """

    global _panelClass
    if not _panelClass:
        return

    try:
        dlgCls = _getSettingsDialogClass()
        if not dlgCls:
            _panelClass = None
            return

        cats = getattr(dlgCls, "categoryClasses", None)
        if isinstance(cats, list) and _panelClass in cats:
            cats.remove(_panelClass)
    except Exception:
        pass

    _panelClass = None


class GlobalPlugin(globalPluginHandler.GlobalPlugin):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

        # Don't load add-on UI in secure mode (Windows logon/UAC screens).
        if getattr(globalVars.appArgs, "secure", False):
            return

        _registerPanel()
        _patchLanguageChangeRefreshInVoicePanel()

    def terminate(self):
        try:
            if not getattr(globalVars.appArgs, "secure", False):
                _unregisterPanel()
        except Exception:
            pass

        super().terminate()
# -*- coding: utf-8 -*-
"""TGSpeechBox - NVDA synth driver (modernized)

Pipeline:
- eSpeak (NVDA built-in) for text -> IPA/phonemes
- nvspFrontend.dll for IPA -> timed SpeechPlayer frames
- speechPlayer.dll for frame -> PCM synthesis
"""

from __future__ import annotations

import ctypes
import math
import os
import queue
import threading
from collections import OrderedDict
from typing import Optional

import config
import nvwave

from logHandler import log
from synthDrivers import _espeak
from synthDriverHandler import SynthDriver, VoiceInfo, synthDoneSpeaking, synthIndexReached

# NVDA command classes moved around across versions; keep imports tolerant.
try:
    from speech.commands import IndexCommand, PitchCommand
except Exception:
    try:
        from speech.commands import IndexCommand  # type: ignore
        PitchCommand = None  # type: ignore
    except Exception:
        import speech  # fallback
        IndexCommand = getattr(speech, "IndexCommand", None)
        PitchCommand = getattr(speech, "PitchCommand", None)

from autoSettingsUtils.driverSetting import DriverSetting, NumericDriverSetting

# BooleanDriverSetting exists in modern NVDA, but keep a safe fallback for older builds.
try:
    from autoSettingsUtils.driverSetting import BooleanDriverSetting  # type: ignore
except Exception:  # pragma: no cover
    BooleanDriverSetting = None  # type: ignore

# Local module imports
from . import speechPlayer
from ._dll_utils import findDllDir
from ._frontend import NvspFrontend

# Import from modularized components
from .constants import (
    languages, pauseModes, sampleRates, voices,
    VOICE_PROFILE_PREFIX, COALESCE_MAX_CHARS, COALESCE_MAX_INDEXES
)
from .text_utils import (
    re_textPause, normalizeTextForEspeak, looksLikeSentenceEnd,
    splitByScript,
)
from .profile_utils import (
    discoverVoiceProfiles, buildVoiceOps, applyVoiceToFrame
)
from .audio import BgThread, AudioThread
from .migrate_config import run as _migrate_config


try:
    import addonHandler
    addonHandler.initTranslation()
except Exception:
    def _(s): return s

# Pre-calculate per-voice operations for fast application
_frameFieldNames = {x[0] for x in speechPlayer.Frame._fields_}
_voiceOps = buildVoiceOps(voices, _frameFieldNames)
del _frameFieldNames

# Wrapper function for backward compatibility (uses module-level _voiceOps)
def _applyVoiceToFrame(frame: speechPlayer.Frame, voiceName: str) -> None:
    applyVoiceToFrame(frame, voiceName, _voiceOps)


class SynthDriver(SynthDriver):
    name = "tgSpeechBox"
    description = "TGSpeechBox"

    _supportedSettings = [
        SynthDriver.VoiceSetting(),
        SynthDriver.RateSetting(),
        SynthDriver.PitchSetting(),
        SynthDriver.InflectionSetting(),
        SynthDriver.VolumeSetting(),
        NumericDriverSetting("voiceTilt", _("Voice tilt (brightness)"), defaultVal=50),
        NumericDriverSetting("noiseGlottalMod", _("Noise glottal modulation"), defaultVal=0),
        NumericDriverSetting("pitchSyncF1", _("Pitch-sync F1 delta"), defaultVal=50),
        NumericDriverSetting("pitchSyncB1", _("Pitch-sync B1 delta"), defaultVal=50),
        NumericDriverSetting("speedQuotient", _("Speed quotient (voice tension)"), defaultVal=50),
        NumericDriverSetting("aspirationTilt", _("Aspiration tilt (breath color)"), defaultVal=50),
        NumericDriverSetting("cascadeBwScale", _("Formant sharpness (cascade bandwidth)"), defaultVal=50),
        NumericDriverSetting("voiceTremor", _("Voice tremor (shakiness)"), defaultVal=0),
        # FrameEx voice quality params (DSP v5+) - for creaky voice, breathiness, etc.
        NumericDriverSetting("frameExCreakiness", _("Creakiness (laryngealization)"), defaultVal=0),
        NumericDriverSetting("frameExBreathiness", _("Breathiness"), defaultVal=0),
        NumericDriverSetting("frameExJitter", _("Jitter (pitch variation)"), defaultVal=0),
        NumericDriverSetting("frameExShimmer", _("Shimmer (amplitude variation)"), defaultVal=0),
        NumericDriverSetting("frameExSharpness", _("Glottal sharpness"), defaultVal=50),
        NumericDriverSetting("legacyPitchInflectionScale",
            _("Legacy pitch inflection scale (only active when pitch mode is classic)"),
            defaultVal=29),
        DriverSetting("pauseMode", _("Pause mode")),
        DriverSetting("sampleRate", _("Sample rate")),
        DriverSetting("language", _("Language")),
        # Runtime language adjustments (other settings are YAML-only now)
        DriverSetting("stopClosureMode", _("Stop closure mode")),
        DriverSetting("spellingDiphthongMode", _("Spelling diphthong mode")),
    ]

    # Only expose legacyPitchMode combo - all other booleans are YAML-only.
    _supportedSettings.append(
        DriverSetting("legacyPitchMode", _("Pitch mode")),
    )

    supportedSettings = tuple(_supportedSettings)

    supportedCommands = {c for c in (IndexCommand, PitchCommand) if c}
    supportedNotifications = {synthIndexReached, synthDoneSpeaking}

    exposeExtraParams = False
    _ESPEAK_PHONEME_MODE = 0x36100 + 0x82

    def __init__(self):
        # Step 0: One-time config migration (nvSpeechPlayer -> tgSpeechBox)
        _migrate_config()

        # =======================================================================
        # CRITICAL: Initialize ALL internal state BEFORE calling super().__init__()
        # because super().__init__() triggers NVDA to restore saved settings,
        # which calls our setters (_set_voice, _set_language, etc.)
        # =======================================================================
        
        # 1. Initialize default values for all instance variables
        #    so property getters/setters don't crash if called early
        self._curPitch = 50
        self._curVoice = "Adam"
        self._curInflection = 0.5
        self._curVolume = 1.0
        self._curRate = 1.0
        self._curVoiceTilt = 50
        self._curNoiseGlottalMod = 0
        self._curPitchSyncF1 = 50
        self._curPitchSyncB1 = 50
        self._curSpeedQuotient = 50  # Maps to 2.0 (neutral)
        self._curAspirationTilt = 50  # Maps to 0.0 dB/oct (no tilt)
        self._curCascadeBwScale = 50  # Maps to 1.0 (no scaling)
        self._curVoiceTremor = 0     # Maps to 0.0 (no tremor)
        # FrameEx voice quality params (DSP v5+)
        self._curFrameExCreakiness = 0
        self._curFrameExBreathiness = 0
        self._curFrameExJitter = 0
        self._curFrameExShimmer = 0
        self._curFrameExSharpness = 50  # 50 = neutral (1.0x multiplier)
        self._curLegacyPitchInflectionScale = 29  # 29 = 0.58 (pack.h default)
        self._perVoiceTilt = {}  # Per-voice tilt storage: {voiceName: tiltValue}
        self._perVoiceNoiseGlottalMod = {}
        self._perVoicePitchSyncF1 = {}
        self._perVoicePitchSyncB1 = {}
        self._perVoiceSpeedQuotient = {}
        self._perVoiceAspirationTilt = {}
        self._perVoiceCascadeBwScale = {}
        self._perVoiceVoiceTremor = {}
        self._perVoiceFrameExCreakiness = {}
        self._perVoiceFrameExBreathiness = {}
        self._perVoiceFrameExJitter = {}
        self._perVoiceFrameExShimmer = {}
        self._perVoiceFrameExSharpness = {}
        self._usingVoiceProfile = False
        self._activeProfileName = ""
        self._pauseMode = "short"
        self._language = "auto"
        self._resolvedLang = "en-us"
        self._langPackSettingsCache: dict[str, object] = {}
        self._sampleRate = 16000
        
        # Initialize containers immediately to avoid NoneType errors
        self._voiceProfiles = []
        
        # Suppress YAML writes during NVDA config replay (YAML is source of truth)
        self._suppressLangPackWrites = True

        # 2. Check architecture compatibility
        if ctypes.sizeof(ctypes.c_void_p) not in (4, 8):
            raise RuntimeError('TGSpeechBox: unsupported Python architecture')

        # 3. Handle extra params if enabled
        if self.exposeExtraParams:
            self._extraParamNames = [x[0] for x in speechPlayer.Frame._fields_]
            self._extraParamAttrNames = [f"speechPlayer_{x}" for x in self._extraParamNames]

            extraSettings = tuple(
                NumericDriverSetting(attrName, f"Frame: {paramName}")
                for paramName, attrName in zip(self._extraParamNames, self._extraParamAttrNames)
            )
            self.supportedSettings = self.supportedSettings + extraSettings
            for attrName in self._extraParamAttrNames:
                setattr(self, attrName, 50)

        # 4. Setup paths and validate packs directory
        here = os.path.dirname(__file__)
        packsDir = os.path.join(here, "packs")
        self._packsDir = packsDir

        if not os.path.isdir(packsDir):
            raise RuntimeError(f"TGSpeechBox: missing packs directory at {packsDir}")

        # Validate required pack files
        requiredRel = [
            "phonemes.yaml",
            os.path.join("lang", "default.yaml"),
        ]
        missingRel = []
        for rel in requiredRel:
            if not os.path.isfile(os.path.join(packsDir, rel)):
                missingRel.append(rel)

        if missingRel:
            raise RuntimeError(f"TGSpeechBox: missing required packs: {', '.join(missingRel)}")

        # 5. Initialize core components (Player and Frontend)
        #    These MUST be ready before super().__init__() calls our setters
        self._player = speechPlayer.SpeechPlayer(self._sampleRate)

        dllDir = findDllDir(here)
        if not dllDir:
            raise RuntimeError('TGSpeechBox: could not find DLLs for this architecture')
        
        feDllPath = os.path.join(dllDir, 'nvspFrontend.dll')
        self._frontend = NvspFrontend(feDllPath, packsDir)

        if not self._frontend.setLanguage("default"):
            log.warning(f"TGSpeechBox: failed to load default pack: {self._frontend.getLastError()}")

        # Push initial FrameEx defaults to frontend (ABI v2+)
        self._pushFrameExDefaultsToFrontend()

        # 6. Discover voice profiles
        #    Try frontend API first (ABI v2+), fall back to Python parsing
        #    This MUST be done before super().__init__() so availableVoices is populated
        try:
            # Try frontend API first
            self._voiceProfiles = self._frontend.getVoiceProfileNames() if self._frontend.hasFrameExSupport() else []
            if not self._voiceProfiles:
                # Fall back to Python parsing
                self._voiceProfiles = discoverVoiceProfiles(packsDir) or []
            if self._voiceProfiles:
                log.info(f"TGSpeechBox: discovered voice profiles: {self._voiceProfiles}")
        except Exception as e:
            log.error(f"TGSpeechBox: error discovering voice profiles: {e}")
            self._voiceProfiles = []

        # Check for pack warnings
        if self._frontend.hasVoiceProfileSupport():
            warnings = self._frontend.getPackWarnings()
            if warnings:
                log.warning(f"TGSpeechBox: pack warnings: {warnings}")

        # 7. Initialize audio system
        self._audio = AudioThread(self, self._player, self._sampleRate)
        self._bgQueue: "queue.Queue" = queue.Queue()
        self._bgStop = threading.Event()
        self._speakGen = 0  # Generation counter: cancel/speak race guard
        self._bgThread = BgThread(self._bgQueue, self._bgStop, onError=self._onBgThreadError)
        self._bgThread.start()

        # 8. Initialize eSpeak
        self._espeakReady = False
        try:
            _espeak.initialize()
            
            # Set a default voice in espeak - required before espeak_TextToPhonemes works
            # Use American English as default since that's the most common; NVDA will set 
            # the correct language later when it restores settings via _set_language.
            # Try multiple formats since espeak can be picky about language codes.
            espeakVoiceSet = False
            for tryLang in ("en-us", "en-US", "en_us", "en_US"):
                try:
                    result = _espeak.setVoiceByLanguage(tryLang)
                    if result is None or result:
                        espeakVoiceSet = True
                        break
                except Exception:
                    continue
            
            if not espeakVoiceSet:
                # Last resort fallback to generic English
                try:
                    _espeak.setVoiceByLanguage("en")
                except Exception:
                    log.debug("TGSpeechBox: failed to set default espeak voice", exc_info=True)
            
            # Verify espeak is actually usable by checking if the DLL and function exist
            espeakDLL = getattr(_espeak, "espeakDLL", None)
            if espeakDLL and hasattr(espeakDLL, "espeak_TextToPhonemes"):
                # Fix espeak_TextToPhonemes prototype for 64-bit Python
                _ttp = espeakDLL.espeak_TextToPhonemes
                _ttp.argtypes = (ctypes.POINTER(ctypes.c_void_p), ctypes.c_int, ctypes.c_int)
                _ttp.restype = ctypes.c_void_p
                self._espeakReady = True
                log.debug("TGSpeechBox: espeak initialized successfully")
            else:
                log.warning("TGSpeechBox: espeak DLL or espeak_TextToPhonemes not available")
        except Exception:
            log.warning("TGSpeechBox: failed to initialize espeak", exc_info=True)

        # =======================================================================
        # 9. NOW call super().__init__()
        #    This triggers NVDA to load config and call our setters
        #    Since everything above is ready, it will succeed
        # =======================================================================
        super().__init__()

        # =======================================================================
        # 10. Post-init tasks (after NVDA has restored settings)
        # =======================================================================
        
        self._scheduleEnableLangPackWrites()
        self._refreshLangPackSettingsCache()

        # Preload optional language packs
        for tag in ("bg", "zh", "hu", "pt", "pl", "es"):
            try:
                self._frontend.setLanguage(tag)
            except Exception:
                pass

        # Schedule deferred re-application of voice profile
        # (in case NVDA's settings restore missed something)
        try:
            import wx
            wx.CallAfter(self._reapplyVoiceProfile)
        except Exception:
            # If wx isn't available yet, try a threaded approach
            def _deferred_reapply():
                import time
                time.sleep(0.1)
                try:
                    self._reapplyVoiceProfile()
                except Exception:
                    pass
            t = threading.Thread(target=_deferred_reapply, daemon=True)
            t.start()
    @classmethod
    def check(cls):
        # Ensure DLLs exist for this NVDA / Python architecture (x86 vs x64).
        if ctypes.sizeof(ctypes.c_void_p) not in (4, 8):
            return False

        here = os.path.dirname(__file__)
        dllDir = findDllDir(here)
        if not dllDir:
            return False

        # Packs are expected in a local ./packs folder.
        packsDir = os.path.join(here, 'packs')
        if not os.path.isdir(packsDir):
            return False

        # Accept either casing on Windows, but check for both to be explicit.
        phonemesLower = os.path.join(packsDir, 'phonemes.yaml')
        phonemesUpper = os.path.join(packsDir, 'Phonemes.yaml')
        if not (os.path.isfile(phonemesLower) or os.path.isfile(phonemesUpper)):
            return False

        # default.yaml is required (others are optional at runtime).
        defaultYaml = os.path.join(packsDir, 'lang', 'default.yaml')
        if not os.path.isfile(defaultYaml):
            return False

        return True

    def _get_availableLanguages(self):
        return languages

    # ---- Punctuation pause mode (driver setting) ----

    def _get_availablePauseModes(self):
        return pauseModes

    # Alias for NVDA 2023.x compatibility (capitalize() lowercases the remainder).
    _get_availablePausemodes = _get_availablePauseModes

    def _get_pauseMode(self):
        return getattr(self, "_pauseMode", "short")

    def _set_pauseMode(self, mode):
        try:
            m = str(mode or "").strip().lower()
            if m not in pauseModes:
                m = "short"
            self._pauseMode = m
        except Exception:
            pass

    # ---- Sample rate (driver setting) ----

    def _get_availableSampleRates(self):
        return sampleRates

    # Alias for NVDA 2023.x compatibility (capitalize() lowercases the remainder).
    _get_availableSamplerates = _get_availableSampleRates

    def _get_sampleRate(self):
        return str(getattr(self, "_sampleRate", 16000))

    def _set_sampleRate(self, rate):
        try:
            try:
                r = int(str(rate).strip())
            except (ValueError, TypeError):
                r = 22050
            if str(r) not in sampleRates:
                r = 22050
            
            # Only reinitialize if rate actually changed
            if r == getattr(self, "_sampleRate", None):
                return
            
            self._sampleRate = r
            
            # Only reinitialize if audio system already exists (not during initial construction)
            if hasattr(self, "_audio") and self._audio:
                self._reinitializeAudio()
        except Exception:
            log.debug("TGSpeechBox: _set_sampleRate failed", exc_info=True)

    def _reinitializeAudio(self):
        """Reinitialize audio subsystem after sample rate change."""
        try:
            self.cancel()
        except Exception:
            log.debug("TGSpeechBox: cancel failed during audio reinit", exc_info=True)
        
        try:
            # Terminate old audio thread
            if hasattr(self, "_audio") and self._audio:
                self._audio.terminate()
            
            # Terminate old player
            if hasattr(self, "_player") and self._player:
                self._player.terminate()
            
            # Create new player with new sample rate
            self._player = speechPlayer.SpeechPlayer(self._sampleRate)
            
            # Create new audio thread
            self._audio = AudioThread(self, self._player, self._sampleRate)
            
            # Reapply voicing tone to the new player
            # This must be done for ALL voices (profiles and Python presets)
            # to restore the user's tilt slider setting
            curVoice = getattr(self, "_curVoice", "Adam") or "Adam"
            if curVoice.startswith(VOICE_PROFILE_PREFIX):
                profileName = curVoice[len(VOICE_PROFILE_PREFIX):]
            else:
                profileName = ""
            self._applyVoicingTone(profileName)
            
        except Exception:
            log.error("TGSpeechBox: failed to reinitialize audio", exc_info=True)

    def _get_language(self):
        return getattr(self, "_language", "auto")

    @staticmethod
    def _resolveAutoLang() -> str:
        """Map NVDA's UI language to the closest available TGSpeechBox language tag."""
        try:
            import languageHandler
            nvdaLang = (languageHandler.getLanguage() or "en").strip().lower().replace("_", "-")
        except Exception:
            return "en-us"
        # Exact match first (e.g. "es-mx", "en-gb", "pt-br")
        if nvdaLang in languages and nvdaLang != "auto":
            return nvdaLang
        # Try base language (e.g. "en" from "en-au")
        base = nvdaLang.split("-", 1)[0] if "-" in nvdaLang else nvdaLang
        if base in languages and base != "auto":
            return base
        # Try common regional defaults
        _REGION_DEFAULTS = {
            "en": "en-us", "es": "es", "pt": "pt-br",
        }
        if base in _REGION_DEFAULTS:
            return _REGION_DEFAULTS[base]
        # Check if any language tag starts with the base (e.g. "en" -> "en-us")
        for tag in languages:
            if tag != "auto" and tag.startswith(base + "-"):
                return tag
        return "en-us"

    def _set_language(self, langCode):
        # Normalize to pack-style language tag: lowercase with hyphens.
        requested = str(langCode or "").strip().lower().replace("_", "-")

        # Resolve "auto" to the NVDA UI language.
        if requested == "auto":
            self._language = "auto"
            resolved = self._resolveAutoLang()
        else:
            # Backward compatibility: older builds used "en" for UK.
            if requested == "en" and "en-gb" in languages:
                requested = "en-gb"
            # Validate against the exposed language list.
            if requested not in languages or requested == "auto":
                requested = "en-us"
            self._language = requested
            resolved = requested

        self._resolvedLang = resolved

        try:
            self.cancel()
        except Exception:
            log.debug("TGSpeechBox: cancel failed while changing language", exc_info=True)

        # Configure eSpeak for text->phonemes. This can fall back to a base language.
        espeakApplied = None

        # Candidate order:
        #   1) exact resolved tag (e.g. en-gb)
        #   2) underscore variant (en_gb) for older eSpeak builds
        #   3) base language (en) if region tag isn't supported
        #   4) final safety fallback: English
        candidates = []
        for c in (resolved, resolved.replace("-", "_")):
            if c and c not in candidates:
                candidates.append(c)

        if "-" in resolved:
            base = resolved.split("-", 1)[0]
            for c in (base, base.replace("-", "_")):
                if c and c not in candidates:
                    candidates.append(c)

        if "en" not in candidates:
            candidates.append("en")

        for tryCode in candidates:
            try:
                ok = _espeak.setVoiceByLanguage(tryCode)
                if ok is None or ok:
                    espeakApplied = tryCode
                    break
            except Exception:
                continue

        self._espeakLang = (espeakApplied or "").strip().lower().replace("_", "-")

        if espeakApplied is None:
            log.error(
                "TGSpeechBox: could not set eSpeak language for %r (tried %s)",
                resolved,
                candidates,
                exc_info=True,
            )

        # Keep frontend pack selection in sync with the resolved language tag.
        try:
            if getattr(self, "_frontend", None):
                self._applyFrontendLangTag(resolved)
        except Exception:
            log.error("TGSpeechBox: error setting frontend language", exc_info=True)

        log.debug("TGSpeechBox: language setting=%r resolved=%r; eSpeak=%r; packs=%r", self._language, resolved, self._espeakLang or None, getattr(self, "_frontendLangTag", None))
        # Refresh cached language-pack settings for the (possibly) new language.
        try:
            self._refreshLangPackSettingsCache()
        except Exception:
            log.debug("TGSpeechBox: could not refresh language-pack cache", exc_info=True)

        # Trigger a GUI refresh so checkboxes update to reflect the new language pack.
        self._scheduleSettingsPanelRefresh()

    def _scheduleSettingsPanelRefresh(self) -> None:
        """Schedule a refresh of the VoiceSettingsPanel to update checkboxes.

        When the user changes language, the language pack's settings may differ.
        This method finds the open settings panel (if any) and triggers an update
        so checkboxes reflect the new language pack's values.
        """
        try:
            import wx
            wx.CallAfter(self._doSettingsPanelRefresh)
        except Exception:
            pass

    def _doSettingsPanelRefresh(self) -> None:
        """Actually perform the settings panel refresh (called via wx.CallAfter)."""
        try:
            import wx
            from gui import settingsDialogs

            # Look for an open NVDASettingsDialog.
            for win in wx.GetTopLevelWindows():
                # Handle different NVDA versions: NVDASettingsDialog or SettingsDialog.
                dlgCls = getattr(settingsDialogs, "NVDASettingsDialog", None)
                if dlgCls is None:
                    dlgCls = getattr(settingsDialogs, "SettingsDialog", None)
                if dlgCls is None:
                    continue

                if not isinstance(win, dlgCls):
                    continue

                # Found the settings dialog. Now find the VoiceSettingsPanel.
                # Different NVDA versions store panels differently.
                panels = []

                # NVDA 2024+: catIdToInstanceMap
                if hasattr(win, "catIdToInstanceMap"):
                    panels.extend(win.catIdToInstanceMap.values())

                # Older NVDA: categoryClasses or similar
                if hasattr(win, "_categoryPanel"):
                    panels.append(win._categoryPanel)

                # Try currentCategory attribute
                if hasattr(win, "currentCategory"):
                    cat = win.currentCategory
                    if cat is not None:
                        panels.append(cat)

                voicePanelCls = getattr(settingsDialogs, "VoiceSettingsPanel", None)
                for panel in panels:
                    if panel is None:
                        continue
                    # Check if it's a VoiceSettingsPanel or has updateDriverSettings
                    isVoicePanel = voicePanelCls and isinstance(panel, voicePanelCls)
                    hasUpdater = hasattr(panel, "updateDriverSettings")

                    if isVoicePanel or hasUpdater:
                        try:
                            panel.updateDriverSettings(changedSetting="language")
                        except Exception:
                            log.debug(
                                "TGSpeechBox: updateDriverSettings failed",
                                exc_info=True,
                            )
                        break
                break
        except Exception:
            # GUI may not be available (e.g., running in secure mode or during shutdown).
            log.debug("TGSpeechBox: could not refresh settings panel", exc_info=True)

    # ---- Startup guard for YAML writes (see __init__) ----

    def _enableLangPackWrites(self) -> None:
        """Re-enable writing language-pack settings back to YAML."""
        self._suppressLangPackWrites = False

    def _scheduleEnableLangPackWrites(self) -> None:
        """Schedule re-enabling YAML writes after NVDA finishes config replay."""
        try:
            import core
            callLater = getattr(core, "callLater", None)
            if callable(callLater):
                callLater(0, self._enableLangPackWrites)
                return
        except Exception:
            pass

        try:
            import wx
            if hasattr(wx, "CallAfter"):
                wx.CallAfter(self._enableLangPackWrites)
                return
        except Exception:
            pass

        self._enableLangPackWrites()

    # ---- Language-pack (YAML) helpers ----

    def _getCurrentLangTag(self) -> str:
        """Return the current resolved language tag in pack file format (lowercase, hyphen)."""
        return str(getattr(self, "_resolvedLang", "en-us") or "en-us").strip().lower().replace("_", "-")

    def _applyFrontendLangTag(self, tag: str) -> bool:
        """Ask the frontend to (re)load packs for *tag*, trying sensible fallbacks.

        Returns True if the frontend reported a successful load.
        """
        if not getattr(self, "_frontend", None):
            return False

        tag = str(tag or "default").strip().lower().replace("_", "-")
        candidates = [tag]
        if "-" in tag:
            candidates.append(tag.split("-", 1)[0])
        candidates.append("default")

        for cand in candidates:
            try:
                if self._frontend.setLanguage(cand):
                    self._frontendLangTag = cand
                    return True
            except Exception:
                log.debug("TGSpeechBox: frontend.setLanguage failed for %r", cand, exc_info=True)
                continue

        try:
            lastErr = self._frontend.getLastError()
        except Exception:
            log.debug("TGSpeechBox: frontend.getLastError failed", exc_info=True)
            lastErr = None
        log.error("TGSpeechBox: frontend could not load pack for %r (tried %s). %s", tag, candidates, lastErr)
        return False

    def reloadLanguagePack(self, tag: str | None = None) -> bool:
        """Public helper (used by the settings panel) to reload frontend packs.

        If *tag* is omitted, reloads the currently selected driver language.
        """
        ok = self._applyFrontendLangTag(tag or self._getCurrentLangTag())
        if ok:
            try:
                self._refreshLangPackSettingsCache()
            except Exception:
                log.debug("TGSpeechBox: could not refresh language-pack cache after reload", exc_info=True)
        return ok

    def _refreshLangPackSettingsCache(self) -> None:
        """Rebuild the cached effective YAML ``settings:`` map for the current language."""
        try:
            from . import langPackYaml

            packsDir = getattr(self, "_packsDir", None)
            if not packsDir:
                self._langPackSettingsCache = {}
                return

            self._langPackSettingsCache = langPackYaml.getEffectiveSettings(
                packsDir=packsDir,
                langTag=self._getCurrentLangTag(),
            )

            # Clear previous error key on success.
            if getattr(self, "_lastLangPackCacheErrorKey", None) is not None:
                self._lastLangPackCacheErrorKey = None
        except Exception as e:
            # Avoid "log spam" if a corrupt YAML causes repeated refresh failures.
            try:
                tag = self._getCurrentLangTag()
            except Exception:
                tag = None
            errKey = (tag, type(e).__name__, str(e))
            if getattr(self, "_lastLangPackCacheErrorKey", None) != errKey:
                log.error("TGSpeechBox: failed to read language-pack settings for %r", tag, exc_info=True)
                self._lastLangPackCacheErrorKey = errKey
            self._langPackSettingsCache = {}

    def _getLangPackBool(self, key: str, default: bool = False) -> bool:
        # Users may edit YAML on disk (either via our settings panel, or via a
        # text editor). Refresh the cache opportunistically so the values shown
        # in NVDA's GUI don't go stale and then get written back to disk.
        self._refreshLangPackSettingsCache()
        try:
            from . import langPackYaml

            raw = getattr(self, "_langPackSettingsCache", {}).get(key)
            return langPackYaml.parseBool(raw, default)
        except Exception:
            return default

    def _getLangPackStr(self, key: str, default: str = "") -> str:
        self._refreshLangPackSettingsCache()
        raw = getattr(self, "_langPackSettingsCache", {}).get(key)
        if raw is None:
            return default
        return str(raw)

    def _setLangPackSetting(self, key: str, value: object) -> None:
        """Write a language-pack ``settings:`` key and reload packs."""
        try:
            from . import langPackYaml

            # During driver initialization NVDA may replay persisted settings
            # from config.conf by calling our property setters. For YAML-backed
            # language-pack settings we treat YAML as authoritative, so we
            # suppress writes during that replay window to avoid overwriting
            # edits made via Notepad or our settings panel.
            if getattr(self, "_suppressLangPackWrites", False):
                return

            # Ensure our effective-value comparison reflects the current files
            # on disk (the YAML may have been edited externally).
            self._refreshLangPackSettingsCache()

            packsDir = getattr(self, "_packsDir", None)
            if not packsDir:
                return

            langTag = self._getCurrentLangTag()

            # Avoid churn if no effective change.
            cur = getattr(self, "_langPackSettingsCache", {}).get(key)
            try:
                if isinstance(value, bool):
                    # If cur is None, the key doesn't exist in YAML yet - always write it.
                    if cur is not None and langPackYaml.parseBool(cur, default=value) == value:
                        return
                else:
                    # Packs store scalars as strings; normalize whitespace for comparison.
                    if cur is not None and str(cur).strip() == str(value).strip():
                        return
            except Exception:
                log.debug(
                    "TGSpeechBox: error comparing language-pack setting %s", key, exc_info=True
                )

            langPackYaml.upsertSetting(
                packsDir=packsDir,
                langTag=langTag,
                key=key,
                value=value,
            )
            # Reload so the frontend re-reads updated YAML.
            self.reloadLanguagePack(langTag)
        except Exception:
            log.error("TGSpeechBox: failed to update language-pack setting %s", key, exc_info=True)

    def _choiceToIdStr(self, value: object) -> str:
        """Return the underlying id string for a combo-box choice.

        NVDA's settings UI may pass either the raw id string or an object
        (for example a VoiceInfo).
        """
        if value is None:
            return ""
        # Common attribute spellings used across NVDA versions.
        for attr in ("id", "ID"):
            try:
                v = getattr(value, attr)
            except Exception:
                continue
            if v is not None and v != "":
                return str(v)
        return str(value)

    # ---- Language-pack quick settings exposed in the synth settings dialog ----

    # NOTE:
    # NVDA's driver settings dialog historically computed the "available..." attribute
    # name for string settings using Python's str.capitalize(), which *lowercases* the
    # remainder of the setting id (e.g. stopClosureMode -> Stopclosuremode).
    # Newer NVDA versions preserve camelCase.
    #
    # For maximum compatibility (NVDA 2023.2+), we provide BOTH spellings for each
    # choice/enum setting.

    _STOP_CLOSURE_MODES = OrderedDict(
        (
            ("always", VoiceInfo("always", _("Always"))),
            ("after-vowel", VoiceInfo("after-vowel", _("After vowel"))),
            ("vowel-and-cluster", VoiceInfo("vowel-and-cluster", _("Vowel and cluster"))),
            ("none", VoiceInfo("none", _("None"))),
        )
    )

    _SPELLING_DIPHTHONG_MODES = OrderedDict(
        (
            ("none", VoiceInfo("none", _("None"))),
            ("monophthong", VoiceInfo("monophthong", _("Monophthong"))),
        )
    )

    _TONE_CONTOURS_MODES = OrderedDict(
        (
            ("absolute", VoiceInfo("absolute", _("Absolute"))),
            ("relative", VoiceInfo("relative", _("Relative"))),
        )
    )

    _COARTICULATION_ADJACENCY_MODES = OrderedDict(
        (
            ("0", VoiceInfo("0", _("Immediate neighbors only"))),
            ("1", VoiceInfo("1", _("Allow C_V (one consonant)"))),
            ("2", VoiceInfo("2", _("Allow CC_V (two consonants)"))),
        )
    )

    _LEGACY_PITCH_MODES = OrderedDict(
        (
            ("espeak_style", VoiceInfo("espeak_style", _("eSpeak style"))),
            ("fujisaki_style", VoiceInfo("fujisaki_style", _("Fujisaki"))),
            ("legacy", VoiceInfo("legacy", _("Classic"))),
        )
    )

    def _makeLangPackAccessors(attrName, yamlKey, kind="str", default=None, choices=None):
        """Generate _get/_set (and available* when needed) methods for YAML-backed settings."""

        def getter(self, _key=yamlKey, _default=default, _kind=kind):
            try:
                if _kind == "bool":
                    return self._getLangPackBool(_key, default=_default)
                return self._getLangPackStr(_key, default=_default)
            except Exception:
                return _default

        def setter(self, val, _key=yamlKey, _kind=kind):
            try:
                if _kind == "bool":
                    self._setLangPackSetting(_key, bool(val))
                else:
                    self._setLangPackSetting(_key, self._choiceToIdStr(val))
            except Exception:
                # Never crash during settings application
                pass

        accessors = {
            f"_get_{attrName}": getter,
            f"_set_{attrName}": setter,
        }

        if choices is not None:
            # Provide BOTH the camelCase spelling and the historical capitalize() spelling.
            camelPlural = attrName[0].upper() + attrName[1:] + "s"
            capPlural = attrName.capitalize() + "s"

            def availGetter(self, _choices=choices):
                return _choices

            accessors[f"_get_available{camelPlural}"] = availGetter
            accessors[f"_get_available{capPlural}"] = availGetter

        return accessors

    # Settings specs: (attrName, yamlKey, kind, default, choices)
    # - kind: "bool" or "enum" (treated as string)
    # - choices: OrderedDict for "enum" settings; None for bool settings.
    #
    # NOTE: Boolean settings for extra/optional features default to False so that
    # when the driver is first configured, it starts with a minimal/conservative
    # configuration. Users can then enable features as desired.
    _LANG_PACK_SPECS = (
        ("stopClosureMode", "stopClosureMode", "enum", "vowel-and-cluster", _STOP_CLOSURE_MODES),
        ("stopClosureClusterGapsEnabled", "stopClosureClusterGapsEnabled", "bool", False, None),
        ("stopClosureAfterNasalsEnabled", "stopClosureAfterNasalsEnabled", "bool", False, None),
        ("autoTieDiphthongs", "autoTieDiphthongs", "bool", False, None),
        ("autoDiphthongOffglideToSemivowel", "autoDiphthongOffglideToSemivowel", "bool", False, None),
        ("segmentBoundarySkipVowelToVowel", "segmentBoundarySkipVowelToVowel", "bool", False, None),
        ("segmentBoundarySkipVowelToLiquid", "segmentBoundarySkipVowelToLiquid", "bool", False, None),
        ("spellingDiphthongMode", "spellingDiphthongMode", "enum", "none", _SPELLING_DIPHTHONG_MODES),
        ("postStopAspirationEnabled", "postStopAspirationEnabled", "bool", False, None),
        # --- Coarticulation settings ---
        ("coarticulationEnabled", "coarticulationEnabled", "bool", False, None),
        ("coarticulationFadeIntoConsonants", "coarticulationFadeIntoConsonants", "bool", False, None),
        ("coarticulationVelarPinchEnabled", "coarticulationVelarPinchEnabled", "bool", False, None),
        ("coarticulationGraduated", "coarticulationGraduated", "bool", False, None),
        ("coarticulationAdjacencyMaxConsonants", "coarticulationAdjacencyMaxConsonants", "enum", "2", _COARTICULATION_ADJACENCY_MODES),
        # --- Phrase-final lengthening settings ---
        ("phraseFinalLengtheningEnabled", "phraseFinalLengtheningEnabled", "bool", False, None),
        ("phraseFinalLengtheningNucleusOnlyMode", "phraseFinalLengtheningNucleusOnlyMode", "bool", False, None),
        # --- Single-word tuning settings ---
        ("singleWordTuningEnabled", "singleWordTuningEnabled", "bool", True, None),
        ("singleWordClauseTypeOverrideCommaOnly", "singleWordClauseTypeOverrideCommaOnly", "bool", True, None),
        # --- Microprosody settings ---
        ("microprosodyEnabled", "microprosodyEnabled", "bool", False, None),
        ("microprosodyVoicelessF0RaiseEnabled", "microprosodyVoicelessF0RaiseEnabled", "bool", False, None),
        ("microprosodyVoicedF0LowerEnabled", "microprosodyVoicedF0LowerEnabled", "bool", False, None),
        # --- Rate reduction settings ---
        ("rateReductionEnabled", "rateReductionEnabled", "bool", False, None),
        # --- Nasalization settings ---
        ("nasalizationAnticipatoryEnabled", "nasalizationAnticipatoryEnabled", "bool", False, None),
        # --- Liquid dynamics settings ---
        ("liquidDynamicsEnabled", "liquidDynamics.enabled", "bool", False, None),
        # --- Length contrast settings ---
        ("lengthContrastEnabled", "lengthContrast.enabled", "bool", False, None),
        # --- Positional allophones settings ---
        ("positionalAllophonesEnabled", "positionalAllophones.enabled", "bool", False, None),
        ("positionalAllophonesGlottalReinforcementEnabled", "positionalAllophones.glottalReinforcement.enabled", "bool", False, None),
        # --- Boundary smoothing settings ---
        ("boundarySmoothingEnabled", "boundarySmoothing.enabled", "bool", False, None),
        # --- Trajectory limit settings ---
        ("trajectoryLimitEnabled", "trajectoryLimit.enabled", "bool", False, None),
        ("trajectoryLimitApplyAcrossWordBoundary", "trajectoryLimit.applyAcrossWordBoundary", "bool", False, None),
        # legacyPitchMode has custom accessors below (for bool→enum migration)
        ("tonal", "tonal", "bool", False, None),
        ("toneDigitsEnabled", "toneDigitsEnabled", "bool", False, None),
        ("toneContoursMode", "toneContoursMode", "enum", "absolute", _TONE_CONTOURS_MODES),
        ("stripAllophoneDigits", "stripAllophoneDigits", "bool", False, None),
        ("stripHyphen", "stripHyphen", "bool", False, None),
    )

    for _attrName, _yamlKey, _kind, _default, _choices in _LANG_PACK_SPECS:
        for _methName, _meth in _makeLangPackAccessors(
            _attrName,
            _yamlKey,
            kind=_kind,
            default=_default,
            choices=_choices,
        ).items():
            locals()[_methName] = _meth

    # Clean up generator helpers so they don't become part of the public driver API.
    del _makeLangPackAccessors, _LANG_PACK_SPECS, _attrName, _yamlKey, _kind, _default, _choices, _methName, _meth

    # Override legacyPitchMode accessor to handle migration from old boolean format.
    # Old YAML had: legacyPitchMode: false/true
    # New YAML has: legacyPitchMode: "espeak_style"/"fujisaki_style"/"legacy"
    def _get_legacyPitchMode(self):
        try:
            val = self._getLangPackStr("legacyPitchMode", default="")
            # Handle missing/empty value
            if not val:
                return "espeak_style"
            # Handle old boolean values (YAML bools become "True"/"False" strings)
            if val in ("true", "True", "1"):
                return "legacy"  # Old "true" meant classic/legacy mode
            if val in ("false", "False", "0"):
                return "espeak_style"  # Old "false" meant espeak style
            # Check if it's a valid new-style value
            if val in ("espeak_style", "fujisaki_style", "legacy"):
                return val
            # Unknown value, return default
            return "espeak_style"
        except Exception:
            return "espeak_style"

    def _set_legacyPitchMode(self, val):
        try:
            self._setLangPackSetting("legacyPitchMode", self._choiceToIdStr(val))
        except Exception:
            pass

    def _get_availableLegacyPitchModes(self):
        return self._LEGACY_PITCH_MODES

    def _get_availableLegacypitchmodes(self):
        return self._LEGACY_PITCH_MODES

    def _onBgThreadError(self):
        """Called by BgThread when an unhandled exception occurs.

        Ensures AudioThread doesn't hang waiting for frames that will never
        come (e.g. if _speakBg crashed after setting allFramesQueued=False).
        """
        if hasattr(self, "_audio") and self._audio:
            self._audio.allFramesQueued = True
            self._audio._framesReady.set()

    def _enqueue(self, func, *args, **kwargs):
        if self._bgStop.is_set():
            return
        self._bgQueue.put((func, args, kwargs))

    def _notifyIndexesAndDone(self, indexes, generation):
        # cancel() may have invalidated this generation while it was
        # queued in BgThread — don't fire a spurious synthDoneSpeaking.
        if generation != self._speakGen:
            return
        for i in indexes:
            synthIndexReached.notify(synth=self, index=i)
        synthDoneSpeaking.notify(synth=self)

    def _espeakTextToIPA(self, text: str) -> str:
        if not text:
            return ""
        
        # Safety check: ensure espeak is initialized and available
        if not getattr(self, "_espeakReady", False):
            log.debug("TGSpeechBox: espeak not ready, skipping IPA conversion")
            return ""
        
        espeakDLL = getattr(_espeak, "espeakDLL", None)
        if not espeakDLL:
            return ""
        
        textToPhonemes = getattr(espeakDLL, "espeak_TextToPhonemes", None)
        if not textToPhonemes:
            return ""
        
        textBuf = ctypes.create_unicode_buffer(text)
        textPtr = ctypes.c_void_p(ctypes.addressof(textBuf))
        chunks = []
        lastPtr = None
        while textPtr and textPtr.value:
            if lastPtr == textPtr.value:
                break
            lastPtr = textPtr.value
            try:
                phonemeBuf = textToPhonemes(
                    ctypes.byref(textPtr),
                    _espeak.espeakCHARS_WCHAR,
                    self._ESPEAK_PHONEME_MODE,
                )
            except OSError as e:
                # Access violation or other OS error - espeak might not be ready
                log.error(f"TGSpeechBox: espeak_TextToPhonemes failed: {e}")
                self._espeakReady = False  # Disable further attempts
                return ""
            if phonemeBuf:
                chunks.append(ctypes.string_at(phonemeBuf))
            else:
                break
        ipaBytes = b"".join(chunks)
        try:
            return ipaBytes.decode("utf8", errors="ignore").strip()
        except Exception:
            return ""

    def _espeakTextToIPA_scriptAware(self, text: str) -> str:
        """Convert text to IPA, switching eSpeak language for foreign-script runs.

        When the active language uses a non-Latin script (Russian, Bulgarian,
        Greek, etc.) and the text contains Latin-script words, eSpeak would
        produce garbage IPA for those words.  This method detects script
        boundaries, temporarily switches eSpeak to the Latin fallback language,
        and reassembles the IPA.
        """
        if not text:
            return ""

        latinFallback = getattr(self, "_latinFallbackLang", "en-gb")
        espeakLang = getattr(self, "_espeakLang", "en")

        segments = splitByScript(text, espeakLang, latinFallback)

        # Fast path: no script switching needed (single segment, base lang).
        if len(segments) == 1 and segments[0][1] is None:
            return self._espeakTextToIPA(text)

        ipaChunks = []
        currentLang = espeakLang

        for segText, langOverride in segments:
            if not segText or not segText.strip():
                continue

            # Switch eSpeak language if needed.
            targetLang = langOverride or espeakLang
            if targetLang != currentLang:
                try:
                    _espeak.setVoiceByLanguage(targetLang)
                    currentLang = targetLang
                except Exception:
                    pass  # Fall through with current language.

            ipa = self._espeakTextToIPA(segText)
            if ipa:
                ipaChunks.append(ipa)

        # Restore base language if we switched away.
        if currentLang != espeakLang:
            try:
                _espeak.setVoiceByLanguage(espeakLang)
            except Exception:
                pass

        return " ".join(ipaChunks)

    def _buildBlocks(self, speechSequence, coalesceSayAll: bool = False):
        """Convert an NVDA speechSequence into blocks: (text, [indexesAfterText], pitchOffset).

        When coalesceSayAll is True, we *delay* IndexCommands that occur mid-sentence.
        This prevents audible gaps when NVDA inserts index markers at visual line wraps
        during Say All.
        """
        blocks = []  # list[tuple[str, list[int], int]]
        textBuf = []
        pendingIndexes = []
        seenNonEmptyText = False

        pitchOffset = 0
        bufPitchOffset = pitchOffset

        def flush():
            nonlocal seenNonEmptyText, bufPitchOffset
            raw = normalizeTextForEspeak(" ".join(textBuf))
            textBuf.clear()
            blocks.append((raw, pendingIndexes.copy(), bufPitchOffset))
            pendingIndexes.clear()
            seenNonEmptyText = False
            bufPitchOffset = pitchOffset

        for item in speechSequence:
            # Treat pitch changes as hard boundaries.
            if PitchCommand and isinstance(item, PitchCommand):
                if textBuf or pendingIndexes:
                    flush()
                pitchOffset = getattr(item, "offset", 0) or 0
                bufPitchOffset = pitchOffset
                continue

            if isinstance(item, str):
                if item:
                    if not textBuf and not pendingIndexes:
                        bufPitchOffset = pitchOffset
                    textBuf.append(item)
                    if item.strip():
                        seenNonEmptyText = True
                continue

            if IndexCommand and isinstance(item, IndexCommand):
                # Leading indexes (no text yet) should fire immediately.
                if not seenNonEmptyText and not textBuf:
                    blocks.append(("", [item.index], pitchOffset))
                    continue

                pendingIndexes.append(item.index)

                if not coalesceSayAll:
                    flush()
                    continue

                # Coalesce across wrapped lines: flush only at a "real" boundary
                # (sentence end) or if the buffer becomes too large.
                safeSoFar = normalizeTextForEspeak(" ".join(textBuf))
                if (
                    looksLikeSentenceEnd(safeSoFar)
                    or len(safeSoFar) >= COALESCE_MAX_CHARS
                    or len(pendingIndexes) >= COALESCE_MAX_INDEXES
                ):
                    flush()
                continue

            # Ignore other command types.

        # Trailing text (and/or delayed indexes)
        if textBuf or pendingIndexes:
            flush()

        # Remove trailing empty blocks with no indexes
        while blocks and (not blocks[-1][0]) and (not blocks[-1][1]):
            blocks.pop()

        return blocks

    def speak(self, speechSequence):
        indexes = []
        anyText = False
        for item in speechSequence:
            if IndexCommand and isinstance(item, IndexCommand):
                indexes.append(item.index)
            elif isinstance(item, str) and item.strip():
                anyText = True

        if (not anyText):
            self._enqueue(self._notifyIndexesAndDone, indexes, self._speakGen)
            return

        # Stamp this speak with the current generation so cancel() can invalidate it.
        # IMPORTANT: Do NOT increment here — only cancel() bumps the counter.
        # If speak() bumped it, a benign second speak() (e.g. from synthDoneSpeaking)
        # would kill an in-flight _speakBg that was never cancelled.
        self._enqueue(self._speakBg, list(speechSequence), self._speakGen)

    def _speakBg(self, speakList, generation):
        # Bail immediately if a cancel() already invalidated this generation
        if generation != self._speakGen:
            return
        hadRealSpeech = False
        hadKickedAudio = False  # streaming: kick AudioThread after first chunk
        hasIndex = bool(IndexCommand) and any(isinstance(i, IndexCommand) for i in speakList)
        blocks = self._buildBlocks(speakList, coalesceSayAll=hasIndex)

        endPause = 0.0
        leadingSilenceMs = 1.0
        minFadeInMs = 3.0
        minFadeOutMs = 3.0
        lastStreamWasVoiced = False
        pauseMode = str(getattr(self, "_pauseMode", "short") or "short").strip().lower()

        def _punctuationPauseMs(punctToken: str | None) -> float:
            """Return pause duration in ms for punctuation.
            
            Short mode: subtle pauses for natural flow
            Long mode: deliberate pauses for clarity
            """
            if not punctToken or pauseMode == "off":
                return 0.0
            if punctToken in (".", "!", "?", "...", ":", ";"):
                return 60.0 if pauseMode == "long" else 35.0
            if punctToken == ",":
                return 50.0 if pauseMode == "long" else 25.0
            return 0.0

        for (text, indexesAfter, blockPitchOffset) in blocks:
            # Bail if cancel() invalidated this generation
            if generation != self._speakGen:
                return

            # Speak text for this block.
            if text:
                for chunk in re_textPause.split(text):
                    # Check again between chunks for fast cancellation
                    if generation != self._speakGen:
                        return

                    if not chunk:
                        continue

                    chunk = normalizeTextForEspeak(chunk)
                    if not chunk:
                        continue

                    # Determine punctuation at the *end* of the chunk.
                    # This influences two things:
                    # - clauseType passed to the frontend (intonation hints)
                    # - optional micro-pause insertion after the chunk
                    punctToken = None
                    s = chunk.rstrip()
                    if s.endswith("..."):
                        punctToken = "..."
                        # Frontend only reads 1 byte; treat ellipsis as '.' for prosody.
                        clauseType = "."
                    elif s and (s[-1] in ".?!,:;"):
                        punctToken = s[-1]
                        clauseType = s[-1]
                    elif s and (s[-1] == ","):
                        punctToken = ","
                        clauseType = ","
                    else:
                        clauseType = None

                    punctPauseMs = _punctuationPauseMs(punctToken)

                    ipaText = self._espeakTextToIPA_scriptAware(chunk)
                    if not ipaText:
                        # Nothing speakable, but don't drop indexes (they are queued after the block).
                        continue

                    pitch = float(self._curPitch) + float(blockPitchOffset)
                    basePitch = 25.0 + (21.25 * (pitch / 12.5))

                    # nvspFrontend.dll: IPA -> frames.
                    queuedCount = 0

                    # Some generators (including nvspFrontend) may emit an initial silence
                    # frame for each queued utterance. When NVDA feeds us multiple chunks
                    # back-to-back (e.g. Say All reading "visual" lines), that redundant
                    # leading silence can become a perceptible pause. Once we've already
                    # queued real speech for this speak operation (or we're appending to
                    # existing output), suppress those leading silence frames.
                    suppressLeadingSilence = hadRealSpeech or bool(getattr(self._audio, "isSpeaking", False))
                    sawRealFrameInThisUtterance = False
                    sawSilenceAfterVoice = False

                    # Pre-calculate extra parameter multipliers once per utterance.
                    # This avoids repeated getattr(self, f"speechPlayer_{p}") lookups on every frame.
                    extraParamMultipliers = ()
                    if self.exposeExtraParams:
                        try:
                            names = getattr(self, "_extraParamNames", ()) or ()
                            attrNames = getattr(self, "_extraParamAttrNames", None)
                            if not attrNames or len(attrNames) != len(names):
                                attrNames = [f"speechPlayer_{x}" for x in names]

                            pairs = []
                            for paramName, attrName in zip(names, attrNames):
                                try:
                                    ratio = float(getattr(self, attrName, 50)) / 50.0
                                except Exception:
                                    continue
                                # Skip default (ratio=1.0) to keep the per-frame hot path tiny.
                                if ratio != 1.0:
                                    pairs.append((paramName, ratio))

                            extraParamMultipliers = tuple(pairs)
                        except Exception:
                            extraParamMultipliers = ()

                    def _onFrame(framePtr, frameExPtr, frameDuration, fadeDuration, idxToSet):
                        nonlocal queuedCount, hadRealSpeech, sawRealFrameInThisUtterance, sawSilenceAfterVoice, lastStreamWasVoiced

                        # Bail immediately if cancel() invalidated this generation mid-word
                        if generation != self._speakGen:
                            return

                        # Reduce leading silence frames to minimize gaps
                        if (not framePtr) and suppressLeadingSilence and (not sawRealFrameInThisUtterance):
                            dur = min(float(frameDuration), float(leadingSilenceMs))
                            if dur > 0:
                                self._player.queueFrame(None, dur, min(float(fadeDuration), dur), userIndex=idxToSet)
                                queuedCount += 1
                                lastStreamWasVoiced = False
                            return

                        # Silence frame: queue as pause
                        if not framePtr:
                            if sawRealFrameInThisUtterance and (not sawSilenceAfterVoice):
                                fd = max(float(fadeDuration), float(minFadeOutMs))
                                if float(frameDuration) > 0:
                                    fd = min(fd, float(frameDuration))
                                else:
                                    fd = 0.0
                                fadeDuration = fd
                                sawSilenceAfterVoice = True
                            self._player.queueFrame(None, frameDuration, fadeDuration, userIndex=idxToSet)
                            queuedCount += 1
                            lastStreamWasVoiced = False
                            return

                        # Ensure fade-in on first voiced frame
                        if not sawRealFrameInThisUtterance:
                            fadeDuration = max(float(fadeDuration), float(minFadeInMs))

                        sawRealFrameInThisUtterance = True
                        hadRealSpeech = True

                        # Copy C frame to Python-owned Frame
                        frame = speechPlayer.Frame()
                        ctypes.memmove(ctypes.byref(frame), framePtr, ctypes.sizeof(speechPlayer.Frame))

                        # Only apply Python voice preset if NOT using a C++ voice profile.
                        # When using a profile, the formant transforms are already applied by the frontend.
                        if not getattr(self, "_usingVoiceProfile", False):
                            _applyVoiceToFrame(frame, self._curVoice)

                        if extraParamMultipliers:
                            for paramName, ratio in extraParamMultipliers:
                                setattr(frame, paramName, getattr(frame, paramName) * ratio)

                        frame.preFormantGain *= self._curVolume
                        
                        # Use FrameEx from frontend callback if available (ABI v2+)
                        # Frontend has already mixed per-phoneme values with user defaults
                        frameEx = None
                        if frameExPtr and getattr(self._player, "hasFrameExSupport", lambda: False)():
                            # Copy C FrameEx to Python-owned struct
                            frameEx = speechPlayer.FrameEx()
                            ctypes.memmove(ctypes.byref(frameEx), frameExPtr, ctypes.sizeof(speechPlayer.FrameEx))
                        
                        # Use queueFrameEx if we have FrameEx data, otherwise fall back
                        if frameEx is not None:
                            self._player.queueFrameEx(frame, frameEx, frameDuration, fadeDuration, userIndex=idxToSet)
                        else:
                            self._player.queueFrame(frame, frameDuration, fadeDuration, userIndex=idxToSet)
                        queuedCount += 1
                        lastStreamWasVoiced = True

                    ok = False
                    try:
                        # Use queueIPA_ExWithText for stress correction (ABI v4+).
                        # Falls back to queueIPA_Ex internally if not available.
                        ok = self._frontend.queueIPA_ExWithText(
                            ipaText,
                            originalText=chunk,
                            speed=self._curRate,
                            basePitch=basePitch,
                            inflection=self._curInflection,
                            clauseType=clauseType,
                            userIndex=None,
                            onFrame=_onFrame,
                        )
                    except Exception:
                        log.error("TGSpeechBox: frontend queueIPA_ExWithText failed", exc_info=True)
                        ok = False

                    if not ok:
                        err = self._frontend.getLastError()
                        if err:
                            log.error(f"TGSpeechBox: frontend error: {err}")

                    # If the frontend fails or outputs nothing, keep going (indexes are still queued).
                    if (not ok) or queuedCount <= 0:
                        continue

                    # Streaming: kick the AudioThread as soon as we have
                    # frames so audio starts while we generate the rest.
                    if not hadKickedAudio:
                        if generation != self._speakGen:
                            return
                        self._audio.allFramesQueued = False
                        self._audio.isSpeaking = True
                        if generation != self._speakGen:
                            self._audio.isSpeaking = False
                            return
                        self._audio.kick()
                        hadKickedAudio = True

                    # Optional punctuation pause (micro-silence) after the clause.
                    # Insert only when we actually queued a voiced frame; otherwise we'd
                    # be adding silence after silence.
                    if punctPauseMs and sawRealFrameInThisUtterance:
                        try:
                            dur = float(punctPauseMs)
                            fd = float(min(float(minFadeOutMs), dur))
                            self._player.queueFrame(None, dur, fd)
                            lastStreamWasVoiced = False
                        except Exception:
                            log.debug("TGSpeechBox: failed inserting punctuation pause", exc_info=True)

                    # Signal AudioThread that new frames are available (streaming mode).
                    # This replaces the 1ms polling sleep with an event-driven wakeup.
                    if hadKickedAudio:
                        self._audio._framesReady.set()

            # Emit IndexCommands after this block
            if indexesAfter:
                for idx in indexesAfter:
                    try:
                        if lastStreamWasVoiced:
                            dur = float(minFadeOutMs)
                            self._player.queueFrame(None, dur, dur, userIndex=int(idx))
                            lastStreamWasVoiced = False
                        else:
                            self._player.queueFrame(None, 0.0, 0.0, userIndex=int(idx))
                    except Exception:
                        log.debug("TGSpeechBox: failed to queue index marker %r", idx, exc_info=True)

        if endPause and endPause > 0:
            self._player.queueFrame(None, float(endPause), min(float(endPause), 5.0))

        # Tiny tail fade to smooth utterance end
        if hadRealSpeech:
            self._player.queueFrame(None, 1.0, 1.0)

        # Signal that all frames have been queued so the AudioThread
        # knows it can exit when synthesize() returns empty.
        self._audio.allFramesQueued = True
        self._audio._framesReady.set()  # wake AudioThread if waiting

        # Final generation check — if cancel() was called while we were
        # generating IPA/frames above, don't start the audio thread.
        if generation != self._speakGen:
            return

        if not hadKickedAudio:
            # No streaming kick happened (e.g. very short utterance).
            # Start the AudioThread now.
            self._audio.isSpeaking = True
            if generation != self._speakGen:
                self._audio.isSpeaking = False
                return
            self._audio.kick()

    def cancel(self):
        """Cancel current speech immediately."""
        try:
            # Guard against early calls before __init__ completes
            if not hasattr(self, "_player") or not self._player:
                return
            if not hasattr(self, "_audio") or not self._audio:
                return

            # === PHASE 1: Stop audio output IMMEDIATELY ===
            # isSpeaking must be cleared BEFORE anything else so the
            # AudioThread's inner loop exits at the next iteration and
            # the post-synthesize isSpeaking re-check prevents it from
            # feeding any more audio to the WavePlayer.
            self._audio.allFramesQueued = True  # stop AudioThread polling
            self._audio._framesReady.set()      # wake if waiting on Event
            self._audio.isSpeaking = False
            self._audio.stopPlayback()
            self._audio._applyFadeIn = True

            # === PHASE 2: Invalidate in-flight work ===
            # Bump generation to invalidate any in-flight or pending _speakBg jobs.
            # BgThread checks this counter between chunks and inside _onFrame callbacks.
            self._speakGen += 1

            # Drain pending jobs as an optimisation (they'd bail on generation
            # mismatch anyway, but this avoids the dequeue-and-bail overhead).
            try:
                while True:
                    self._bgQueue.get_nowait()
                    self._bgQueue.task_done()
            except queue.Empty:
                pass

            # === PHASE 3: Purge frame queue and flush resonators ===
            # We do NOT call player.synthesize() here because the
            # AudioThread may still be inside its own synthesize() call
            # (it exits after checking isSpeaking, which we just cleared).
            # Concurrent synthesize() calls on the unsynchronised C DLL
            # cause data races.  Instead, just purge the frame queue.
            # The fade-in envelope (_applyFadeIn) will mask any stale
            # resonator energy when the next utterance starts.
            self._player.queueFrame(None, 3.0, 3.0, purgeQueue=True)

            self._audio.kick()
        except Exception:
            log.debug("TGSpeechBox: cancel failed", exc_info=True)

    def pause(self, switch):
        try:
            if self._audio and self._audio._wavePlayer:
                self._audio._wavePlayer.pause(switch)
        except Exception:
            log.debug("TGSpeechBox: pause failed", exc_info=True)

    def terminate(self):
        try:
            # Cancel any ongoing speech first
            self.cancel()
            
            # Signal the background thread to stop
            if hasattr(self, "_bgStop"):
                self._bgStop.set()
            if hasattr(self, "_bgQueue"):
                # Put None to wake up the queue.get() if it's blocking
                try:
                    self._bgQueue.put_nowait(None)
                except queue.Full:
                    # Queue is full - thread should wake up anyway when processing items
                    pass
                except (AttributeError, TypeError):
                    # Queue not properly initialized
                    pass
            
            # Terminate audio thread FIRST (it uses the player)
            # Do this before waiting on bgThread since audio thread is higher priority
            if hasattr(self, "_audio") and self._audio:
                try:
                    self._audio.terminate()
                except Exception:
                    log.debug("TGSpeechBox: audio terminate failed", exc_info=True)
                self._audio = None
            
            # Now join the background thread with a SHORT timeout
            # It should exit quickly since _bgStop is set
            if hasattr(self, "_bgThread") and self._bgThread is not None:
                try:
                    self._bgThread.join(timeout=0.3)
                except (TypeError, AttributeError):
                    # Very old Python without timeout parameter
                    pass
                except Exception:
                    log.debug("TGSpeechBox: bgThread join failed", exc_info=True)
                self._bgThread = None
            
            # Terminate frontend (unloads nvspFrontend.dll)
            # Do this BEFORE terminating player since frontend may reference player resources
            if getattr(self, "_frontend", None):
                try:
                    self._frontend.terminate()
                except Exception:
                    log.debug("TGSpeechBox: frontend terminate failed", exc_info=True)
                self._frontend = None
            
            # Terminate player last (unloads speechPlayer.dll)
            if hasattr(self, "_player") and self._player:
                try:
                    self._player.terminate()
                except Exception:
                    log.debug("TGSpeechBox: player terminate failed", exc_info=True)
                self._player = None
            
            # Finally terminate espeak
            try:
                _espeak.terminate()
            except Exception:
                log.debug("TGSpeechBox: espeak terminate failed", exc_info=True)
        except Exception:
            log.debug("TGSpeechBox: terminate failed", exc_info=True)

    def loadSettings(self, onlyChanged=False):
        """Override loadSettings to ensure frontend profile is always properly restored.
        
        This fixes the Escape key bug where:
        1. User selects a voice profile (e.g., profile:Beth)
        2. User opens settings, modifies tilt/rate, then hits Escape
        3. NVDA calls loadSettings() to restore saved settings
        4. If anything goes wrong (exception in changeVoice, etc.), the frontend
           profile could be left in an inconsistent state
        
        Our fix: After the parent loadSettings() completes (successfully or not),
        we always re-sync the frontend with the current voice profile state.
        """
        # Capture the voice BEFORE loadSettings runs, in case something corrupts it
        voiceBeforeLoad = getattr(self, "_curVoice", None)
        
        try:
            # Call parent implementation
            super().loadSettings(onlyChanged)
        except Exception:
            log.debug("TGSpeechBox: parent loadSettings failed", exc_info=True)
            # Don't re-raise - we'll try to recover below
        
        # CRITICAL: Always re-sync frontend profile after loadSettings completes.
        # This ensures the frontend has the correct phonetic transformations applied,
        # even if NVDA's loadSettings hit an exception and tried to "recover".
        try:
            curVoice = getattr(self, "_curVoice", None)
            
            # If voice got corrupted (changed unexpectedly), restore it
            if voiceBeforeLoad and curVoice != voiceBeforeLoad:
                # Check if the "new" voice is just a fallback to Adam
                # when we actually had a valid profile before
                if voiceBeforeLoad.startswith(VOICE_PROFILE_PREFIX) and curVoice == "Adam":
                    log.debug(f"TGSpeechBox: loadSettings corrupted voice from {voiceBeforeLoad} to {curVoice}, restoring")
                    self._curVoice = voiceBeforeLoad
                    curVoice = voiceBeforeLoad
            
            # Re-apply frontend profile to ensure phonetic transformations are active
            if curVoice and curVoice.startswith(VOICE_PROFILE_PREFIX):
                profileName = curVoice[len(VOICE_PROFILE_PREFIX):]
                self._activeProfileName = profileName
                # Re-apply voicing tone as well
                self._applyVoicingTone(profileName)
                log.debug(f"TGSpeechBox: loadSettings re-synced frontend profile '{profileName}'")
            else:
                # Python preset or no voice - ensure frontend profile is cleared
                if hasattr(self, "_frontend") and self._frontend:
                    self._frontend.setVoiceProfile("")
                    self._usingVoiceProfile = False
                    self._activeProfileName = ""
                    self._applyVoicingTone("")
        except Exception:
            log.debug("TGSpeechBox: loadSettings frontend re-sync failed", exc_info=True)

    def _get_rate(self):
        try:
            return int(math.log(getattr(self, "_curRate", 1.0) / 0.25, 2) * 25.0)
        except Exception:
            return 50

    def _set_rate(self, val):
        try:
            self._curRate = 0.25 * (2 ** (float(val) / 25.0))
        except Exception:
            pass

    def _get_pitch(self):
        return int(getattr(self, "_curPitch", 50))

    def _set_pitch(self, val):
        try:
            self._curPitch = int(val)
        except Exception:
            pass

    def _get_volume(self):
        return int(getattr(self, "_curVolume", 1.0) * 75)

    def _set_volume(self, val):
        try:
            self._curVolume = float(val) / 75.0
        except Exception:
            pass

    def _get_inflection(self):
        return int(getattr(self, "_curInflection", 0.5) / 0.01)

    def _set_inflection(self, val):
        try:
            self._curInflection = float(val) * 0.01
        except Exception:
            pass

    def _get_voiceTilt(self):
        return int(getattr(self, "_curVoiceTilt", 50))

    def _set_voiceTilt(self, val):
        try:
            newVal = int(val)
            # Only re-apply if value actually changed
            if newVal == getattr(self, "_curVoiceTilt", 50):
                return
            
            self._curVoiceTilt = newVal
            
            # Derive profile name from _curVoice (the source of truth)
            curVoice = getattr(self, "_curVoice", "Adam") or "Adam"
            if curVoice.startswith(VOICE_PROFILE_PREFIX):
                profileName = curVoice[len(VOICE_PROFILE_PREFIX):]
            else:
                profileName = ""
            
            # Re-apply voicing tone (and frontend profile) with the new tilt offset
            self._applyVoicingTone(profileName)
        except Exception:
            # Never crash during settings application
            pass

    # --- Noise Glottal Modulation slider (0-100, maps to 0.0-1.0) ---
    def _get_noiseGlottalMod(self):
        return int(getattr(self, "_curNoiseGlottalMod", 0))

    def _set_noiseGlottalMod(self, val):
        try:
            newVal = int(val)
            if newVal == getattr(self, "_curNoiseGlottalMod", 0):
                return
            self._curNoiseGlottalMod = newVal
            curVoice = getattr(self, "_curVoice", "Adam") or "Adam"
            if curVoice.startswith(VOICE_PROFILE_PREFIX):
                profileName = curVoice[len(VOICE_PROFILE_PREFIX):]
            else:
                profileName = ""
            self._applyVoicingTone(profileName)
        except Exception:
            pass

    # --- Pitch-sync F1 slider (0-100, maps to 0-120 Hz delta) ---
    def _get_pitchSyncF1(self):
        return int(getattr(self, "_curPitchSyncF1", 50))

    def _set_pitchSyncF1(self, val):
        try:
            newVal = int(val)
            if newVal == getattr(self, "_curPitchSyncF1", 50):
                return
            self._curPitchSyncF1 = newVal
            curVoice = getattr(self, "_curVoice", "Adam") or "Adam"
            if curVoice.startswith(VOICE_PROFILE_PREFIX):
                profileName = curVoice[len(VOICE_PROFILE_PREFIX):]
            else:
                profileName = ""
            self._applyVoicingTone(profileName)
        except Exception:
            pass

    # --- Pitch-sync B1 slider (0-100, maps to 0-100 Hz delta) ---
    def _get_pitchSyncB1(self):
        return int(getattr(self, "_curPitchSyncB1", 50))

    def _set_pitchSyncB1(self, val):
        try:
            newVal = int(val)
            if newVal == getattr(self, "_curPitchSyncB1", 50):
                return
            self._curPitchSyncB1 = newVal
            curVoice = getattr(self, "_curVoice", "Adam") or "Adam"
            if curVoice.startswith(VOICE_PROFILE_PREFIX):
                profileName = curVoice[len(VOICE_PROFILE_PREFIX):]
            else:
                profileName = ""
            self._applyVoicingTone(profileName)
        except Exception:
            pass

    # --- Speed Quotient slider (0-100, maps to 0.5-4.0) ---
    # Controls glottal pulse asymmetry for male/female voice distinction.
    # 0 = 0.5 (very breathy/soft), 50 = 2.0 (neutral), 100 = 4.0 (pressed/tense)
    def _get_speedQuotient(self):
        return int(getattr(self, "_curSpeedQuotient", 50))

    def _set_speedQuotient(self, val):
        try:
            newVal = int(val)
            if newVal == getattr(self, "_curSpeedQuotient", 50):
                return
            self._curSpeedQuotient = newVal
            curVoice = getattr(self, "_curVoice", "Adam") or "Adam"
            if curVoice.startswith(VOICE_PROFILE_PREFIX):
                profileName = curVoice[len(VOICE_PROFILE_PREFIX):]
            else:
                profileName = ""
            self._applyVoicingTone(profileName)
        except Exception:
            pass

    # --- Aspiration Tilt slider (0-100 maps to -12 to +12 dB/oct) ---
    # Controls brightness/darkness of breath noise.
    # 0 = -12 dB/oct (dark/soft), 50 = 0 (neutral), 100 = +12 dB/oct (bright/harsh)
    def _get_aspirationTilt(self):
        return int(getattr(self, "_curAspirationTilt", 50))

    def _set_aspirationTilt(self, val):
        try:
            newVal = int(val)
            if newVal == getattr(self, "_curAspirationTilt", 50):
                return
            self._curAspirationTilt = newVal
            curVoice = getattr(self, "_curVoice", "Adam") or "Adam"
            if curVoice.startswith(VOICE_PROFILE_PREFIX):
                profileName = curVoice[len(VOICE_PROFILE_PREFIX):]
            else:
                profileName = ""
            self._applyVoicingTone(profileName)
        except Exception:
            pass


    def _get_cascadeBwScale(self):
        return int(getattr(self, "_curCascadeBwScale", 50))

    def _set_cascadeBwScale(self, val):
        try:
            newVal = int(val)
            if newVal == getattr(self, "_curCascadeBwScale", 50):
                return
            self._curCascadeBwScale = newVal
            curVoice = getattr(self, "_curVoice", "Adam") or "Adam"
            if curVoice.startswith(VOICE_PROFILE_PREFIX):
                profileName = curVoice[len(VOICE_PROFILE_PREFIX):]
            else:
                profileName = ""
            self._applyVoicingTone(profileName)
        except Exception:
            pass

    def _get_voiceTremor(self):
        return int(getattr(self, "_curVoiceTremor", 0))

    def _set_voiceTremor(self, val):
        try:
            newVal = int(val)
            if newVal == getattr(self, "_curVoiceTremor", 0):
                return
            self._curVoiceTremor = newVal
            curVoice = getattr(self, "_curVoice", "Adam") or "Adam"
            if curVoice.startswith(VOICE_PROFILE_PREFIX):
                profileName = curVoice[len(VOICE_PROFILE_PREFIX):]
            else:
                profileName = ""
            self._applyVoicingTone(profileName)
        except Exception:
            pass
    # =========================================================================
    # FrameEx voice quality sliders (DSP v5+)
    # These are applied per-frame via queueFrameEx, not via voicingTone.
    # Slider range 0-100 maps to 0.0-1.0 for the DSP.
    # =========================================================================

    def _get_frameExCreakiness(self):
        return int(getattr(self, "_curFrameExCreakiness", 0))

    def _set_frameExCreakiness(self, val):
        try:
            self._curFrameExCreakiness = int(val)
            self._pushFrameExDefaultsToFrontend()
        except Exception:
            pass

    def _get_frameExBreathiness(self):
        return int(getattr(self, "_curFrameExBreathiness", 0))

    def _set_frameExBreathiness(self, val):
        try:
            self._curFrameExBreathiness = int(val)
            self._pushFrameExDefaultsToFrontend()
        except Exception:
            pass

    def _get_frameExJitter(self):
        return int(getattr(self, "_curFrameExJitter", 0))

    def _set_frameExJitter(self, val):
        try:
            self._curFrameExJitter = int(val)
            self._pushFrameExDefaultsToFrontend()
        except Exception:
            pass

    def _get_frameExShimmer(self):
        return int(getattr(self, "_curFrameExShimmer", 0))

    def _set_frameExShimmer(self, val):
        try:
            self._curFrameExShimmer = int(val)
            self._pushFrameExDefaultsToFrontend()
        except Exception:
            pass

    def _get_frameExSharpness(self):
        return int(getattr(self, "_curFrameExSharpness", 50))

    def _set_frameExSharpness(self, val):
        try:
            self._curFrameExSharpness = int(val)
            self._pushFrameExDefaultsToFrontend()
        except Exception:
            pass

    def _get_legacyPitchInflectionScale(self):
        try:
            raw = self._getLangPackStr("legacyPitchInflectionScale", default="")
            if raw:
                fval = float(raw)
                # Map 0.0–2.0 → 0–100
                return int(max(0, min(100, fval / 2.0 * 100.0)))
            return 29  # default 0.58
        except Exception:
            return 29

    def _set_legacyPitchInflectionScale(self, val):
        try:
            sliderVal = int(val)
            if sliderVal == getattr(self, "_curLegacyPitchInflectionScale", 29):
                return
            self._curLegacyPitchInflectionScale = sliderVal
            # Map 0–100 → 0.0–2.0
            yamlVal = round(sliderVal / 100.0 * 2.0, 3)
            self._setLangPackSetting("legacyPitchInflectionScale", yamlVal)
        except Exception:
            pass

    def _pushFrameExDefaultsToFrontend(self) -> None:
        """Push current FrameEx slider values to the frontend (ABI v2+).
        
        The frontend mixes these user-level defaults with per-phoneme values
        when emitting frames via queueIPA_Ex(). This must be called:
        - After frontend initialization
        - Whenever a FrameEx slider changes
        - When switching voices (to restore per-voice settings)
        """
        if not hasattr(self, "_frontend") or not self._frontend:
            return
        if not self._frontend.hasFrameExSupport():
            return
        
        try:
            creakVal = getattr(self, "_curFrameExCreakiness", 0)
            breathVal = getattr(self, "_curFrameExBreathiness", 0)
            jitterVal = getattr(self, "_curFrameExJitter", 0)
            shimmerVal = getattr(self, "_curFrameExShimmer", 0)
            sharpnessVal = getattr(self, "_curFrameExSharpness", 50)
            
            # Convert slider values (0-100) to FrameEx ranges
            # Sharpness: 0-100 maps to 0.5-2.0 (50 = 1.0x neutral)
            sharpnessMul = 0.5 + (sharpnessVal / 100.0) * 1.5
            
            self._frontend.setFrameExDefaults(
                creakiness=creakVal / 100.0,
                breathiness=breathVal / 100.0,
                jitter=jitterVal / 100.0,
                shimmer=shimmerVal / 100.0,
                sharpness=sharpnessMul,
            )
        except Exception:
            log.debug("TGSpeechBox: _pushFrameExDefaultsToFrontend failed", exc_info=True)

    def _applyVoicingTone(self, profileName: str) -> None:
        """Apply DSP-level voicing tone parameters safely.
        
        Gets base voicing tone from frontend (parses voicingTone: from YAML),
        then applies slider offsets on top.
        
        ALSO ensures the frontend voice profile (formant transforms) is applied.
        
        CRITICAL: This entire function is wrapped in try/except to prevent crashes
        from killing NVDA's settings application loop.
        """
        # Guard: need player
        if not hasattr(self, "_player") or not self._player:
            return
        
        # CRITICAL: Catch ALL exceptions here.
        try:
            # ALWAYS ensure the frontend has the correct voice profile set
            if hasattr(self, "_frontend") and self._frontend:
                self._frontend.setVoiceProfile(profileName or "")
                self._pushFrameExDefaultsToFrontend()
            
            playerHasSupport = getattr(self._player, "hasVoicingToneSupport", lambda: False)()
            if not playerHasSupport:
                return
            
            # Build the tone struct with safe defaults
            tone = speechPlayer.VoicingTone.defaults()
            
            # Get base tone from frontend (ABI v2+) - parses voicingTone: from YAML
            if profileName and hasattr(self, "_frontend") and self._frontend:
                if self._frontend.hasExplicitVoicingTone():
                    frontendTone = self._frontend.getVoicingTone()
                    if frontendTone:
                        tone.voicingPeakPos = frontendTone.voicingPeakPos
                        tone.voicedPreEmphA = frontendTone.voicedPreEmphA
                        tone.voicedPreEmphMix = frontendTone.voicedPreEmphMix
                        tone.highShelfGainDb = frontendTone.highShelfGainDb
                        tone.highShelfFcHz = frontendTone.highShelfFcHz
                        tone.highShelfQ = frontendTone.highShelfQ
                        tone.voicedTiltDbPerOct = frontendTone.voicedTiltDbPerOct
                        tone.noiseGlottalModDepth = frontendTone.noiseGlottalModDepth
                        tone.pitchSyncF1DeltaHz = frontendTone.pitchSyncF1DeltaHz
                        tone.pitchSyncB1DeltaHz = frontendTone.pitchSyncB1DeltaHz
                        tone.speedQuotient = frontendTone.speedQuotient
                        tone.aspirationTiltDbPerOct = frontendTone.aspirationTiltDbPerOct
                        tone.cascadeBwScale = frontendTone.cascadeBwScale
            
            # Helper for slider values
            def safe_float(val, default=0.0):
                try:
                    return float(val)
                except (ValueError, TypeError):
                    return default
            
            # Apply voice tilt OFFSET from the slider
            tiltSlider = safe_float(getattr(self, "_curVoiceTilt", 50), 50.0)
            tiltOffset = (tiltSlider - 50.0) * (24.0 / 50.0)
            tone.voicedTiltDbPerOct += tiltOffset
            tone.voicedTiltDbPerOct = max(-24.0, min(24.0, tone.voicedTiltDbPerOct))
            
            # Apply noise glottal modulation from slider (0-100 maps to 0.0-1.0)
            noiseModSlider = safe_float(getattr(self, "_curNoiseGlottalMod", 0), 0.0)
            tone.noiseGlottalModDepth = noiseModSlider / 100.0
            
            # Apply pitch-sync F1 from slider (0-100 maps to -60 to +60 Hz, centered at 50 = 0)
            f1Slider = safe_float(getattr(self, "_curPitchSyncF1", 50), 50.0)
            tone.pitchSyncF1DeltaHz = (f1Slider - 50.0) * 1.2
            
            # Apply pitch-sync B1 from slider (0-100 maps to -50 to +50 Hz, centered at 50 = 0)
            b1Slider = safe_float(getattr(self, "_curPitchSyncB1", 50), 50.0)
            tone.pitchSyncB1DeltaHz = (b1Slider - 50.0) * 1.0
            
            # Apply speed quotient from slider (0-100 maps to 0.5-4.0, centered at 50 = 2.0)
            sqSlider = safe_float(getattr(self, "_curSpeedQuotient", 50), 50.0)
            if sqSlider <= 50.0:
                tone.speedQuotient = 0.5 + (sqSlider / 50.0) * 1.5
            else:
                tone.speedQuotient = 2.0 + ((sqSlider - 50.0) / 50.0) * 2.0
            
            # Apply aspiration tilt from slider (0-100 maps to -12 to +12 dB/oct, centered at 50 = 0)
            aspTiltSlider = safe_float(getattr(self, "_curAspirationTilt", 50), 50.0)
            tone.aspirationTiltDbPerOct = (aspTiltSlider - 50.0) * 0.24
            
            # Apply cascade bandwidth scale from slider (0-100 maps to 0.4-1.4, centered at 50 = 1.0)
            # Below 50: sharper formants (Eloquence-like clarity)
            # Above 50: wider formants (softer, more blended)
            bwSlider = safe_float(getattr(self, "_curCascadeBwScale", 50), 50.0)
            if bwSlider <= 50.0:
                # 0 -> 2.0 (wide/muffled), 50 -> 0.9 (neutral)
                tone.cascadeBwScale = 2.0 - (bwSlider / 50.0) * 1.1
            else:
                # 50 -> 0.9 (neutral), 100 -> 0.3 (sharp/ringy)
                tone.cascadeBwScale = 0.9 - ((bwSlider - 50.0) / 50.0) * 0.6
            # Safety clamp
                tone.cascadeBwScale = max(0.2, min(2.0, tone.cascadeBwScale))

            # Apply voice tremor from slider (0-100 maps to 0.0-0.4 depth)
            tremorSlider = safe_float(getattr(self, "_curVoiceTremor", 0), 0.0)
            tone.tremorDepth = (tremorSlider / 100.0) * 0.4
            tone.tremorDepth = max(0.0, min(0.5, tone.tremorDepth))
            
            # Apply to player
            self._player.setVoicingTone(tone)
            self._lastAppliedVoicingTone = tone
            
        except Exception as e:
            log.error(f"TGSpeechBox: _applyVoicingTone failed: {e}", exc_info=True)

    def _reapplyVoiceProfile(self):
        """Re-apply the current voice setting to ensure the frontend profile is in sync.
        
        This is called after __init__ completes to handle the case where NVDA restores
        the saved voice setting but the frontend profile wasn't properly applied.
        """
        try:
            voice = getattr(self, "_curVoice", "Adam")
            if voice and voice.startswith(VOICE_PROFILE_PREFIX):
                profileName = voice[len(VOICE_PROFILE_PREFIX):]
                if hasattr(self, "_frontend") and self._frontend:
                    self._frontend.setVoiceProfile(profileName)
                    self._usingVoiceProfile = True
                    self._activeProfileName = profileName
                    # Also apply voicing tone for this profile
                    self._applyVoicingTone(profileName)
                    log.debug(f"TGSpeechBox: reapplied voice profile '{profileName}'")
            else:
                # Python preset - ensure voicing tone is at defaults
                self._applyVoicingTone("")
        except Exception:
            log.debug("TGSpeechBox: _reapplyVoiceProfile failed", exc_info=True)

    def _get_voice(self):
        return getattr(self, "_curVoice", "Adam") or "Adam"

    def _set_voice(self, voice):
        try:
            # CRITICAL: Do not force "Adam" if validation fails.
            # If NVDA asks for a profile we haven't loaded yet, accept it.
            # Forcing "Adam" here causes the settings loss on Escape.
            
            # Check if voice is actually changing
            oldVoice = getattr(self, "_curVoice", None)
            voiceChanged = (oldVoice is not None and oldVoice != voice)
            
            # Initialize per-voice storage dicts if needed
            if not hasattr(self, "_perVoiceTilt"):
                self._perVoiceTilt = {}
            if not hasattr(self, "_perVoiceNoiseGlottalMod"):
                self._perVoiceNoiseGlottalMod = {}
            if not hasattr(self, "_perVoicePitchSyncF1"):
                self._perVoicePitchSyncF1 = {}
            if not hasattr(self, "_perVoicePitchSyncB1"):
                self._perVoicePitchSyncB1 = {}
            if not hasattr(self, "_perVoiceSpeedQuotient"):
                self._perVoiceSpeedQuotient = {}
            if not hasattr(self, "_perVoiceAspirationTilt"):
                self._perVoiceAspirationTilt = {}
            if not hasattr(self, "_perVoiceCascadeBwScale"):
                self._perVoiceCascadeBwScale = {}
            if not hasattr(self, "_perVoiceFrameExCreakiness"):
                self._perVoiceFrameExCreakiness = {}
            if not hasattr(self, "_perVoiceFrameExBreathiness"):
                self._perVoiceFrameExBreathiness = {}
            if not hasattr(self, "_perVoiceFrameExJitter"):
                self._perVoiceFrameExJitter = {}
            if not hasattr(self, "_perVoiceFrameExShimmer"):
                self._perVoiceFrameExShimmer = {}
            if not hasattr(self, "_perVoiceFrameExSharpness"):
                self._perVoiceFrameExSharpness = {}
            
            # Save current slider values for the OLD voice before switching
            if voiceChanged and oldVoice:
                self._perVoiceTilt[oldVoice] = getattr(self, "_curVoiceTilt", 50)
                self._perVoiceNoiseGlottalMod[oldVoice] = getattr(self, "_curNoiseGlottalMod", 0)
                self._perVoicePitchSyncF1[oldVoice] = getattr(self, "_curPitchSyncF1", 50)
                self._perVoicePitchSyncB1[oldVoice] = getattr(self, "_curPitchSyncB1", 50)
                self._perVoiceSpeedQuotient[oldVoice] = getattr(self, "_curSpeedQuotient", 50)
                self._perVoiceAspirationTilt[oldVoice] = getattr(self, "_curAspirationTilt", 50)
                self._perVoiceCascadeBwScale[oldVoice] = getattr(self, "_curCascadeBwScale", 50)
                self._perVoiceVoiceTremor[oldVoice] = getattr(self, "_curVoiceTremor", 0)
                self._perVoiceFrameExCreakiness[oldVoice] = getattr(self, "_curFrameExCreakiness", 0)
                self._perVoiceFrameExBreathiness[oldVoice] = getattr(self, "_curFrameExBreathiness", 0)
                self._perVoiceFrameExJitter[oldVoice] = getattr(self, "_curFrameExJitter", 0)
                self._perVoiceFrameExShimmer[oldVoice] = getattr(self, "_curFrameExShimmer", 0)
                self._perVoiceFrameExSharpness[oldVoice] = getattr(self, "_curFrameExSharpness", 50)
            
            self._curVoice = voice
            
            # Restore slider values for the NEW voice (or default if never set)
            if voiceChanged and voice:
                self._curVoiceTilt = self._perVoiceTilt.get(voice, 50)
                self._curNoiseGlottalMod = self._perVoiceNoiseGlottalMod.get(voice, 0)
                self._curPitchSyncF1 = self._perVoicePitchSyncF1.get(voice, 50)
                self._curPitchSyncB1 = self._perVoicePitchSyncB1.get(voice, 50)
                self._curSpeedQuotient = self._perVoiceSpeedQuotient.get(voice, 50)
                self._curAspirationTilt = self._perVoiceAspirationTilt.get(voice, 50)
                self._curCascadeBwScale = self._perVoiceCascadeBwScale.get(voice, 50)
                self._curVoiceTremor = self._perVoiceVoiceTremor.get(voice, 0)
                self._curFrameExCreakiness = self._perVoiceFrameExCreakiness.get(voice, 0)
                self._curFrameExBreathiness = self._perVoiceFrameExBreathiness.get(voice, 0)
                self._curFrameExJitter = self._perVoiceFrameExJitter.get(voice, 0)
                self._curFrameExShimmer = self._perVoiceFrameExShimmer.get(voice, 0)
                self._curFrameExSharpness = self._perVoiceFrameExSharpness.get(voice, 50)
                # Push restored FrameEx settings to frontend
                self._pushFrameExDefaultsToFrontend()
            
            # Handle voice profile vs Python preset
            if voice and voice.startswith(VOICE_PROFILE_PREFIX):
                profileName = voice[len(VOICE_PROFILE_PREFIX):]
                self._usingVoiceProfile = True
                self._activeProfileName = profileName
                
                if hasattr(self, "_frontend") and self._frontend:
                    self._frontend.setVoiceProfile(profileName)
                    self._applyVoicingTone(profileName)
            else:
                self._usingVoiceProfile = False
                self._activeProfileName = ""
                
                if hasattr(self, "_frontend") and self._frontend:
                    self._frontend.setVoiceProfile("")
                    self._applyVoicingTone("")
            
            if self.exposeExtraParams:
                for paramName in getattr(self, "_extraParamNames", []):
                    setattr(self, f"speechPlayer_{paramName}", 50)
        except Exception:
            # Never crash during settings application
            log.debug("TGSpeechBox: _set_voice failed", exc_info=True)

    def _getAvailableVoices(self):
        try:
            d = OrderedDict()
            # Python presets first
            for name in sorted(voices):
                d[name] = VoiceInfo(name, name)
            # Voice profiles from phonemes.yaml (if any)
            for profileName in sorted(getattr(self, "_voiceProfiles", []) or []):
                voiceId = f"{VOICE_PROFILE_PREFIX}{profileName}"
                # Display name includes profile name, description notes it's a profile
                d[voiceId] = VoiceInfo(voiceId, f"{profileName} (profile)")
            
            # CRITICAL: Ensure the current voice is always in availableVoices.
            # This prevents KeyError in speechDictHandler.loadVoiceDict() when
            # NVDA calls loadSettings() on Escape (onDiscard), which does:
            #   synth.availableVoices[synth.voice].displayName
            # If current voice isn't in the dict, that line throws KeyError,
            # which causes loadSettings() to fall back to "Adam".
            curVoice = getattr(self, "_curVoice", None)
            if curVoice and curVoice not in d:
                if curVoice.startswith(VOICE_PROFILE_PREFIX):
                    profileName = curVoice[len(VOICE_PROFILE_PREFIX):]
                    d[curVoice] = VoiceInfo(curVoice, f"{profileName} (profile)")
                else:
                    # Unknown Python preset - add it anyway
                    d[curVoice] = VoiceInfo(curVoice, curVoice)
            
            return d
        except Exception:
            # Return at least the default voice
            return OrderedDict([("Adam", VoiceInfo("Adam", "Adam"))])
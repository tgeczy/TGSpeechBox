# -*- coding: utf-8 -*-
"""NV Speech Player - NVDA synth driver (modernized)

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
    re_textPause, normalizeTextForEspeak, looksLikeSentenceEnd
)
from .profile_utils import (
    discoverVoiceProfiles, discoverVoicingTones, 
    buildVoiceOps, applyVoiceToFrame
)
from .audio import BgThread, AudioThread

# Pre-calculate per-voice operations for fast application
_frameFieldNames = {x[0] for x in speechPlayer.Frame._fields_}
_voiceOps = buildVoiceOps(voices, _frameFieldNames)
del _frameFieldNames

# Wrapper function for backward compatibility (uses module-level _voiceOps)
def _applyVoiceToFrame(frame: speechPlayer.Frame, voiceName: str) -> None:
    applyVoiceToFrame(frame, voiceName, _voiceOps)


class SynthDriver(SynthDriver):
    name = "nvSpeechPlayer"
    description = "NV Speech Player"

    _supportedSettings = [
        SynthDriver.VoiceSetting(),
        SynthDriver.RateSetting(),
        SynthDriver.PitchSetting(),
        SynthDriver.InflectionSetting(),
        SynthDriver.VolumeSetting(),
        NumericDriverSetting("voiceTilt", "Voice tilt (brightness)", defaultVal=50),
        DriverSetting("pauseMode", "Pause mode"),
        DriverSetting("sampleRate", "Sample rate"),
        DriverSetting("language", "Language"),

        # --- Language-pack quick settings (YAML: packs/lang/*.yaml -> settings:) ---
        DriverSetting("stopClosureMode", "Stop closure mode"),
        # Newer setting: how diphthongs are handled in spelled-out text.
        # (Supported values: none, monophthong)
        DriverSetting("spellingDiphthongMode", "Spelling diphthong mode"),
    ]

    if BooleanDriverSetting is not None:
        _supportedSettings.extend(
            [
                BooleanDriverSetting("stopClosureClusterGapsEnabled", "Insert brief closure pauses in consonant clusters"),  # type: ignore
                BooleanDriverSetting("stopClosureAfterNasalsEnabled", "Insert stop closure after nasal sounds"),  # type: ignore
                BooleanDriverSetting("autoTieDiphthongs", "Treat diphthongs as a single connected sound"),  # type: ignore
                BooleanDriverSetting("autoDiphthongOffglideToSemivowel", "Convert diphthong offglides into smoother semivowels"),  # type: ignore
                BooleanDriverSetting("segmentBoundarySkipVowelToVowel", "Skip join gap between speech chunks when a vowel follows a vowel"),  # type: ignore
                BooleanDriverSetting("segmentBoundarySkipVowelToLiquid", "Skip join gap between speech chunks when a liquid follows a vowel"),  # type: ignore
                BooleanDriverSetting("postStopAspirationEnabled", "Add aspiration after unvoiced stop consonants"),  # type: ignore
                # --- Coarticulation settings ---
                BooleanDriverSetting("coarticulationEnabled", "Enable formant coarticulation"),  # type: ignore
                BooleanDriverSetting("coarticulationFadeIntoConsonants", "Fade coarticulation into consonants"),  # type: ignore
                BooleanDriverSetting("coarticulationVelarPinchEnabled", "Enable velar pinch effect for coarticulation"),  # type: ignore
                BooleanDriverSetting("coarticulationGraduated", "Use graduated coarticulation blending"),  # type: ignore
            ]
        )
        # Coarticulation adjacency combo-box (grouped with coarticulation settings)
        _supportedSettings.append(DriverSetting("coarticulationAdjacencyMaxConsonants", "Coarticulation adjacency range"))

    if BooleanDriverSetting is not None:
        _supportedSettings.extend(
            [
                # --- Phrase-final lengthening settings ---
                BooleanDriverSetting("phraseFinalLengtheningEnabled", "Enable phrase-final lengthening"),  # type: ignore
                BooleanDriverSetting("phraseFinalLengtheningNucleusOnlyMode", "Apply phrase-final lengthening to nucleus only"),  # type: ignore
                # --- Microprosody settings ---
                BooleanDriverSetting("microprosodyEnabled", "Enable microprosody adjustments"),  # type: ignore
                BooleanDriverSetting("microprosodyVoicelessF0RaiseEnabled", "Raise F0 for voiceless consonants"),  # type: ignore
                BooleanDriverSetting("microprosodyVoicedF0LowerEnabled", "Lower F0 for voiced consonants"),  # type: ignore
                # --- Rate reduction settings ---
                BooleanDriverSetting("rateReductionEnabled", "Enable rate-dependent reduction"),  # type: ignore
                # --- Nasalization settings ---
                BooleanDriverSetting("nasalizationAnticipatoryEnabled", "Enable anticipatory nasalization"),  # type: ignore
                # --- Liquid dynamics settings ---
                BooleanDriverSetting("liquidDynamicsEnabled", "Enable liquid dynamics (lateral onglide transitions)"),  # type: ignore
                # --- Length contrast settings ---
                BooleanDriverSetting("lengthContrastEnabled", "Enable phonemic length contrast"),  # type: ignore
                # --- Positional allophones settings ---
                BooleanDriverSetting("positionalAllophonesEnabled", "Enable positional allophone variation"),  # type: ignore
                BooleanDriverSetting("positionalAllophonesGlottalReinforcementEnabled", "Enable glottal reinforcement for stops"),  # type: ignore
                # --- Boundary smoothing settings ---
                BooleanDriverSetting("boundarySmoothingEnabled", "Enable boundary smoothing between sounds"),  # type: ignore
                # --- Trajectory limit settings ---
                BooleanDriverSetting("trajectoryLimitEnabled", "Enable formant trajectory rate limiting"),  # type: ignore
                BooleanDriverSetting("trajectoryLimitApplyAcrossWordBoundary", "Apply trajectory limit across word boundaries"),  # type: ignore
                BooleanDriverSetting("legacyPitchMode", "Use classic pitch and intonation style"),  # type: ignore
                BooleanDriverSetting("tonal", "Enable tonal language behavior"),  # type: ignore
                BooleanDriverSetting("toneDigitsEnabled", "Interpret tone numbers in text"),  # type: ignore
            ]
        )

    _supportedSettings.append(DriverSetting("toneContoursMode", "Tone contour mode"))

    if BooleanDriverSetting is not None:
        _supportedSettings.extend(
            [
                BooleanDriverSetting("stripAllophoneDigits", "Strip allophone digits"),  # type: ignore
                BooleanDriverSetting("stripHyphen", "Strip hyphens from IPA output"),  # type: ignore
            ]
        )

    supportedSettings = tuple(_supportedSettings)

    supportedCommands = {c for c in (IndexCommand, PitchCommand) if c}
    supportedNotifications = {synthIndexReached, synthDoneSpeaking}

    exposeExtraParams = False
    _ESPEAK_PHONEME_MODE = 0x36100 + 0x82

    def __init__(self):
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
        self._perVoiceTilt = {}  # Per-voice tilt storage: {voiceName: tiltValue}
        self._usingVoiceProfile = False
        self._activeProfileName = ""
        self._pauseMode = "short"
        self._language = "en-us"
        self._langPackSettingsCache: dict[str, object] = {}
        self._sampleRate = 16000
        
        # Initialize containers immediately to avoid NoneType errors
        self._voiceProfiles = []
        self._voicingTones = {}
        
        # Suppress YAML writes during NVDA config replay (YAML is source of truth)
        self._suppressLangPackWrites = True

        # 2. Check architecture compatibility
        if ctypes.sizeof(ctypes.c_void_p) not in (4, 8):
            raise RuntimeError('nvSpeechPlayer: unsupported Python architecture')

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
            raise RuntimeError(f"nvSpeechPlayer: missing packs directory at {packsDir}")

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
            raise RuntimeError(f"nvSpeechPlayer: missing required packs: {', '.join(missingRel)}")

        # 5. Initialize core components (Player and Frontend)
        #    These MUST be ready before super().__init__() calls our setters
        self._player = speechPlayer.SpeechPlayer(self._sampleRate)

        dllDir = findDllDir(here)
        if not dllDir:
            raise RuntimeError('nvSpeechPlayer: could not find DLLs for this architecture')
        
        feDllPath = os.path.join(dllDir, 'nvspFrontend.dll')
        self._frontend = NvspFrontend(feDllPath, packsDir)

        if not self._frontend.setLanguage("default"):
            log.warning(f"nvSpeechPlayer: failed to load default pack: {self._frontend.getLastError()}")

        # 6. Discover voice profiles and voicing tones
        #    This MUST be done before super().__init__() so availableVoices is populated
        #    Wrapped in try/except so bad YAML doesn't crash init
        try:
            self._voiceProfiles = discoverVoiceProfiles(packsDir) or []
            if self._voiceProfiles:
                log.info(f"nvSpeechPlayer: discovered voice profiles: {self._voiceProfiles}")
        except Exception as e:
            log.error(f"nvSpeechPlayer: error discovering voice profiles: {e}")
            self._voiceProfiles = []
        
        try:
            self._voicingTones = discoverVoicingTones(packsDir) or {}
            if self._voicingTones:
                log.info(f"nvSpeechPlayer: discovered voicing tones for profiles: {list(self._voicingTones.keys())}")
        except Exception as e:
            log.error(f"nvSpeechPlayer: error discovering voicing tones: {e}")
            self._voicingTones = {}

        # Check for pack warnings
        if self._frontend.hasVoiceProfileSupport():
            warnings = self._frontend.getPackWarnings()
            if warnings:
                log.warning(f"nvSpeechPlayer: pack warnings: {warnings}")

        # 7. Initialize audio system
        self._audio = AudioThread(self, self._player, self._sampleRate)
        self._bgQueue: "queue.Queue" = queue.Queue()
        self._bgStop = threading.Event()
        self._bgThread = BgThread(self._bgQueue, self._bgStop)
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
                    log.debug("nvSpeechPlayer: failed to set default espeak voice", exc_info=True)
            
            # Verify espeak is actually usable by checking if the DLL and function exist
            espeakDLL = getattr(_espeak, "espeakDLL", None)
            if espeakDLL and hasattr(espeakDLL, "espeak_TextToPhonemes"):
                # Fix espeak_TextToPhonemes prototype for 64-bit Python
                _ttp = espeakDLL.espeak_TextToPhonemes
                _ttp.argtypes = (ctypes.POINTER(ctypes.c_void_p), ctypes.c_int, ctypes.c_int)
                _ttp.restype = ctypes.c_void_p
                self._espeakReady = True
                log.debug("nvSpeechPlayer: espeak initialized successfully")
            else:
                log.warning("nvSpeechPlayer: espeak DLL or espeak_TextToPhonemes not available")
        except Exception:
            log.warning("nvSpeechPlayer: failed to initialize espeak", exc_info=True)

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
            log.debug("nvSpeechPlayer: _set_sampleRate failed", exc_info=True)

    def _reinitializeAudio(self):
        """Reinitialize audio subsystem after sample rate change."""
        try:
            self.cancel()
        except Exception:
            log.debug("nvSpeechPlayer: cancel failed during audio reinit", exc_info=True)
        
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
            log.error("nvSpeechPlayer: failed to reinitialize audio", exc_info=True)

    def _get_language(self):
        return getattr(self, "_language", "en-us")

    def _set_language(self, langCode):
        code = str(langCode or "").strip().lower()
        if code not in languages:
            code = "en-us"

        try:
            self.cancel()
        except Exception:
            log.debug("nvSpeechPlayer: cancel failed while changing language", exc_info=True)

        applied = False
        for tryCode in (code, code.replace("_", "-"), code.replace("-", "_")):
            try:
                ok = _espeak.setVoiceByLanguage(tryCode)
                if ok is None or ok:
                    applied = True
                    code = tryCode.lower()
                    break
            except Exception:
                continue

        if not applied:
            try:
                _espeak.setVoiceByLanguage("en")
                code = "en"
                applied = True
            except Exception:
                log.error("nvSpeechPlayer: could not set language", exc_info=True)

        self._language = code

        # Keep frontend pack selection in sync with the driver language.
        try:
            if getattr(self, "_frontend", None):
                # Packs are stored by language tag. Region variants (e.g. "es-mx")
                # may not exist even when the base language ("es") does.
                tag = str(code or "").strip().lower().replace("_", "-")
                candidates = [tag]
                if "-" in tag:
                    candidates.append(tag.split("-", 1)[0])
                candidates.append("default")

                loaded = False
                for cand in candidates:
                    try:
                        if self._frontend.setLanguage(cand):
                            loaded = True
                            break
                    except Exception:
                        log.debug(
                            "nvSpeechPlayer: frontend.setLanguage failed for %r", cand, exc_info=True
                        )
                        continue

                if not loaded:
                    log.error(
                        f"nvSpeechPlayer: frontend could not load pack for '{code}' (tried {candidates}): {self._frontend.getLastError()}"
                    )
        except Exception:
            log.error("nvSpeechPlayer: error setting frontend language", exc_info=True)

        # Refresh cached language-pack settings for the (possibly) new language.
        try:
            self._refreshLangPackSettingsCache()
        except Exception:
            log.debug("nvSpeechPlayer: could not refresh language-pack cache", exc_info=True)

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
                                "nvSpeechPlayer: updateDriverSettings failed",
                                exc_info=True,
                            )
                        break
                break
        except Exception:
            # GUI may not be available (e.g., running in secure mode or during shutdown).
            log.debug("nvSpeechPlayer: could not refresh settings panel", exc_info=True)

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
        """Return the current language tag in the pack file format (lowercase, hyphen)."""
        return str(getattr(self, "_language", "default") or "default").strip().lower().replace("_", "-")

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
                log.debug("nvSpeechPlayer: frontend.setLanguage failed for %r", cand, exc_info=True)
                continue

        try:
            lastErr = self._frontend.getLastError()
        except Exception:
            log.debug("nvSpeechPlayer: frontend.getLastError failed", exc_info=True)
            lastErr = None
        log.error("nvSpeechPlayer: frontend could not load pack for %r (tried %s). %s", tag, candidates, lastErr)
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
                log.debug("nvSpeechPlayer: could not refresh language-pack cache after reload", exc_info=True)
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
                log.error("nvSpeechPlayer: failed to read language-pack settings for %r", tag, exc_info=True)
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
                    "nvSpeechPlayer: error comparing language-pack setting %s", key, exc_info=True
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
            log.error("nvSpeechPlayer: failed to update language-pack setting %s", key, exc_info=True)

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
            ("always", VoiceInfo("always", "Always")),
            ("after-vowel", VoiceInfo("after-vowel", "After vowel")),
            ("vowel-and-cluster", VoiceInfo("vowel-and-cluster", "Vowel and cluster")),
            ("none", VoiceInfo("none", "None")),
        )
    )

    _SPELLING_DIPHTHONG_MODES = OrderedDict(
        (
            ("none", VoiceInfo("none", "None")),
            ("monophthong", VoiceInfo("monophthong", "Monophthong")),
        )
    )

    _TONE_CONTOURS_MODES = OrderedDict(
        (
            ("absolute", VoiceInfo("absolute", "Absolute")),
            ("relative", VoiceInfo("relative", "Relative")),
        )
    )

    _COARTICULATION_ADJACENCY_MODES = OrderedDict(
        (
            ("0", VoiceInfo("0", "Immediate neighbors only")),
            ("1", VoiceInfo("1", "Allow C_V (one consonant)")),
            ("2", VoiceInfo("2", "Allow CC_V (two consonants)")),
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
        ("legacyPitchMode", "legacyPitchMode", "bool", False, None),
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

    def _enqueue(self, func, *args, **kwargs):
        if self._bgStop.is_set():
            return
        self._bgQueue.put((func, args, kwargs))

    def _notifyIndexesAndDone(self, indexes):
        for i in indexes:
            synthIndexReached.notify(synth=self, index=i)
        synthDoneSpeaking.notify(synth=self)

    def _espeakTextToIPA(self, text: str) -> str:
        if not text:
            return ""
        
        # Safety check: ensure espeak is initialized and available
        if not getattr(self, "_espeakReady", False):
            log.debug("nvSpeechPlayer: espeak not ready, skipping IPA conversion")
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
                log.error(f"nvSpeechPlayer: espeak_TextToPhonemes failed: {e}")
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
            self._enqueue(self._notifyIndexesAndDone, indexes)
            return

        self._enqueue(self._speakBg, list(speechSequence))

    def _speakBg(self, speakList):
        hadRealSpeech = False
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
            # Speak text for this block.
            if text:
                for chunk in re_textPause.split(text):
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

                    ipaText = self._espeakTextToIPA(chunk)
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

                    def _onFrame(framePtr, frameDuration, fadeDuration, idxToSet):
                        nonlocal queuedCount, hadRealSpeech, sawRealFrameInThisUtterance, sawSilenceAfterVoice, lastStreamWasVoiced

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
                        self._player.queueFrame(frame, frameDuration, fadeDuration, userIndex=idxToSet)
                        queuedCount += 1
                        lastStreamWasVoiced = True

                    ok = False
                    try:
                        ok = self._frontend.queueIPA(
                            ipaText,
                            speed=self._curRate,
                            basePitch=basePitch,
                            inflection=self._curInflection,
                            clauseType=clauseType,
                            userIndex=None,
                            onFrame=_onFrame,
                        )
                    except Exception:
                        log.error("nvSpeechPlayer: frontend queueIPA failed", exc_info=True)
                        ok = False

                    if not ok:
                        err = self._frontend.getLastError()
                        if err:
                            log.error(f"nvSpeechPlayer: frontend error: {err}")

                    # If the frontend fails or outputs nothing, keep going (indexes are still queued).
                    if (not ok) or queuedCount <= 0:
                        continue

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
                            log.debug("nvSpeechPlayer: failed inserting punctuation pause", exc_info=True)

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
                        log.debug("nvSpeechPlayer: failed to queue index marker %r", idx, exc_info=True)

        if endPause and endPause > 0:
            self._player.queueFrame(None, float(endPause), min(float(endPause), 5.0))

        # Tiny tail fade to smooth utterance end
        if hadRealSpeech:
            self._player.queueFrame(None, 1.0, 1.0)

        self._audio.isSpeaking = True
        self._audio.kick()

    def cancel(self):
        """Cancel current speech immediately."""
        try:
            # Guard against early calls before __init__ completes
            if not hasattr(self, "_player") or not self._player:
                return
            if not hasattr(self, "_audio") or not self._audio:
                return
                
            self._player.queueFrame(None, 3.0, 3.0, purgeQueue=True)
            self._audio.isSpeaking = False
            self._audio.kick()
            if self._audio._wavePlayer:
                self._audio._wavePlayer.stop()
            self._audio._applyFadeIn = True
        except Exception:
            log.debug("nvSpeechPlayer: cancel failed", exc_info=True)

    def pause(self, switch):
        try:
            if self._audio and self._audio._wavePlayer:
                self._audio._wavePlayer.pause(switch)
        except Exception:
            log.debug("nvSpeechPlayer: pause failed", exc_info=True)

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
                except Exception:
                    pass
            
            # Terminate audio thread FIRST (it uses the player)
            # Do this before waiting on bgThread since audio thread is higher priority
            if hasattr(self, "_audio") and self._audio:
                try:
                    self._audio.terminate()
                except Exception:
                    log.debug("nvSpeechPlayer: audio terminate failed", exc_info=True)
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
                    log.debug("nvSpeechPlayer: bgThread join failed", exc_info=True)
                self._bgThread = None
            
            # Terminate frontend (unloads nvspFrontend.dll)
            # Do this BEFORE terminating player since frontend may reference player resources
            if getattr(self, "_frontend", None):
                try:
                    self._frontend.terminate()
                except Exception:
                    log.debug("nvSpeechPlayer: frontend terminate failed", exc_info=True)
                self._frontend = None
            
            # Terminate player last (unloads speechPlayer.dll)
            if hasattr(self, "_player") and self._player:
                try:
                    self._player.terminate()
                except Exception:
                    log.debug("nvSpeechPlayer: player terminate failed", exc_info=True)
                self._player = None
            
            # Finally terminate espeak
            try:
                _espeak.terminate()
            except Exception:
                log.debug("nvSpeechPlayer: espeak terminate failed", exc_info=True)
        except Exception:
            log.debug("nvSpeechPlayer: terminate failed", exc_info=True)

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
            log.debug("nvSpeechPlayer: parent loadSettings failed", exc_info=True)
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
                    log.debug(f"nvSpeechPlayer: loadSettings corrupted voice from {voiceBeforeLoad} to {curVoice}, restoring")
                    self._curVoice = voiceBeforeLoad
                    curVoice = voiceBeforeLoad
            
            # Re-apply frontend profile to ensure phonetic transformations are active
            if curVoice and curVoice.startswith(VOICE_PROFILE_PREFIX):
                profileName = curVoice[len(VOICE_PROFILE_PREFIX):]
                if hasattr(self, "_frontend") and self._frontend:
                    self._frontend.setVoiceProfile(profileName)
                    self._usingVoiceProfile = True
                    self._activeProfileName = profileName
                    # Re-apply voicing tone as well
                    self._applyVoicingTone(profileName)
                    log.debug(f"nvSpeechPlayer: loadSettings re-synced frontend profile '{profileName}'")
            else:
                # Python preset or no voice - ensure frontend profile is cleared
                if hasattr(self, "_frontend") and self._frontend:
                    self._frontend.setVoiceProfile("")
                    self._usingVoiceProfile = False
                    self._activeProfileName = ""
                    self._applyVoicingTone("")
        except Exception:
            log.debug("nvSpeechPlayer: loadSettings frontend re-sync failed", exc_info=True)

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

    def _applyVoicingTone(self, profileName: str) -> None:
        """Apply DSP-level voicing tone parameters safely.
        
        This sets the wave generator's glottal pulse shape, pre-emphasis, spectral
        tilt, and high-shelf EQ based on the voicingTone block in the voice profile YAML
        or from the predefined Python voices dict.
        
        ALSO ensures the frontend voice profile (formant transforms) is applied.
        
        CRITICAL: This entire function is wrapped in try/except to prevent crashes
        from killing NVDA's settings application loop.
        """
        # Guard: need player
        if not hasattr(self, "_player") or not self._player:
            return
        
        # CRITICAL: Catch ALL exceptions here.
        # If YAML parsing returns a string instead of a float, or if the DLL rejects values,
        # raising an exception here crashes NVDA's settings application loop.
        try:
            # ALWAYS ensure the frontend has the correct voice profile set
            # This is critical because loadSettings may call us without going through _set_voice
            if hasattr(self, "_frontend") and self._frontend:
                self._frontend.setVoiceProfile(profileName or "")
            
            playerHasSupport = getattr(self._player, "hasVoicingToneSupport", lambda: False)()
            if not playerHasSupport:
                return
            
            # Helper to safely cast config values to float
            def safe_float(val, default=0.0):
                try:
                    return float(val)
                except (ValueError, TypeError):
                    return default
            
            toneParams = None
            
            # First, check YAML voice profiles (takes priority)
            if profileName:
                voicingTones = getattr(self, "_voicingTones", {}) or {}
                toneParams = voicingTones.get(profileName)
            
            # If no YAML tone, check predefined Python voices dict
            if toneParams is None:
                curVoice = getattr(self, "_curVoice", "Adam")
                voiceDict = voices.get(curVoice, {})
                
                voicingToneFields = ["voicingPeakPos", "voicedPreEmphA", "voicedPreEmphMix",
                                     "highShelfGainDb", "highShelfFcHz", "highShelfQ", 
                                     "voicedTiltDbPerOct"]
                predefinedTone = {k: v for k, v in voiceDict.items() if k in voicingToneFields}
                if predefinedTone:
                    toneParams = predefinedTone
            
            # Build the tone struct with safe defaults
            tone = speechPlayer.VoicingTone.defaults()
            
            if toneParams:
                if "voicingPeakPos" in toneParams:
                    tone.voicingPeakPos = safe_float(toneParams["voicingPeakPos"], 0.91)
                if "voicedPreEmphA" in toneParams:
                    tone.voicedPreEmphA = safe_float(toneParams["voicedPreEmphA"], 0.92)
                if "voicedPreEmphMix" in toneParams:
                    tone.voicedPreEmphMix = safe_float(toneParams["voicedPreEmphMix"], 0.35)
                if "highShelfGainDb" in toneParams:
                    tone.highShelfGainDb = safe_float(toneParams["highShelfGainDb"], 2.0)
                if "highShelfFcHz" in toneParams:
                    tone.highShelfFcHz = safe_float(toneParams["highShelfFcHz"], 2800.0)
                if "highShelfQ" in toneParams:
                    tone.highShelfQ = safe_float(toneParams["highShelfQ"], 0.7)
                if "voicedTiltDbPerOct" in toneParams:
                    tone.voicedTiltDbPerOct = safe_float(toneParams["voicedTiltDbPerOct"], 0.0)
            
            # Apply voice tilt OFFSET from the slider
            tiltSlider = safe_float(getattr(self, "_curVoiceTilt", 50), 50.0)
            tiltOffset = (tiltSlider - 50.0) * (24.0 / 50.0)
            tone.voicedTiltDbPerOct += tiltOffset
            
            # Clamp to valid range
            tone.voicedTiltDbPerOct = max(-24.0, min(24.0, tone.voicedTiltDbPerOct))
            
            # Apply to player
            self._player.setVoicingTone(tone)
            self._lastAppliedVoicingTone = tone
            
        except Exception as e:
            # Log the error but DO NOT CRASH.
            # This allows the "OK" button to succeed even if audio params are wonky.
            log.error(f"nvSpeechPlayer: _applyVoicingTone failed: {e}", exc_info=True)

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
                    log.debug(f"nvSpeechPlayer: reapplied voice profile '{profileName}'")
            else:
                # Python preset - ensure voicing tone is at defaults
                self._applyVoicingTone("")
        except Exception:
            log.debug("nvSpeechPlayer: _reapplyVoiceProfile failed", exc_info=True)

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
            
            # Initialize per-voice tilt storage if needed
            if not hasattr(self, "_perVoiceTilt"):
                self._perVoiceTilt = {}
            
            # Save current tilt for the OLD voice before switching
            if voiceChanged and oldVoice:
                self._perVoiceTilt[oldVoice] = getattr(self, "_curVoiceTilt", 50)
            
            self._curVoice = voice
            
            # Restore tilt for the NEW voice (or default to 50 if never set)
            if voiceChanged and voice:
                self._curVoiceTilt = self._perVoiceTilt.get(voice, 50)
            
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
            log.debug("nvSpeechPlayer: _set_voice failed", exc_info=True)

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
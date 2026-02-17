"""NVDA Settings panel for editing TGSpeechBox language-pack YAML settings.

This provides the UX described in readme.md:
  - Choose a language tag
  - Choose a setting key from an alphabetized combo box and edit its value
  - Apply the pending edits when the user presses OK in NVDA's Settings dialog

The panel intentionally avoids a full YAML UI; it only edits ``settings:`` keys.

Implementation notes:
  - NVDA can import synth drivers before the GUI is fully initialized.
    Importing wx/gui modules at *module import time* can therefore fail.
  - To keep this robust across NVDA 2024.1 .. 2026.1, all GUI imports and the
    SettingsPanel subclass definition are done lazily when registerSettingsPanel
    is called (typically from SynthDriver.__init__).
"""

from __future__ import annotations

import os
from typing import Dict, Optional


try:
    import addonHandler  # type: ignore
    addonHandler.initTranslation()
except Exception:
    def _(s): return s


import re as _re


def _splitCamelCase(s: str) -> str:
    """Split camelCase into space-separated words: 'vowelToStopFadeMs' -> 'vowel To Stop Fade Ms'."""
    s = _re.sub(r"([a-z0-9])([A-Z])", r"\1 \2", s)
    s = _re.sub(r"([A-Z]+)([A-Z][a-z])", r"\1 \2", s)
    return s


def _prettifyField(s: str) -> str:
    """Capitalize first letter, uppercase formant refs, convert trailing units."""
    if not s:
        return s
    s = s[0].upper() + s[1:]
    # Uppercase formant/bandwidth refs: f1→F1, b1→B1, cf2→CF2, pf3→PF3
    s = _re.sub(r"\b([cfpb])(\d)", lambda m: m.group(1).upper() + m.group(2), s)
    # Only convert units at the very END of the string to avoid mid-string mangling
    s = _re.sub(r"\s+Ms$", " (ms)", s)
    s = _re.sub(r"\s+Hz$", " (Hz)", s)
    s = _re.sub(r"\s+Db$", " (dB)", s)
    s = _re.sub(r"\s+Pct$", " (%)", s)
    return s


def _autoDisplayName(key: str) -> str:
    """Generate a human-readable display name from an internal YAML key.

    Nested keys   → 'Category: Field'
    Sub-nested    → 'Category: Sub-Block Field'
    Flat camelCase → 'Title Cased Words'
    """
    if "." in key:
        parts = key.split(".")
        category = _splitCamelCase(parts[0]).title()
        fieldParts = [_splitCamelCase(p) for p in parts[1:]]
        field = " ".join(fieldParts)
        field = _prettifyField(field)
        return f"{category}: {field}"
    name = _splitCamelCase(key)
    return _prettifyField(name)


def _displayNameForKey(key: str) -> str:
    """Return the localized display name for a YAML setting key."""
    return _(_autoDisplayName(key))


# Reverse-lookup cache: populated lazily per call
_reverseCache: Dict[str, str] = {}
_reverseCacheKeys: Optional[list] = None


def _keyForDisplayName(displayName: str, knownKeys: list) -> str:
    """Reverse-lookup: localized display name -> raw YAML key."""
    global _reverseCache, _reverseCacheKeys
    if _reverseCacheKeys is not knownKeys:
        _reverseCache = {_displayNameForKey(k): k for k in knownKeys}
        _reverseCacheKeys = knownKeys
    return _reverseCache.get(displayName, displayName)


def _getPacksDir() -> str:
    # This module lives next to synthDrivers/tgSpeechBox/__init__.py
    return os.path.join(os.path.dirname(__file__), "packs")


from . import langPackYaml

GitHub_URL = "https://github.com/tgeczy/TGSpeechBox"
ADDON_UPDATE_URL = "https://eurpod.com/synths/tgSpeechBox-2026.nvda-addon"
ADDON_VERSION_URL = "https://eurpod.com/tgSpeechBox-version.txt"


def _getInstalledAddonVersion() -> str:
    """Read the version from this addon's manifest.ini file.
    
    Returns the version string (e.g., "170") or empty string if not found.
    """
    try:
        # manifest.ini is in the addon root, which is parent of synthDrivers/tgSpeechBox
        addonDir = os.path.dirname(os.path.dirname(os.path.dirname(__file__)))
        manifestPath = os.path.join(addonDir, "manifest.ini")
        
        if not os.path.isfile(manifestPath):
            return ""
        
        with open(manifestPath, "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if line.lower().startswith("version"):
                    # Parse "version = 170" or "version=170"
                    parts = line.split("=", 1)
                    if len(parts) == 2:
                        return parts[1].strip()
        return ""
    except Exception:
        return ""


def _parseVersionTuple(versionStr: str):
    """Parse a version string into a tuple of integers for comparison.
    
    Handles versions like "170", "2026.1.27", "2026.1.27.2", etc.
    Non-numeric parts are treated as 0.
    """
    if not versionStr:
        return (0,)
    parts = []
    for part in versionStr.replace("-", ".").replace("_", ".").split("."):
        try:
            parts.append(int(part))
        except ValueError:
            # Non-numeric part (e.g., "beta"), treat as 0
            parts.append(0)
    return tuple(parts) if parts else (0,)


def _isNewerVersion(remoteVersion: str, installedVersion: str) -> bool:
    """Return True if remoteVersion is newer than installedVersion.
    
    Compares version tuples element by element. A shorter version is padded
    with zeros for comparison (e.g., "2026.1.27" vs "2026.1.27.2").
    """
    remote = _parseVersionTuple(remoteVersion)
    installed = _parseVersionTuple(installedVersion)
    
    # Pad shorter tuple with zeros
    maxLen = max(len(remote), len(installed))
    remote = remote + (0,) * (maxLen - len(remote))
    installed = installed + (0,) * (maxLen - len(installed))
    
    return remote > installed


_PANEL_CLS = None


def _getPanelClass():
    """Return the SettingsPanel subclass, defining it lazily when wx is available."""
    global _PANEL_CLS
    if _PANEL_CLS is not None:
        return _PANEL_CLS

    # Resolve SettingsPanel base across NVDA versions.
    try:
        from gui.settingsDialogs import SettingsPanel as SettingsPanelBase
    except Exception:
        try:
            # Some NVDA builds may relocate panels.
            from gui.settingsPanels import SettingsPanel as SettingsPanelBase  # type: ignore
        except Exception:
            # GUI not ready yet.
            return None

    class TGSpeechBoxLanguagePacksPanel(SettingsPanelBase):
        title = _("TGSpeechBox language packs")

        def __init__(self, *args, **kwargs):
            # NOTE:
            # NVDA's SettingsPanel base class builds the GUI *inside its __init__*
            # (it calls makeSettings via _buildGui). That means attributes used by
            # makeSettings MUST exist before we call super().__init__().
            self._packsDir = _getPacksDir()
            self._knownKeys = []
            # Pending edits are stored per-language so switching the language tag
            # doesn't accidentally apply the previous language's edits.
            #
            #   normalizedLangTag -> { key -> rawValueString }
            self._pending: Dict[str, Dict[str, str]] = {}
            self._currentKey: Optional[str] = None

            # Guard to prevent EVT_TEXT handlers from recording pending edits when
            # we are programmatically populating controls.
            self._isPopulating = False

            super().__init__(*args, **kwargs)

        # ----- NVDA SettingsPanel hooks -----

        def _addLabeledControlCompat(self, sHelper, label, controlClass, **kwargs):
            """Add a labeled control with compatibility for older NVDA versions.
            
            Older NVDA versions (e.g., 2023.2 with Python 3.10) have a more limited
            addLabeledControl that doesn't support all control types like wx.ComboBox.
            This helper creates the label and control manually if needed.
            """
            import wx
            
            # First, try the modern NVDA approach
            try:
                return sHelper.addLabeledControl(label, controlClass, **kwargs)
            except (NotImplementedError, TypeError, AttributeError):
                pass
            
            # Fallback: create label and control manually
            # Create horizontal sizer for label + control
            hSizer = wx.BoxSizer(wx.HORIZONTAL)
            
            # Create label
            labelCtrl = wx.StaticText(self, label=label)
            hSizer.Add(labelCtrl, 0, wx.ALIGN_CENTER_VERTICAL | wx.RIGHT, 5)
            
            # Create the control
            ctrl = controlClass(self, **kwargs)
            hSizer.Add(ctrl, 1, wx.EXPAND)
            
            # Add the horizontal sizer to the helper's main sizer
            sHelper.addItem(hSizer)
            
            return ctrl

        def makeSettings(self, settingsSizer):
            # Import GUI pieces lazily.
            try:
                import wx
                from gui import guiHelper
            except Exception:
                return

            sHelper = guiHelper.BoxSizerHelper(self, sizer=settingsSizer)

            sHelper.addItem(
                wx.StaticText(
                    self,
                    label=_(
                        "Edit language-pack settings (packs/lang/*.yaml) without using Notepad. "
                        "Only the YAML 'settings:' section is edited."
                    ),
                )
            )

            # Language tag control.
            self.langTagCtrl = self._addLabeledControlCompat(
                sHelper,
                _("Language tag:"),
                wx.ComboBox,
                choices=self._getLanguageChoices(),
                style=wx.CB_DROPDOWN,
            )
            self.langTagCtrl.Bind(wx.EVT_TEXT, self._onLangTagChanged)
            self.langTagCtrl.Bind(wx.EVT_COMBOBOX, self._onLangTagChanged)

            # --- Reset to Factory Defaults button ---
            self.resetDefaultsButton = sHelper.addItem(
                wx.Button(self, label=_("Reset Language Pack Defaults..."))
            )
            self.resetDefaultsButton.Bind(wx.EVT_BUTTON, self._onResetDefaultsClick)

            # --- Save Voice Profile Settings button ---
            self.saveVoiceProfileButton = sHelper.addItem(
                wx.Button(self, label=_("Save Voice Profile Sliders to YAML..."))
            )
            self.saveVoiceProfileButton.Bind(wx.EVT_BUTTON, self._onSaveVoiceProfileClick)

            # --- Setting key/value editor ---
            sHelper.addItem(wx.StaticText(self, label=_("Edit setting:")))

            self._knownKeys = langPackYaml.listKnownSettingKeys(self._packsDir) or []
            # Ensure all pack.h/pack.cpp setting keys are available in the
            # combo box, even if default.yaml hasn't been updated yet.
            # Source of truth: LangPack struct in pack.h + nested block
            # parsing in pack.cpp. Alphabetized for the combo box.
            _extraKeys = [
                # --- Allophone rules ---
                "allophoneRules.enabled",
                "allophoneRulesEnabled",
                # --- Boundary smoothing (flat keys) ---
                "boundarySmoothingFricToStopMs",
                "boundarySmoothingFricToVowelMs",
                "boundarySmoothingLiquidToStopMs",
                "boundarySmoothingLiquidToVowelMs",
                "boundarySmoothingNasalF1Instant",
                "boundarySmoothingNasalF2F3SpansPhone",
                "boundarySmoothingNasalToStopMs",
                "boundarySmoothingNasalToVowelMs",
                "boundarySmoothingPlosiveSpansPhone",
                "boundarySmoothingStopToFricMs",
                "boundarySmoothingStopToVowelMs",
                "boundarySmoothingVowelToFricMs",
                "boundarySmoothingVowelToLiquidMs",
                "boundarySmoothingVowelToNasalMs",
                "boundarySmoothingVowelToStopMs",
                "boundarySmoothingVowelToVowelMs",
                # --- Boundary smoothing (nested block) ---
                "boundarySmoothing.enabled",
                "boundarySmoothing.f1Scale",
                "boundarySmoothing.f2Scale",
                "boundarySmoothing.f3Scale",
                # --- Clause-final ---
                "clauseFinalFadeMs",
                # --- Cluster blend (nested block) ---
                "clusterBlend.defaultPairScale",
                "clusterBlend.enabled",
                "clusterBlend.f1Scale",
                "clusterBlend.fricToFricScale",
                "clusterBlend.fricToStopScale",
                "clusterBlend.homorganicScale",
                "clusterBlend.liquidToFricScale",
                "clusterBlend.liquidToStopScale",
                "clusterBlend.nasalToFricScale",
                "clusterBlend.nasalToStopScale",
                "clusterBlend.stopToFricScale",
                "clusterBlend.stopToStopScale",
                "clusterBlend.strength",
                "clusterBlend.wordBoundaryScale",
                # --- Cluster blend (flat keys) ---
                "clusterBlendDefaultPairScale",
                "clusterBlendEnabled",
                "clusterBlendF1Scale",
                "clusterBlendFricToFricScale",
                "clusterBlendFricToStopScale",
                "clusterBlendHomorganicScale",
                "clusterBlendLiquidToFricScale",
                "clusterBlendLiquidToStopScale",
                "clusterBlendNasalToFricScale",
                "clusterBlendNasalToStopScale",
                "clusterBlendStopToFricScale",
                "clusterBlendStopToStopScale",
                "clusterBlendStrength",
                "clusterBlendWordBoundaryScale",
                # --- Cluster timing (nested block) ---
                "clusterTiming.affricateInClusterScale",
                "clusterTiming.enabled",
                "clusterTiming.fricBeforeFricScale",
                "clusterTiming.fricBeforeStopScale",
                "clusterTiming.stopBeforeFricScale",
                "clusterTiming.stopBeforeStopScale",
                "clusterTiming.tripleClusterMiddleScale",
                "clusterTiming.wordFinalObstruentScale",
                "clusterTiming.wordMedialConsonantScale",
                # --- Coarticulation ---
                "coarticulationAdjacencyMaxConsonants",
                "coarticulationAlveolarBackVowelStrengthBoost",
                "coarticulationAlveolarF2Locus",
                "coarticulationAlveolarScale",
                "coarticulationAspirationBlendEnd",
                "coarticulationAspirationBlendStart",
                "coarticulationBackVowelF2Threshold",
                "coarticulationEnabled",
                "coarticulationF1Scale",
                "coarticulationF2Scale",
                "coarticulationF3Scale",
                "coarticulationGraduated",
                "coarticulationLabialF2Locus",
                "coarticulationLabialScale",
                "coarticulationLabializedFricativeF2Pull",
                "coarticulationMitalkK",
                "coarticulationPalatalScale",
                "coarticulationStrength",
                "coarticulationTransitionExtent",
                "coarticulationVelarF2Locus",
                "coarticulationVelarPinchEnabled",
                "coarticulationVelarPinchF2Scale",
                "coarticulationVelarPinchF3",
                "coarticulationVelarPinchThreshold",
                "coarticulationVelarScale",
                "coarticulationWordInitialFadeScale",
                # --- Defaults ---
                "defaultFadeMs",
                "defaultVowelDurationMs",
                # --- English-specific ---
                "englishLongUKey",
                "englishLongUShortenEnabled",
                "englishLongUWordFinalScale",
                # --- Fujisaki pitch model ---
                "fujisakiAccentDur",
                "fujisakiAccentLen",
                "fujisakiAccentMode",
                "fujisakiDeclinationMax",
                "fujisakiDeclinationRate",
                "fujisakiDeclinationScale",
                "fujisakiPhraseAmp",
                "fujisakiPhraseDecay",
                "fujisakiPhraseLen",
                "fujisakiPrimaryAccentAmp",
                "fujisakiSecondaryAccentAmp",
                # --- Legacy pitch ---
                "legacyPitchInflectionScale",
                "legacyPitchMode",
                # --- Length contrast (nested block) ---
                "lengthContrast.enabled",
                "lengthContrast.geminateClosureScale",
                "lengthContrast.geminateReleaseScale",
                "lengthContrast.longVowelFloor",
                "lengthContrast.preGeminateVowelScale",
                "lengthContrast.shortVowelCeiling",
                "lengthenedScale",
                "lengthenedVowelFinalCodaScale",
                # --- Liquid dynamics (nested block) ---
                "liquidDynamics.enabled",
                "liquidDynamics.labialGlideTransition.enabled",
                "liquidDynamics.labialGlideTransition.startF1",
                "liquidDynamics.labialGlideTransition.startF2",
                "liquidDynamics.labialGlideTransition.transitionPct",
                "liquidDynamics.lateralOnglide.durationPct",
                "liquidDynamics.lateralOnglide.f1Delta",
                "liquidDynamics.lateralOnglide.f2Delta",
                "liquidDynamics.rhoticF3Dip.dipDurationPct",
                "liquidDynamics.rhoticF3Dip.enabled",
                "liquidDynamics.rhoticF3Dip.f3Minimum",
                # --- Microprosody ---
                "microprosodyEnabled",
                "microprosodyFollowingF0Enabled",
                "microprosodyFollowingVoicedLowerHz",
                "microprosodyFollowingVoicelessRaiseHz",
                "microprosodyIntrinsicF0Enabled",
                "microprosodyIntrinsicF0HighRaiseHz",
                "microprosodyIntrinsicF0HighThreshold",
                "microprosodyIntrinsicF0LowDropHz",
                "microprosodyIntrinsicF0LowThreshold",
                "microprosodyMaxTotalDeltaHz",
                "microprosodyMinVowelMs",
                "microprosodyPreVoicelessMinMs",
                "microprosodyPreVoicelessShortenEnabled",
                "microprosodyPreVoicelessShortenScale",
                "microprosodyVoicedF0LowerEnabled",
                "microprosodyVoicedF0LowerHz",
                "microprosodyVoicedFricativeLowerScale",
                "microprosodyVoicelessF0RaiseEnabled",
                "microprosodyVoicelessF0RaiseEndHz",
                "microprosodyVoicelessF0RaiseHz",
                # --- Nasalization ---
                "nasalizationAnticipatoryAmplitude",
                "nasalizationAnticipatoryBlend",
                "nasalizationAnticipatoryEnabled",
                # --- Phrase-final lengthening ---
                "phraseFinalLengtheningCodaFricativeScale",
                "phraseFinalLengtheningCodaScale",
                "phraseFinalLengtheningCodaStopScale",
                "phraseFinalLengtheningEnabled",
                "phraseFinalLengtheningFinalSyllableScale",
                "phraseFinalLengtheningNucleusOnlyMode",
                "phraseFinalLengtheningNucleusScale",
                "phraseFinalLengtheningPenultimateSyllableScale",
                "phraseFinalLengtheningQuestionScale",
                "phraseFinalLengtheningStatementScale",
                # --- Prominence (nested block) ---
                "prominence.amplitudeBoostDb",
                "prominence.amplitudeReductionDb",
                "prominence.durationProminentFloorMs",
                "prominence.durationReducedCeiling",
                "prominence.enabled",
                "prominence.longVowelMode",
                "prominence.longVowelWeight",
                "prominence.pitchFromProminence",
                "prominence.primaryStressWeight",
                "prominence.secondaryStressLevel",
                "prominence.secondaryStressWeight",
                "prominence.wordFinalReduction",
                "prominence.wordInitialBoost",
                # --- Prominence (flat keys) ---
                "prominenceAmplitudeBoostDb",
                "prominenceAmplitudeReductionDb",
                "prominenceDurationProminentFloorMs",
                "prominenceDurationReducedCeiling",
                "prominenceEnabled",
                "prominenceLongVowelMode",
                "prominenceLongVowelWeight",
                "prominencePitchFromProminence",
                "prominencePrimaryStressWeight",
                "prominenceSecondaryStressLevel",
                "prominenceSecondaryStressWeight",
                "prominenceWordFinalReduction",
                "prominenceWordInitialBoost",
                # --- Post-stop aspiration ---
                "postStopAspirationDurationMs",
                "postStopAspirationEnabled",
                "postStopAspirationPhoneme",
                # --- Stress timing ---
                "primaryStressDiv",
                "secondaryStressDiv",
                # --- Nasal floor ---
                "nasalMinDurationMs",
                # --- Rate compensation ---
                "rateCompAffricateFloorMs",
                "rateCompClusterMaxRatioShift",
                "rateCompClusterProportionGuard",
                "rateCompEnabled",
                "rateCompFloorSpeedScale",
                "rateCompFricativeFloorMs",
                "rateCompLiquidFloorMs",
                "rateCompNasalFloorMs",
                "rateCompSchwaReductionEnabled",
                "rateCompSchwaScale",
                "rateCompSchwaThreshold",
                "rateCompSemivowelFloorMs",
                "rateCompStopFloorMs",
                "rateCompTapFloorMs",
                "rateCompTrillFloorMs",
                "rateCompVoicedConsonantFloorMs",
                "rateCompVowelFloorMs",
                "rateCompWordFinalBonusMs",
                # --- Segment boundary ---
                "segmentBoundaryFadeMs",
                "segmentBoundaryGapMs",
                "segmentBoundarySkipVowelToLiquid",
                "segmentBoundarySkipVowelToVowel",
                "semivowelOffglideScale",
                # --- Single-word tuning ---
                "singleWordClauseTypeOverride",
                "singleWordClauseTypeOverrideCommaOnly",
                "singleWordFinalFadeMs",
                "singleWordFinalHoldMs",
                "singleWordFinalLiquidHoldScale",
                "singleWordTuningEnabled",
                # --- Special coarticulation (nested block) ---
                "specialCoarticulation.enabled",
                "specialCoarticulation.maxDeltaHz",
                # --- Spelling / misc ---
                "spellingDiphthongMode",
                "stopClosureAfterNasalsEnabled",
                "stopClosureClusterFadeMs",
                "stopClosureClusterGapMs",
                "stopClosureMode",
                "stopClosureVowelFadeMs",
                "stopClosureVowelGapMs",
                "stressedVowelHiatusFadeMs",
                "stressedVowelHiatusGapMs",
                "stripAllophoneDigits",
                # --- Trajectory limit (nested block) ---
                "trajectoryLimit.applyAcrossWordBoundary",
                "trajectoryLimit.applyTo",
                "trajectoryLimit.enabled",
                "trajectoryLimit.liquidRateScale",
                "trajectoryLimit.maxHzPerMs.cf2",
                "trajectoryLimit.maxHzPerMs.cf3",
                "trajectoryLimit.maxHzPerMs.pf2",
                "trajectoryLimit.maxHzPerMs.pf3",
                "trajectoryLimit.windowMs",
                # --- Trill ---
                "trillModulationFadeMs",
                "trillModulationMs",
                # --- Timing defaults ---
                "voicedConsonantDurationMs",
                "voicelessFricativeDurationMs",
                "vowelBeforeLiquidDurationMs",
                "vowelBeforeNasalDurationMs",
                # --- Word-final schwa ---
                "wordFinalSchwaMinDurationMs",
                "wordFinalSchwaReductionEnabled",
                "wordFinalSchwaScale",
                # --- Keys below were added in bulk to sync with pack.cpp ---
                # Timing defaults (flat)
                "affricateDurationMs",
                "fadeAfterLiquidMs",
                "liquidFadeMs",
                "stopDurationMs",
                "tapDurationMs",
                "tiedFromVowelDurationMs",
                "tiedFromVowelFadeMs",
                "tiedVowelDurationMs",
                "trillFallbackDurationMs",
                # Boundary smoothing — place-of-articulation scales (nested)
                "boundarySmoothing.alveolarF1Scale",
                "boundarySmoothing.alveolarF2Scale",
                "boundarySmoothing.alveolarF3Scale",
                "boundarySmoothing.labialF1Scale",
                "boundarySmoothing.labialF2Scale",
                "boundarySmoothing.labialF3Scale",
                "boundarySmoothing.palatalF1Scale",
                "boundarySmoothing.palatalF2Scale",
                "boundarySmoothing.palatalF3Scale",
                "boundarySmoothing.velarF1Scale",
                "boundarySmoothing.velarF2Scale",
                "boundarySmoothing.velarF3Scale",
                "boundarySmoothing.withinSyllableFadeScale",
                "boundarySmoothing.withinSyllableScale",
                # Boundary smoothing — nested fade times
                "boundarySmoothing.fricToStopFadeMs",
                "boundarySmoothing.fricToVowelFadeMs",
                "boundarySmoothing.liquidToStopFadeMs",
                "boundarySmoothing.liquidToVowelFadeMs",
                "boundarySmoothing.nasalF1Instant",
                "boundarySmoothing.nasalF2F3SpansPhone",
                "boundarySmoothing.nasalToStopFadeMs",
                "boundarySmoothing.nasalToVowelFadeMs",
                "boundarySmoothing.plosiveSpansPhone",
                "boundarySmoothing.stopToFricFadeMs",
                "boundarySmoothing.stopToVowelFadeMs",
                "boundarySmoothing.vowelToFricFadeMs",
                "boundarySmoothing.vowelToLiquidFadeMs",
                "boundarySmoothing.vowelToNasalFadeMs",
                "boundarySmoothing.vowelToStopFadeMs",
                "boundarySmoothing.vowelToVowelFadeMs",
                # Boundary smoothing — flat keys missing previously
                "boundarySmoothingEnabled",
                "boundarySmoothingF1Scale",
                "boundarySmoothingF2Scale",
                "boundarySmoothingF3Scale",
                # Cluster blend — forward drift
                "clusterBlend.forwardDriftStrength",
                "clusterBlendForwardDriftStrength",
                # Cluster timing — flat keys
                "clusterTimingAffricateInClusterScale",
                "clusterTimingEnabled",
                "clusterTimingFricBeforeFricScale",
                "clusterTimingFricBeforeStopScale",
                "clusterTimingStopBeforeFricScale",
                "clusterTimingStopBeforeStopScale",
                "clusterTimingTripleClusterMiddleScale",
                "clusterTimingWordFinalObstruentScale",
                "clusterTimingWordMedialConsonantScale",
                # Coarticulation — missing flat key
                "coarticulationCrossSyllableScale",
                # Defaults (voice/output)
                "defaultGlottalOpenQuotient",
                "defaultOutputGain",
                "defaultPreFormantGain",
                "defaultVibratoPitchOffset",
                "defaultVibratoSpeed",
                "defaultVoiceTurbulenceAmplitude",
                # Diphthong auto-tie
                "autoDiphthongOffglideToSemivowel",
                "autoTieDiphthongs",
                # Fujisaki — missing
                "fujisakiDeclinationPostFloor",
                # Hungarian / language-specific
                "applyLengthenedScaleToVowelsOnly",
                "huShortAVowelEnabled",
                "huShortAVowelKey",
                "huShortAVowelScale",
                "lengthenedScaleHu",
                # Length contrast — flat keys
                "lengthContrastEnabled",
                "lengthContrastGeminateClosureScale",
                "lengthContrastGeminateReleaseScale",
                "lengthContrastLongVowelFloorMs",
                "lengthContrastPreGeminateVowelScale",
                "lengthContrastShortVowelCeilingMs",
                # Liquid dynamics — flat keys
                "liquidDynamicsEnabled",
                "liquidDynamicsLabialGlideStartF1",
                "liquidDynamicsLabialGlideStartF2",
                "liquidDynamicsLabialGlideTransitionEnabled",
                "liquidDynamicsLabialGlideTransitionPct",
                "liquidDynamicsLateralOnglideDurationPct",
                "liquidDynamicsLateralOnglideF1Delta",
                "liquidDynamicsLateralOnglideF2Delta",
                "liquidDynamicsRhoticF3DipDurationPct",
                "liquidDynamicsRhoticF3DipEnabled",
                "liquidDynamicsRhoticF3Minimum",
                # Positional allophones
                "positionalAllophones.enabled",
                # Special coarticulation — flat keys
                "specialCoarticMaxDeltaHz",
                "specialCoarticulationEnabled",
                # Stop closure — word boundary
                "stopClosureClusterGapsEnabled",
                "stopClosureWordBoundaryClusterFadeMs",
                "stopClosureWordBoundaryClusterGapMs",
                # Syllable duration (nested + flat)
                "syllableDuration.codaScale",
                "syllableDuration.enabled",
                "syllableDuration.onsetScale",
                "syllableDuration.unstressedOpenNucleusScale",
                "syllableDurationCodaScale",
                "syllableDurationEnabled",
                "syllableDurationOnsetScale",
                "syllableDurationUnstressedOpenNucleusScale",
                # Tonal / text processing
                "stripHyphen",
                "tonal",
                "toneContoursAbsolute",
                "toneContoursMode",
                "toneDigitsEnabled",
                # Trajectory limit — flat keys
                "trajectoryLimitApplyAcrossWordBoundary",
                "trajectoryLimitApplyTo",
                "trajectoryLimitEnabled",
                "trajectoryLimitLiquidRateScale",
                "trajectoryLimitMaxHzPerMsCf2",
                "trajectoryLimitMaxHzPerMsCf3",
                "trajectoryLimitMaxHzPerMsPf2",
                "trajectoryLimitMaxHzPerMsPf3",
                "trajectoryLimitWindowMs",
                # Voice profile
                "voiceProfileName",
            ]
            for k in _extraKeys:
                if k not in self._knownKeys:
                    self._knownKeys.append(k)
            self._knownKeys.sort(key=str.lower)
            if not self._knownKeys:
                # Last-resort fallback for broken/missing default.yaml.
                self._knownKeys = sorted([
                    "boundarySmoothing.enabled",
                    "coarticulationEnabled",
                    "coarticulationStrength",
                    "fujisakiDeclinationMax",
                    "fujisakiDeclinationScale",
                    "fujisakiPhraseAmp",
                    "fujisakiPrimaryAccentAmp",
                    "legacyPitchInflectionScale",
                    "legacyPitchMode",
                    "lengthContrast.enabled",
                    "liquidDynamics.enabled",
                    "microprosodyEnabled",
                    "microprosodyFollowingF0Enabled",
                    "microprosodyIntrinsicF0Enabled",
                    "microprosodyPreVoicelessShortenEnabled",
                    "nasalizationAnticipatoryEnabled",
                    "phraseFinalLengtheningEnabled",
                    "positionalAllophones.enabled",
                    "positionalAllophones.glottalReinforcement.enabled",
                    "postStopAspirationEnabled",
                    "primaryStressDiv",
                    "rateCompEnabled",
                    "secondaryStressDiv",
                    "semivowelOffglideScale",
                    "stopClosureMode",
                    "stopClosureVowelFadeMs",
                    "stopClosureVowelGapMs",
                    "stripAllophoneDigits",
                    "trajectoryLimit.enabled",
                ], key=str.lower)

            # Build display names for the combo box (localized, human-readable).
            # Raw YAML keys are preserved in self._knownKeys for all YAML operations.
            self._knownKeyDisplayNames = [_displayNameForKey(k) for k in self._knownKeys]
            self.settingKeyCtrl = self._addLabeledControlCompat(
                sHelper,
                _("Setting:"),
                wx.ComboBox,
                choices=self._knownKeyDisplayNames,
                style=wx.CB_DROPDOWN | wx.CB_READONLY,
            )
            self.settingKeyCtrl.Bind(wx.EVT_COMBOBOX, self._onSettingKeyChanged)

            self.valueCtrl = self._addLabeledControlCompat(sHelper, _("Value:"), wx.TextCtrl)
            self.valueCtrl.Bind(wx.EVT_TEXT, self._onGenericValueChanged)

            self.sourceLabel = sHelper.addItem(wx.StaticText(self, label=""))

            # GitHub link
            self.gitHubButton = sHelper.addItem(wx.Button(self, label=_("Open TGSpeechBox on GitHub")))
            self.gitHubButton.Bind(wx.EVT_BUTTON, self._onGitHubClick)

            # Check for updates button
            self.updateButton = sHelper.addItem(wx.Button(self, label=_("Check for Add-on Updates")))
            self.updateButton.Bind(wx.EVT_BUTTON, self._onUpdateClick)

            # Initialize defaults.
            self._setInitialLanguageTag()
            self._refreshAllDisplays()
            if self._knownKeys:
                self._setCurrentKey(self._knownKeys[0])

        def onSave(self):
            # Apply all pending changes.
            if not self._pending:
                return

            for langTag, keyMap in list(self._pending.items()):
                for key, val in list((keyMap or {}).items()):
                    if val is None:
                        continue
                    langPackYaml.upsertSetting(self._packsDir, langTag, key, val)

            # Best-effort live reload if TGSpeechBox is the active synth.
            try:
                import synthDriverHandler

                synth = synthDriverHandler.getSynth()
                if synth and synth.__class__.__module__.endswith("tgSpeechBox"):
                    if hasattr(synth, "reloadLanguagePack"):
                        synth.reloadLanguagePack()
            except Exception:
                pass

            self._pending.clear()

        def onDiscard(self):
            # User hit Cancel.
            self._pending.clear()

        # ----- UI helpers -----

        def _onGitHubClick(self, evt):
            """Open the GitHub page in the default browser."""
            try:
                import wx
                wx.LaunchDefaultBrowser(GitHub_URL)
            except Exception:
                try:
                    import webbrowser
                    webbrowser.open(GitHub_URL)
                except Exception:
                    pass
            evt.Skip()

        def _onUpdateClick(self, evt):
            """Check for updates and download the latest add-on if available."""
            try:
                import wx
            except Exception:
                evt.Skip()
                return

            import tempfile
            import urllib.request
            import urllib.error

            # First, check if an update is available
            wx.BeginBusyCursor()
            remoteVersion = None
            versionCheckError = None

            try:
                req = urllib.request.Request(
                    ADDON_VERSION_URL,
                    headers={"User-Agent": "NVDA-TGSpeechBox-Updater/1.0"},
                )
                with urllib.request.urlopen(req, timeout=15) as response:
                    remoteVersion = response.read().decode("utf-8").strip()
            except urllib.error.URLError as e:
                versionCheckError = str(e.reason) if hasattr(e, 'reason') else str(e)
            except Exception as e:
                versionCheckError = str(e)
            finally:
                wx.EndBusyCursor()

            if versionCheckError:
                # Couldn't check version - ask user if they want to download anyway
                dlg = wx.MessageDialog(
                    self,
                    _(
                        "Could not check for updates:\n{}\n\n"
                        "Would you like to download the add-on anyway?"
                    ).format(versionCheckError),
                    _("Version Check Failed"),
                    wx.YES_NO | wx.NO_DEFAULT | wx.ICON_WARNING,
                )
                result = dlg.ShowModal()
                dlg.Destroy()
                if result != wx.ID_YES:
                    evt.Skip()
                    return
            else:
                # Compare versions
                installedVersion = _getInstalledAddonVersion()

                # Check if remote version is actually newer
                if installedVersion and remoteVersion and not _isNewerVersion(remoteVersion, installedVersion):
                    wx.MessageBox(
                        _(
                            "You already have the latest version installed.\n\n"
                            "Installed version: {}\n"
                            "Latest version: {}"
                        ).format(installedVersion, remoteVersion),
                        _("No Update Available"),
                        wx.OK | wx.ICON_INFORMATION,
                        self,
                    )
                    evt.Skip()
                    return

                # Update available - confirm with user
                if installedVersion:
                    msg = _(
                        "A new version is available!\n\n"
                        "Installed version: {}\n"
                        "Latest version: {}\n\n"
                        "Download and install the update?"
                    ).format(installedVersion, remoteVersion)
                else:
                    msg = _(
                        "Latest version available: {}\n\n"
                        "Download and install?"
                    ).format(remoteVersion)

                dlg = wx.MessageDialog(
                    self,
                    msg,
                    _("Update Available"),
                    wx.YES_NO | wx.YES_DEFAULT | wx.ICON_QUESTION,
                )
                result = dlg.ShowModal()
                dlg.Destroy()

                if result != wx.ID_YES:
                    evt.Skip()
                    return

            # Show a simple busy cursor since download may take a moment
            wx.BeginBusyCursor()

            downloadPath = None
            errorMsg = None

            try:
                # Download to temp directory
                tempDir = tempfile.gettempdir()
                # Extract filename from URL
                addonFilename = ADDON_UPDATE_URL.rsplit("/", 1)[-1]
                if not addonFilename.endswith(".nvda-addon"):
                    addonFilename = "TGSpeechBox-update.nvda-addon"
                downloadPath = os.path.join(tempDir, addonFilename)

                # Clean up any old downloaded addon file from previous attempts
                try:
                    if os.path.isfile(downloadPath):
                        os.remove(downloadPath)
                except Exception:
                    pass  # Not critical if cleanup fails

                # Download the file
                req = urllib.request.Request(
                    ADDON_UPDATE_URL,
                    headers={"User-Agent": "NVDA-TGSpeechBox-Updater/1.0"},
                )
                with urllib.request.urlopen(req, timeout=60) as response:
                    data = response.read()

                # Write to temp file
                with open(downloadPath, "wb") as f:
                    f.write(data)

            except urllib.error.URLError as e:
                errorMsg = _("Failed to download add-on:\n{}").format(str(e.reason) if hasattr(e, 'reason') else str(e))
            except Exception as e:
                errorMsg = _("An error occurred during download:\n{}").format(str(e))
            finally:
                wx.EndBusyCursor()

            if errorMsg:
                wx.MessageBox(
                    errorMsg,
                    _("Download Error"),
                    wx.OK | wx.ICON_ERROR,
                    self,
                )
                evt.Skip()
                return

            # Launch the downloaded file with the default handler (NVDA's addon installer)
            # Note: os.startfile returns immediately; we can't know if user clicks "No" in NVDA's
            # installer dialog. The temp file will be cleaned up on next download attempt.
            try:
                # os.startfile is Windows-only but NVDA is Windows-only anyway
                os.startfile(downloadPath)
            except Exception as e:
                # If startfile fails, show the path so user can install manually
                wx.MessageBox(
                    _(
                        "Download complete, but failed to launch installer:\n{}\n\n"
                        "File saved to:\n{}\n\n"
                        "Please install it manually by opening the file."
                    ).format(str(e), downloadPath),
                    _("Warning"),
                    wx.OK | wx.ICON_WARNING,
                    self,
                )

            evt.Skip()

        def _onResetDefaultsClick(self, evt):
            """Reset language packs to factory defaults by copying from .defaults folder."""
            try:
                import wx
            except Exception:
                evt.Skip()
                return

            # Get the currently selected language tag
            currentLangTag = langPackYaml.normalizeLangTag(self.langTagCtrl.GetValue())

            # Create a custom dialog with a checkbox
            dlg = wx.Dialog(self, title=_("Reset to Factory Defaults"), style=wx.DEFAULT_DIALOG_STYLE)

            sizer = wx.BoxSizer(wx.VERTICAL)

            # Warning message
            msgText = wx.StaticText(
                dlg,
                label=_(
                    "This will overwrite language pack YAML files in packs/lang/ "
                    "with the factory default versions from packs/.defaults/.\n\n"
                    "Any customizations you have made will be lost."
                ),
            )
            sizer.Add(msgText, 0, wx.ALL | wx.EXPAND, 10)

            # Checkbox for single language pack reset
            resetSingleCheckbox = wx.CheckBox(
                dlg,
                label=_("Reset only the currently selected language pack ({})").format(currentLangTag),
            )
            resetSingleCheckbox.SetValue(True)  # Default to single pack (safer)
            sizer.Add(resetSingleCheckbox, 0, wx.LEFT | wx.RIGHT | wx.BOTTOM, 10)

            # Create buttons manually for better control
            btnSizer = wx.BoxSizer(wx.HORIZONTAL)
            yesBtn = wx.Button(dlg, wx.ID_YES, _("Yes"))
            noBtn = wx.Button(dlg, wx.ID_NO, _("No"))
            btnSizer.Add(yesBtn, 0, wx.RIGHT, 5)
            btnSizer.Add(noBtn, 0)
            sizer.Add(btnSizer, 0, wx.ALL | wx.ALIGN_CENTER, 10)

            # Set No as default button and handle escape
            noBtn.SetDefault()
            dlg.SetEscapeId(wx.ID_NO)
            dlg.SetAffirmativeId(wx.ID_YES)

            dlg.SetSizer(sizer)
            sizer.Fit(dlg)
            dlg.CenterOnParent()

            result = dlg.ShowModal()

            # Only proceed if user explicitly clicked Yes
            if result != wx.ID_YES:
                dlg.Destroy()
                evt.Skip()
                return

            # Get checkbox value before destroying dialog
            resetSingleOnly = resetSingleCheckbox.GetValue()
            dlg.Destroy()

            # Perform the reset
            defaultsDir = os.path.join(self._packsDir, ".defaults")
            langDir = langPackYaml.getLangDir(self._packsDir)

            if not os.path.isdir(defaultsDir):
                wx.MessageBox(
                    _(
                        "Factory defaults folder not found:\n{}\n\n"
                        "Cannot reset language packs."
                    ).format(defaultsDir),
                    _("Error"),
                    wx.OK | wx.ICON_ERROR,
                    self,
                )
                evt.Skip()
                return

            # Ensure lang directory exists
            try:
                os.makedirs(langDir, exist_ok=True)
            except Exception as e:
                wx.MessageBox(
                    _("Failed to create language pack directory:\n{}").format(str(e)),
                    _("Error"),
                    wx.OK | wx.ICON_ERROR,
                    self,
                )
                evt.Skip()
                return

            # Copy *.yaml files from .defaults to lang
            copiedCount = 0
            errors = []
            try:
                for fn in os.listdir(defaultsDir):
                    if not fn.lower().endswith(".yaml"):
                        continue

                    # If resetting single language only, check if this file matches
                    if resetSingleOnly:
                        fileTag = os.path.splitext(fn)[0].lower()
                        if fileTag != currentLangTag:
                            continue

                    srcPath = os.path.join(defaultsDir, fn)
                    dstPath = os.path.join(langDir, fn)
                    if not os.path.isfile(srcPath):
                        continue
                    try:
                        # Read and write to avoid shutil dependency issues
                        with open(srcPath, "r", encoding="utf-8") as f:
                            content = f.read()
                        with open(dstPath, "w", encoding="utf-8", newline="\n") as f:
                            f.write(content)
                        copiedCount += 1
                    except Exception as e:
                        errors.append(f"{fn}: {e}")
            except Exception as e:
                wx.MessageBox(
                    _("Failed to read defaults directory:\n{}").format(str(e)),
                    _("Error"),
                    wx.OK | wx.ICON_ERROR,
                    self,
                )
                evt.Skip()
                return

            if copiedCount == 0 and resetSingleOnly:
                wx.MessageBox(
                    _(
                        "No factory default found for language pack '{}'.\n\n"
                        "The file '{}.yaml' does not exist in the .defaults folder."
                    ).format(currentLangTag, currentLangTag),
                    _("Not Found"),
                    wx.OK | wx.ICON_WARNING,
                    self,
                )
                evt.Skip()
                return

            if errors:
                wx.MessageBox(
                    _(
                        "Reset completed with errors.\n\n"
                        "Copied {} file(s).\n\n"
                        "Errors:\n{}"
                    ).format(copiedCount, "\n".join(errors)),
                    _("Warning"),
                    wx.OK | wx.ICON_WARNING,
                    self,
                )
            else:
                if resetSingleOnly:
                    wx.MessageBox(
                        _("Successfully reset '{}' language pack to factory defaults.").format(currentLangTag),
                        _("Reset Complete"),
                        wx.OK | wx.ICON_INFORMATION,
                        self,
                    )
                else:
                    wx.MessageBox(
                        _("Successfully reset {} language pack file(s) to factory defaults.").format(copiedCount),
                        _("Reset Complete"),
                        wx.OK | wx.ICON_INFORMATION,
                        self,
                    )

            # Clear pending edits (they're now stale)
            if resetSingleOnly:
                # Only clear pending edits for the reset language
                if currentLangTag in self._pending:
                    del self._pending[currentLangTag]
            else:
                self._pending.clear()

            # Refresh the language choices in case new files appeared
            try:
                currentTag = self.langTagCtrl.GetValue()
                self.langTagCtrl.Clear()
                for choice in self._getLanguageChoices():
                    self.langTagCtrl.Append(choice)
                self.langTagCtrl.SetValue(currentTag)
            except Exception:
                pass

            # Refresh all displays to show the reset values
            self._refreshAllDisplays()

            # Trigger live reload if TGSpeechBox is active
            try:
                import synthDriverHandler

                synth = synthDriverHandler.getSynth()
                if synth and synth.__class__.__module__.endswith("tgSpeechBox"):
                    if hasattr(synth, "reloadLanguagePack"):
                        synth.reloadLanguagePack()
            except Exception:
                pass

            evt.Skip()

        def _onSaveVoiceProfileClick(self, evt):
            """Save current voice profile slider values to phonemes.yaml via frontend API."""
            try:
                import wx
            except Exception:
                evt.Skip()
                return

            # Get the current synth
            try:
                import synthDriverHandler
                synth = synthDriverHandler.getSynth()
                if not synth or not synth.__class__.__module__.endswith("tgSpeechBox"):
                    wx.MessageBox(
                        _("TGSpeechBox must be the active synthesizer to save voice profile settings."),
                        _("Cannot Save"),
                        wx.OK | wx.ICON_WARNING,
                        self,
                    )
                    evt.Skip()
                    return
            except Exception as e:
                wx.MessageBox(
                    _("Error accessing synthesizer: {}").format(str(e)),
                    _("Error"),
                    wx.OK | wx.ICON_ERROR,
                    self,
                )
                evt.Skip()
                return

            # Check if frontend supports saving
            frontend = getattr(synth, "_frontend", None)
            if not frontend or not frontend.hasFrameExSupport():
                wx.MessageBox(
                    _("This feature requires a newer version of the frontend DLL."),
                    _("Not Supported"),
                    wx.OK | wx.ICON_WARNING,
                    self,
                )
                evt.Skip()
                return

            # Get current voice - works for both profiles AND built-in voices
            curVoice = getattr(synth, "_curVoice", "") or "Adam"
            VOICE_PROFILE_PREFIX = "profile:"
            if curVoice.startswith(VOICE_PROFILE_PREFIX):
                profileName = curVoice[len(VOICE_PROFILE_PREFIX):]
            else:
                profileName = curVoice  # Built-in voice like "Adam"

            # Get current slider values and convert to DSP values
            tiltSlider = getattr(synth, "_curVoiceTilt", 50)
            noiseModSlider = getattr(synth, "_curNoiseGlottalMod", 0)
            f1Slider = getattr(synth, "_curPitchSyncF1", 50)
            b1Slider = getattr(synth, "_curPitchSyncB1", 50)
            sqSlider = getattr(synth, "_curSpeedQuotient", 50)
            aspTiltSlider = getattr(synth, "_curAspirationTilt", 50)
            bwSlider = getattr(synth, "_curCascadeBwScale", 50)
            creakSlider = getattr(synth, "_curFrameExCreakiness", 0)
            breathSlider = getattr(synth, "_curFrameExBreathiness", 0)
            jitterSlider = getattr(synth, "_curFrameExJitter", 0)
            shimmerSlider = getattr(synth, "_curFrameExShimmer", 0)
            sharpnessSlider = getattr(synth, "_curFrameExSharpness", 50)

            # Convert VoicingTone sliders to DSP values
            tiltDbPerOct = (tiltSlider - 50.0) * (24.0 / 50.0)
            noiseModDepth = noiseModSlider / 100.0
            f1DeltaHz = (f1Slider - 50.0) * 1.2
            b1DeltaHz = (b1Slider - 50.0) * 1.0
            if sqSlider <= 50.0:
                speedQuotient = 0.5 + (sqSlider / 50.0) * 1.5
            else:
                speedQuotient = 2.0 + ((sqSlider - 50.0) / 50.0) * 2.0
            aspTiltDbPerOct = (aspTiltSlider - 50.0) * 0.24

            # Cascade bandwidth scale (0-100 -> 0.4-1.4, 50 = 1.0)
            if bwSlider <= 50.0:
                cascadeBwScale = 0.4 + (bwSlider / 50.0) * 0.6
            else:
                cascadeBwScale = 1.0 + ((bwSlider - 50.0) / 50.0) * 0.4
            cascadeBwScale = max(0.4, min(1.4, cascadeBwScale))

            # Convert FrameEx sliders to DSP values
            creakiness = creakSlider / 100.0
            breathiness = breathSlider / 100.0
            jitter = jitterSlider / 100.0
            shimmer = shimmerSlider / 100.0
            sharpness = 0.5 + (sharpnessSlider / 100.0) * 1.5  # 0-100 -> 0.5-2.0

            # Build confirmation message
            msg = _(
                "Save the following slider settings to profile '{}'?\n\n"
                "VoicingTone:\n"
                "  voicedTiltDbPerOct: {:.2f}\n"
                "  noiseGlottalModDepth: {:.2f}\n"
                "  pitchSyncF1DeltaHz: {:.1f}\n"
                "  pitchSyncB1DeltaHz: {:.1f}\n"
                "  speedQuotient: {:.2f}\n"
                "  aspirationTiltDbPerOct: {:.2f}\n\n"
                "  cascadeBwScale: {:.2f}\n\n"
                "FrameEx:\n"
                "  creakiness: {:.2f}\n"
                "  breathiness: {:.2f}\n"
                "  jitter: {:.2f}\n"
                "  shimmer: {:.2f}\n"
                "  sharpness: {:.2f}\n\n"
                "This will update packs/phonemes.yaml."
            ).format(
                profileName,
                tiltDbPerOct, noiseModDepth, f1DeltaHz, b1DeltaHz, speedQuotient, aspTiltDbPerOct,
                cascadeBwScale,
                creakiness, breathiness, jitter, shimmer, sharpness,
            )

            result = wx.MessageBox(
                msg,
                _("Save Voice Profile Settings"),
                wx.YES_NO | wx.ICON_QUESTION,
                self,
            )

            if result != wx.YES:
                evt.Skip()
                return

            # Call frontend API to save
            try:
                success = frontend.saveVoiceProfileSliders(
                    profileName=profileName,
                    voicedTiltDbPerOct=tiltDbPerOct,
                    noiseGlottalModDepth=noiseModDepth,
                    pitchSyncF1DeltaHz=f1DeltaHz,
                    pitchSyncB1DeltaHz=b1DeltaHz,
                    speedQuotient=speedQuotient,
                    aspirationTiltDbPerOct=aspTiltDbPerOct,
                    cascadeBwScale=cascadeBwScale,
                    creakiness=creakiness,
                    breathiness=breathiness,
                    jitter=jitter,
                    shimmer=shimmer,
                    sharpness=sharpness,
                )

                if success:
                    wx.MessageBox(
                        _("Successfully saved slider settings for profile '{}'.\n\n"
                          "The frontend will reload the settings on next speech.").format(profileName),
                        _("Save Complete"),
                        wx.OK | wx.ICON_INFORMATION,
                        self,
                    )

                    # Trigger frontend reload and re-apply voice profile
                    try:
                        langTag = getattr(synth, "_language", "en-us")
                        frontend.setLanguage(langTag)
                        if curVoice.startswith(VOICE_PROFILE_PREFIX):
                            frontend.setVoiceProfile(profileName)
                            synth._applyVoicingTone(profileName)
                    except Exception:
                        pass
                else:
                    errMsg = frontend.getLastError() or "Unknown error"
                    wx.MessageBox(
                        _("Failed to save settings: {}").format(errMsg),
                        _("Error"),
                        wx.OK | wx.ICON_ERROR,
                        self,
                    )

            except Exception as e:
                wx.MessageBox(
                    _("Error saving to phonemes.yaml: {}").format(str(e)),
                    _("Error"),
                    wx.OK | wx.ICON_ERROR,
                    self,
                )

            evt.Skip()

        def _getLanguageChoices(self):
            # List files in packs/lang as suggestions.
            langDir = langPackYaml.getLangDir(self._packsDir)
            choices = []
            try:
                for fn in os.listdir(langDir):
                    if not fn.lower().endswith(".yaml"):
                        continue
                    tag = os.path.splitext(fn)[0]
                    choices.append(tag)
            except Exception:
                pass

            # Ensure default is always present.
            if "default" not in choices:
                choices.append("default")
            return sorted(set(choices))

        def _setInitialLanguageTag(self):
            # Intentionally do *not* auto-detect the language in use.
            # Users may open this panel while a different synth is selected.
            self.langTagCtrl.SetValue("default")

        def _onLangTagChanged(self, evt):
            self._refreshAllDisplays()
            evt.Skip()

        def _onSettingKeyChanged(self, evt):
            displayName = self.settingKeyCtrl.GetValue()
            key = _keyForDisplayName(displayName, self._knownKeys)
            self._setCurrentKey(key)
            evt.Skip()

        def _onGenericValueChanged(self, evt):
            if self._isPopulating:
                evt.Skip()
                return
            if not self._currentKey:
                evt.Skip()
                return
            langTag = langPackYaml.normalizeLangTag(self.langTagCtrl.GetValue())
            self._pending.setdefault(langTag, {})[self._currentKey] = self.valueCtrl.GetValue()
            evt.Skip()

        def _setCurrentKey(self, key: str):
            key = (key or "").strip()
            if not key:
                return
            self._currentKey = key
            try:
                self.settingKeyCtrl.SetValue(_displayNameForKey(key))
            except Exception:
                pass
            self._updateGenericDisplay()

        def _refreshAllDisplays(self):
            self._updateGenericDisplay()

        def _updateGenericDisplay(self):
            if not self._currentKey:
                return

            langTag = langPackYaml.normalizeLangTag(self.langTagCtrl.GetValue())
            key = self._currentKey
            pendingForLang = self._pending.get(langTag, {})

            # Prefer pending edits.
            if key in pendingForLang:
                value = pendingForLang[key]
                source = _("(pending edit)")
            else:
                value = langPackYaml.getEffectiveSettingValue(self._packsDir, langTag, key)
                sourceTag = langPackYaml.getSettingSource(self._packsDir, langTag, key)
                source = _("(from {}.yaml)").format(sourceTag) if sourceTag else ""
                if value is None:
                    value = ""

            self._isPopulating = True
            try:
                try:
                    self.valueCtrl.ChangeValue(str(value))
                except Exception:
                    self.valueCtrl.SetValue(str(value))

                try:
                    self.sourceLabel.SetLabel(source)
                except Exception:
                    pass
            finally:
                self._isPopulating = False

    _PANEL_CLS = TGSpeechBoxLanguagePacksPanel
    return _PANEL_CLS


def registerSettingsPanel() -> None:
    """Register the panel with NVDA's Settings dialog (best effort)."""
    panelCls = _getPanelClass()
    if panelCls is None:
        return

    # Import settingsDialogs lazily (NVDA can import synth drivers early).
    try:
        from gui import settingsDialogs
    except Exception:
        return

    # Resolve the dialog class across NVDA versions.
    dlgCls = None
    for name in ("NVDASettingsDialog", "SettingsDialog"):
        dlgCls = getattr(settingsDialogs, name, None)
        if dlgCls:
            break
    if dlgCls is None:
        return

    # Avoid duplicate registration.
    try:
        cats = getattr(dlgCls, "categoryClasses", None)
        if isinstance(cats, (list, tuple)) and panelCls in cats:
            return
    except Exception:
        pass

    # Newer NVDA builds: registerCategory().
    try:
        fn = getattr(dlgCls, "registerCategory", None)
        if callable(fn):
            fn(panelCls)
            return
    except Exception:
        pass

    # Older NVDA builds: categoryClasses list.
    try:
        if hasattr(dlgCls, "categoryClasses"):
            dlgCls.categoryClasses.append(panelCls)
            return
    except Exception:
        pass

    # Last resort: some builds expose module-level helpers.
    for name in ("registerCategory", "registerSettingsPanel"):
        try:
            fn = getattr(settingsDialogs, name, None)
            if callable(fn):
                fn(panelCls)
                return
        except Exception:
            continue
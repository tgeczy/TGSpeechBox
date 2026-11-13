"""NVDA Settings panel for editing TGSpeechBox language-pack YAML settings.

This provides the UX described in readme.md:
  - Choose a language tag
  - Choose a setting key from an alphabetized combo box and edit its value
  - Apply the pending edits when the user presses OK in NVDA's Settings dialog

The panel intentionally avoids a full YAML UI; it only edits ``settings:`` keys.

Implementation notes:
  - NVDA can import synth drivers before the GUI is fully initialized.
    Importing wx/gui modules at *module import time* can therefore fail.
  - NVDA can import synth drivers before the GUI is initialized, so all GUI
    imports and the SettingsPanel subclass definition are done lazily when
    registerSettingsPanel is called (typically from SynthDriver.__init__).
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
from . import updates

GitHub_URL = "https://github.com/tgeczy/TGSpeechBox"

# The add-on's own section of NVDA's configuration, for what is not a voice
# setting: which releases the update check looks at (#103).
_CONF_SECTION = "tgSpeechBox"
_CONF_SPEC = {"updateChannel": "string(default='stable')"}


def _addonConf():
    """The add-on's section of NVDA's configuration.  NVDA is told the
    section's settings first, as it must be before one is read or written."""
    import config
    if _CONF_SECTION not in config.conf.spec:
        config.conf.spec[_CONF_SECTION] = dict(_CONF_SPEC)
    return config.conf[_CONF_SECTION]


def _getUpdateChannel() -> str:
    """The update channel NVDA's configuration holds: one of updates.CHANNELS,
    "stable" for anything else and when the configuration cannot be read."""
    try:
        channel = _addonConf()["updateChannel"]
    except Exception:
        channel = None
    return channel if channel in updates.CHANNELS else "stable"


def _setUpdateChannel(channel: str) -> None:
    try:
        _addonConf()["updateChannel"] = channel if channel in updates.CHANNELS else "stable"
    except Exception:
        pass


def _runInBackground(work, done) -> None:
    """Run work() off the GUI thread, then done(result, error) on it.

    The update check and the download wait for the network; done on the GUI
    thread they froze NVDA's settings dialog, and the screen reader's own
    interface with it, for as long as the server took.
    """
    import threading
    import wx

    def run():
        try:
            result, error = work(), None
        except Exception as e:
            result, error = None, e
        wx.CallAfter(done, result, error)

    threading.Thread(target=run, name="TGSpeechBoxUpdateCheck", daemon=True).start()


def _profileSaveReason(e) -> str:
    """Why phonemes.yaml was not written, in the listener's language.

    profile_save.py names its refusals by code (it has no _()).  Anything
    else that goes wrong, a full disk say, is shown as the system words it.
    """
    from . import profile_save
    if not isinstance(e, profile_save.ProfileSaveError):
        return str(e)
    sentences = {
        "toneUnreadable": _("The profile's voicingTone line cannot be read: {}"),
        "toneNotEditable": _("The profile's voicingTone cannot be edited as it is written: {}"),
        "profilesNotABlock": _(
            "The voiceProfiles section is not written as a block. Edit it in the phoneme editor."
        ),
        "profileOnOneLine": _("Profile '{}' is written on one line. Edit it in the phoneme editor."),
        "badName": _("A profile cannot be named '{}'."),
    }
    sentence = sentences.get(e.code)
    return sentence.format(e.detail) if sentence else str(e)


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
        title = _("TGSpeechBox")

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
            self.langTagCtrl = sHelper.addLabeledControl(
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

            # --- Save As (export copy to user-chosen location) ---
            self.exportPhonemeFileButton = sHelper.addItem(
                wx.Button(self, label=_("Save Voice Profile Sliders to YAML As..."))
            )
            self.exportPhonemeFileButton.Bind(wx.EVT_BUTTON, self._onExportPhonemeFileClick)

            # --- Import Voice Profile Sliders button ---
            self.importPhonemeFileButton = sHelper.addItem(
                wx.Button(self, label=_("Import Voice Profile Sliders..."))
            )
            self.importPhonemeFileButton.Bind(wx.EVT_BUTTON, self._onImportPhonemeFileClick)

            # --- User Dictionary Export/Import ---
            self.exportUserDictButton = sHelper.addItem(
                wx.Button(self, label=_("Export User Dictionary..."))
            )
            self.exportUserDictButton.Bind(wx.EVT_BUTTON, self._onExportUserDictClick)

            self.importUserDictButton = sHelper.addItem(
                wx.Button(self, label=_("Import User Dictionary..."))
            )
            self.importUserDictButton.Bind(wx.EVT_BUTTON, self._onImportUserDictClick)

            # --- Setting key/value editor ---
            sHelper.addItem(wx.StaticText(self, label=_("Edit setting:")))

            self._knownKeys = langPackYaml.listKnownSettingKeys(self._packsDir) or []
            # Ensure all pack.h/pack.cpp setting keys are available in the
            # combo box, even if default.yaml hasn't been updated yet.
            # Source of truth: LangPack struct in pack.h + nested block
            # parsing in pack.cpp. Alphabetized for the combo box.
            _extraKeys = [
                # ============================================================
                # Nested block settings (dot notation).
                # These are the preferred format -- the phoneme editor and NVDA
                # driver both write nested YAML blocks for these settings.
                # ============================================================

                # --- Allophone rules ---
                "allophoneRules.enabled",

                # --- Boundary smoothing ---
                "boundarySmoothing.enabled",
                "boundarySmoothing.f1Scale",
                "boundarySmoothing.f2Scale",
                "boundarySmoothing.f3Scale",
                "boundarySmoothing.fricToStopFadeMs",
                "boundarySmoothing.fricToVowelFadeMs",
                "boundarySmoothing.liquidToStopFadeMs",
                "boundarySmoothing.liquidToVowelFadeMs",
                "boundarySmoothing.nasalF1Instant",
                "boundarySmoothing.nasalToStopFadeMs",
                "boundarySmoothing.nasalToVowelFadeMs",
                "boundarySmoothing.stopToFricFadeMs",
                "boundarySmoothing.stopToVowelFadeMs",
                "boundarySmoothing.vowelToFricFadeMs",
                "boundarySmoothing.vowelToLiquidFadeMs",
                "boundarySmoothing.vowelToNasalFadeMs",
                "boundarySmoothing.vowelToStopFadeMs",
                "boundarySmoothing.vowelToVowelFadeMs",
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
                "boundarySmoothing.highRateFadeRatioFloor",
                "boundarySmoothing.affricateToVowelHold",
                "boundarySmoothing.fricativeToVowelHold",
                "boundarySmoothing.stopToVowelHold",
                "boundarySmoothing.affricateToVowelVoicingHold",
                "boundarySmoothing.stopToVowelVoicingHold",

                # --- Cluster blend ---
                "clusterBlend.defaultPairScale",
                "clusterBlend.enabled",
                "clusterBlend.f1Scale",
                "clusterBlend.forwardDriftStrength",
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

                # --- Cluster timing ---
                "clusterTiming.affricateInClusterScale",
                "clusterTiming.enabled",
                "clusterTiming.fricBeforeFricScale",
                "clusterTiming.fricBeforeStopScale",
                "clusterTiming.stopBeforeFricScale",
                "clusterTiming.stopBeforeStopScale",
                "clusterTiming.tripleClusterMiddleScale",
                "clusterTiming.wordFinalObstruentScale",
                "clusterTiming.wordMedialConsonantScale",

                # --- Diphthong collapse ---
                "diphthongCollapse.amplitudeDipFactor",
                "diphthongCollapse.durationScale",
                "diphthongCollapse.durationFloorMs",
                "diphthongCollapse.rateCompensation",
                "diphthongCollapse.enabled",
                "diphthongCollapse.microFrameIntervalMs",
                "diphthongCollapse.onsetHoldExponent",
                "diphthongCollapse.onsetSettleMs",

                # --- Length contrast ---
                "lengthContrast.enabled",
                "lengthContrast.geminateClosureScale",
                "lengthContrast.geminateReleaseScale",
                "lengthContrast.longVowelFloor",
                "lengthContrast.preGeminateVowelScale",
                "lengthContrast.shortVowelCeiling",

                # --- Liquid dynamics ---
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

                # --- Positional allophones ---
                "positionalAllophones.enabled",

                # --- Prominence ---
                "prominence.amplitudeBoostDb",
                "prominence.amplitudeReductionDb",
                "prominence.durationPrimaryFloorMs",
                "prominence.durationProminentFloorMs",
                "prominence.durationReducedCeiling",
                "prominence.enabled",
                "prominence.fullVowelFloor",
                "prominence.monosyllableFloor",
                "prominence.longVowelMode",
                "prominence.longVowelWeight",
                "prominence.pitchFromProminence",
                "prominence.primaryStressWeight",
                "prominence.secondaryStressLevel",
                "prominence.secondaryStressWeight",
                "prominence.wordFinalReduction",
                "prominence.wordInitialBoost",

                # --- Rate compensation ---
                "rateCompensation.clusterMaxRatioShift",
                "rateCompensation.clusterProportionGuard",
                "rateCompensation.enabled",
                "rateCompensation.floorSpeedScale",
                "rateCompensation.minimumDurations.affricateMs",
                "rateCompensation.minimumDurations.diphthongMs",
                "rateCompensation.minimumDurations.fricativeMs",
                "rateCompensation.minimumDurations.liquidMs",
                "rateCompensation.minimumDurations.nasalMs",
                "rateCompensation.minimumDurations.semivowelMs",
                "rateCompensation.minimumDurations.stopMs",
                "rateCompensation.minimumDurations.tapMs",
                "rateCompensation.minimumDurations.trillMs",
                "rateCompensation.minimumDurations.voicedConsonantMs",
                "rateCompensation.minimumDurations.vowelMs",
                "rateCompensation.schwaReduction.enabled",
                "rateCompensation.schwaReduction.scale",
                "rateCompensation.schwaReduction.threshold",
                "rateCompensation.sonorantContextBonusMs",
                "rateCompensation.wordFinalBonusMs",

                # --- Special coarticulation ---
                "specialCoarticulation.enabled",
                "specialCoarticulation.maxDeltaHz",

                # --- Syllable duration ---
                "syllableDuration.codaScale",
                "syllableDuration.enabled",
                "syllableDuration.onsetScale",
                "syllableDuration.unstressedOpenNucleusScale",

                # --- Trajectory limit ---
                "trajectoryLimit.applyAcrossWordBoundary",
                "trajectoryLimit.applyTo",
                "trajectoryLimit.enabled",
                "trajectoryLimit.liquidRateScale",
                "trajectoryLimit.maxHzPerMs.cf1",
                "trajectoryLimit.maxHzPerMs.cf2",
                "trajectoryLimit.maxHzPerMs.cf3",
                "trajectoryLimit.maxHzPerMs.pf2",
                "trajectoryLimit.maxHzPerMs.pf3",
                "trajectoryLimit.windowMs",

                # ============================================================
                # Flat-only settings (no nested block equivalent).
                # These are written as direct keys under settings:.
                # ============================================================

                # --- Clause-final ---
                "clauseFinalHoldMs",
                "clauseFinalFadeMs",

                # --- Clause pauses ---
                "scalePauseToRate",

                # --- Coarticulation ---
                "coarticulationAdjacencyMaxConsonants",
                "coarticulationAlveolarBackVowelStrengthBoost",
                "coarticulationAlveolarF2Locus",
                "coarticulationAlveolarScale",
                "coarticulationAspirationBlendEnd",
                "coarticulationAspirationBlendStart",
                "coarticulationBackVowelF2Threshold",
                "coarticulationCrossSyllableScale",
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
                "coarticulationVelarF2LocusFront",
                "coarticulationVelarF2LocusBack",
                "coarticulationVelarPinchEnabled",
                "coarticulationVelarPinchF2Scale",
                "coarticulationVelarPinchF3",
                "coarticulationVelarPinchThreshold",
                "coarticulationVelarScale",

                # --- Coda noise taper (fricative→stop continuity) ---
                "codaNoiseTaperEnabled",
                "codaNoiseTaperPreGain",
                "codaNoiseTaperEarlyFricScale",
                "codaNoiseTaperEarlyAspAmp",
                "codaNoiseTaperLateFricScale",
                "codaNoiseTaperLateAspAmp",

                # --- Defaults ---
                "defaultFadeMs",
                "defaultGlottalOpenQuotient",
                "defaultOutputGain",
                "defaultPreFormantGain",
                "defaultVibratoPitchOffset",
                "defaultVibratoSpeed",
                "defaultVoiceTurbulenceAmplitude",
                "defaultVowelDurationMs",

                # --- Diphthong auto-tie ---
                "autoDiphthongOffglideToSemivowel",
                "autoTieDiphthongs",

                # --- English-specific ---
                "englishLongUKey",
                "englishLongUShortenEnabled",
                "englishLongUWordFinalScale",

                # --- Fujisaki pitch model ---
                "fujisakiAccentDur",
                "fujisakiAccentLen",
                "fujisakiAccentMode",
                "fujisakiDeclinationMax",
                "fujisakiDeclinationPostFloor",
                "fujisakiCompoundDeclinStep",
                "fujisakiDeclinationRate",
                "fujisakiDeclinationScale",
                "fujisakiMonoAccentDurScale",
                "fujisakiPhraseAmp",
                "fujisakiPhraseDecay",
                "fujisakiPhraseLen",
                "fujisakiPitchCurveExponent",
                "fujisakiPrimaryAccentAmp",
                "fujisakiSecondaryAccentAmp",

                # --- Fujisaki clause-type overrides ---
                "fujisakiQuestionPhraseAmpScale",
                "fujisakiQuestionAccentBoost",
                "fujisakiQuestionDeclinationScale",
                "fujisakiQuestionBasePitchScale",
                "fujisakiQuestionFinalRiseScale",
                "fujisakiQuestionFinalDropScale",
                "fujisakiExclamationPhraseAmpScale",
                "fujisakiExclamationAccentBoost",
                "fujisakiExclamationDeclinationScale",
                "fujisakiExclamationBasePitchScale",
                "fujisakiExclamationFinalRiseScale",
                "fujisakiExclamationFinalDropScale",
                "fujisakiCommaPhraseAmpScale",
                "fujisakiCommaAccentBoost",
                "fujisakiCommaDeclinationScale",
                "fujisakiCommaBasePitchScale",
                "fujisakiCommaFinalRiseScale",
                "fujisakiCommaFinalDropScale",

                # --- High-rate intelligibility ---
                "highRateBandwidthWideningFactor",
                "highRateCoarticulationFloor",
                "highRateThreshold",

                # --- Impulse pitch model ---
                "impulseAssertiveness",
                "impulseContinuationRiseHz",
                "impulseDeclinationHzPerSec",
                "impulseDeclinationRangeHz",
                "impulseFirstStressBoostHz",
                "impulseFourthStressBoostHz",
                "impulseHatFallScale",
                "impulseQuestionReduction",
                "impulseQuestionRiseHz",
                "impulseRiseHz",
                "impulseSecondStressBoostHz",
                "impulseSecondaryStressScale",
                "impulseSmoothAlpha",
                "impulseStressGain",
                "impulseTerminalFallHz",
                "impulseTerminalStressHz",
                "impulseThirdStressBoostHz",

                # --- Klatt hat-pattern pitch model ---
                "klattContinuationRiseHz",
                "klattDeclinationHzPerSec",
                "klattFinalFallBelowBaseHz",
                "klattGlottalLowerHz",
                "klattHatRiseHz",
                "klattQuestionRiseHz",
                "klattSmoothAlpha",
                "klattStress1Hz",
                "klattStress2Hz",
                "klattStress3Hz",
                "klattStress4Hz",

                # --- Arató (BraiLab) intonation ---
                "aratoFramesPerSyllable",
                "aratoPitchByteHz",
                "aratoStartArticle",
                "aratoStartDecl",
                "aratoStartWh",
                "aratoStartComma",
                "aratoHumpCode",
                "aratoDeclFallToLastSyll",
                "aratoDeclFallEnd",
                "aratoDeclLongFallToLastWord",
                "aratoDeclLongFallEnd",
                "aratoDeclOneSyllFall",
                "aratoWhStartFrame",
                "aratoWhOneSyllFall",
                "aratoWhTwoSyllFall",
                "aratoWhFall",
                "aratoWhTail",
                "aratoYnCreep",
                "aratoYnRise",
                "aratoYnFall",
                "aratoYnOneSyllRise1",
                "aratoYnOneSyllRise2",
                "aratoYnTwoSyllRise",
                "aratoYnTwoSyllFall",
                "aratoCommaFall",
                "aratoCommaRise",
                "aratoInflectionRef",
                "aratoColonFlat",

                # --- Hungarian / language-specific ---
                "applyLengthenedScaleToVowelsOnly",
                "huShortAVowelEnabled",
                "huShortAVowelKey",
                "huShortAVowelScale",
                "lengthenedScale",
                "lengthenedScaleHu",
                "lengthenedVowelFinalCodaScale",

                # --- Legacy pitch ---
                "legacyPitchInflectionScale",
                "legacyPitchMode",

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
                "microprosodyVoicelessCodaLengthenEnabled",
                "microprosodyVoicelessCodaLengthenScale",
                "microprosodyPreVoicelessShortenScale",
                "microprosodyVoicedF0LowerEnabled",
                "microprosodyVoicedF0LowerHz",
                "microprosodyVoicedFricativeLowerScale",
                "microprosodyVoicelessF0RaiseEnabled",
                "microprosodyVoicelessF0RaiseHz",

                # --- Nasalization ---
                "nasalizationAnticipatoryAmplitude",
                "nasalizationAnticipatoryBlend",
                "nasalizationAnticipatoryEnabled",
                "nasalMinDurationMs",

                # --- Phrase-final lengthening ---
                "phraseFinalLengtheningCodaFricativeScale",
                "phraseFinalLengtheningCodaNasalScale",
                "phraseFinalLengtheningCodaScale",
                "phraseFinalLengtheningCodaStopScale",
                "phraseFinalLengtheningEnabled",
                "phraseFinalLengtheningFinalSyllableScale",
                "phraseFinalLengtheningNucleusOnlyMode",
                "phraseFinalLengtheningNucleusDiphthongScale",
                "phraseFinalLengtheningNucleusScale",
                "phraseFinalLengtheningPenultimateSyllableScale",
                "phraseFinalLengtheningQuestionScale",
                "phraseFinalLengtheningStatementScale",

                # --- Post-stop aspiration ---
                "postStopAspirationAfterSibilant",
                "postStopAspirationDurationMs",
                "postStopAspirationEnabled",
                "postStopAspirationPhoneme",
                "postStopAspirationUnstressedScale",

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

                # --- Spelling / misc ---
                "spellingDiphthongMode",
                "stopClosureAfterNasalsEnabled",
                "stopClosureClusterFadeMs",
                "stopClosureClusterGapMs",
                "stopClosureClusterGapsEnabled",
                "stopClosureMode",
                "stopClosureNasalToStopFadeMs",
                "stopClosureNasalToStopGapMs",
                "stopClosureVowelFadeMs",
                "stopClosureVowelGapMs",
                "stopClosureWordBoundaryClusterFadeMs",
                "stopClosureWordBoundaryClusterGapMs",
                "stressedVowelHiatusFadeMs",
                "stressedVowelHiatusGapMs",
                "stripAllophoneDigits",
                "stripHyphen",

                # --- Stress timing ---
                "primaryStressDiv",
                "secondaryStressDiv",

                # --- Timing defaults ---
                "affricateDurationMs",
                "fadeAfterLiquidMs",
                "liquidFadeMs",
                "stopDurationMs",
                "tapDurationMs",
                "tiedFromVowelDurationMs",
                "tiedFromVowelFadeMs",
                "tiedVowelDurationMs",
                "trillFallbackDurationMs",
                "voicedConsonantDurationMs",
                "voicelessFricativeDurationMs",
                "vowelBeforeLiquidDurationMs",
                "vowelBeforeNasalDurationMs",

                # --- Tonal / text processing ---
                "tonal",
                "toneContoursAbsolute",
                "toneContoursMode",
                "toneDigitsEnabled",

                # --- Trill ---
                "trillModulationMs",

                # --- Word boundary ---
                "wordBoundaryDipDepth",
                "wordBoundaryDipMs",

                # --- Voice profile ---
                "voiceProfileName",

                # --- Word-final schwa ---
                "wordFinalSchwaMinDurationMs",
                "wordFinalSchwaReductionEnabled",
                "wordFinalSchwaScale",
                # --- Year splitting ---
                "yearSplittingEnabled",
                # --- Thousands separator ---
                "thousandsSeparatorCommaToSpace",
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
                    "diphthongCollapse.enabled",
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
                    "rateCompSonorantContextBonusMs",
                    "secondaryStressDiv",
                    "sonorantContextAmplitudeScale",
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
            self.settingKeyCtrl = sHelper.addLabeledControl(
                _("Setting:"),
                wx.ComboBox,
                choices=self._knownKeyDisplayNames,
                style=wx.CB_DROPDOWN | wx.CB_READONLY,
            )
            self.settingKeyCtrl.Bind(wx.EVT_COMBOBOX, self._onSettingKeyChanged)

            self.valueCtrl = sHelper.addLabeledControl(_("Value:"), wx.TextCtrl)
            self.valueCtrl.Bind(wx.EVT_TEXT, self._onGenericValueChanged)

            self.sourceLabel = sHelper.addItem(wx.StaticText(self, label=""))

            # GitHub link
            self.gitHubButton = sHelper.addItem(wx.Button(self, label=_("Open TGSpeechBox on GitHub")))
            self.gitHubButton.Bind(wx.EVT_BUTTON, self._onGitHubClick)

            # Update channel and the check for updates button
            self._makeUpdateControls(sHelper, wx)

            # Initialize defaults.
            self._setInitialLanguageTag()
            self._refreshAllDisplays()
            if self._knownKeys:
                self._setCurrentKey(self._knownKeys[0])

        def onSave(self):
            # The update channel is NVDA's to keep, not a language file's.
            if getattr(self, "updateChannelCtrl", None) is not None:
                _setUpdateChannel(self._selectedUpdateChannel())

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

        def _makeUpdateControls(self, sHelper, wx):
            """The "Update channel" choice and the "Check for Add-on Updates"
            button.  Not on a secure screen: there NVDA runs as SYSTEM on the
            sign-in desktop, and nothing is fetched or installed from it."""
            self.updateChannelCtrl = None
            self.updateButton = None
            if updates._secure():
                return
            self.updateChannelCtrl = sHelper.addLabeledControl(
                _("Update channel:"),
                wx.Choice,
                # In the order of updates.CHANNELS.
                choices=[_("Stable"), _("Prerelease")],
            )
            self.updateChannelCtrl.SetSelection(updates.CHANNELS.index(_getUpdateChannel()))
            self.updateButton = sHelper.addItem(wx.Button(self, label=_("Check for Add-on Updates")))
            self.updateButton.Bind(wx.EVT_BUTTON, self._onUpdateClick)

        def _selectedUpdateChannel(self) -> str:
            """The channel the choice shows, saved yet or not."""
            try:
                return updates.CHANNELS[self.updateChannelCtrl.GetSelection()]
            except Exception:
                return _getUpdateChannel()

        def _setUpdateBusy(self, message):
            """One check at a time.  `message` says what the network is being
            asked for and is spoken; None when the answer is in.

            The button is not disabled meanwhile: it has the focus, and a
            disabled control that has the focus leaves the keyboard nowhere
            to go.  A press while busy says the message again instead.
            """
            self._updateBusy = message
            if message:
                try:
                    import ui
                    ui.message(message)
                except Exception:
                    pass

        def _onUpdateClick(self, evt):
            """Ask GitHub's releases for a newer add-on on the chosen channel.

            Only here, when the button is pressed: nothing checks on its own
            (updates.py says why).  The asking is done off the GUI thread and
            the answer comes back in _onUpdateChecked.
            """
            evt.Skip()
            if getattr(self, "updateButton", None) is None:
                return
            if getattr(self, "_updateBusy", None):
                self._setUpdateBusy(self._updateBusy)
                return
            channel = self._selectedUpdateChannel()
            self._setUpdateBusy(_("Checking for updates..."))
            _runInBackground(lambda: updates.latest_release(channel), self._onUpdateChecked)

        def _onUpdateChecked(self, found, error):
            """The check's answer, on the GUI thread: (version, page, url) or
            (None, reason, None) from updates.latest_release."""
            if not self:  # the settings dialog was closed meanwhile
                return
            self._setUpdateBusy(None)
            try:
                import wx
            except Exception:
                return

            version, reason, addonUrl = found if found else (None, str(error), None)
            installedVersion = updates.installed_version()
            if version and updates.parse_version(installedVersion) is None:
                version, reason = None, _("The installed add-on's version could not be read.")
            if not version:
                # Couldn't check - offer the releases page instead
                result = wx.MessageBox(
                    _(
                        "Could not check for updates:\n{}\n\n"
                        "Would you like to open the releases page in your browser instead?"
                    ).format(reason),
                    _("Version Check Failed"),
                    wx.YES_NO | wx.NO_DEFAULT | wx.ICON_WARNING,
                    self,
                )
                if result == wx.YES:
                    wx.LaunchDefaultBrowser(updates.RELEASES_PAGE)
                return

            if not updates.is_newer(version, installedVersion):
                wx.MessageBox(
                    _(
                        "You already have the latest version installed.\n\n"
                        "Installed version: {}\n"
                        "Latest version: {}"
                    ).format(installedVersion, version),
                    _("No Update Available"),
                    wx.OK | wx.ICON_INFORMATION,
                    self,
                )
                return

            # Update available - confirm with user
            result = wx.MessageBox(
                _(
                    "A new version is available!\n\n"
                    "Installed version: {}\n"
                    "Latest version: {}\n\n"
                    "Download and install the update?"
                ).format(installedVersion, version),
                _("Update Available"),
                wx.YES_NO | wx.YES_DEFAULT | wx.ICON_QUESTION,
                self,
            )
            if result != wx.YES:
                return

            self._setUpdateBusy(_("Downloading the update..."))
            _runInBackground(lambda: updates.fetch(addonUrl), self._onUpdateFetched)

        def _onUpdateFetched(self, downloadPath, error):
            """The downloaded add-on, on the GUI thread."""
            if not self:
                return
            self._setUpdateBusy(None)
            try:
                import wx
            except Exception:
                return

            if error is not None or not downloadPath:
                wx.MessageBox(
                    _("Failed to download add-on:\n{}").format(str(error)),
                    _("Download Error"),
                    wx.OK | wx.ICON_ERROR,
                    self,
                )
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
            """Save the voice that is heard into a profile in phonemes.yaml.

            On a profile the voice sliders are offsets on its stored values,
            so the composed values are saved and the sliders go back to rest;
            a built-in voice becomes a profile of the same name (#195).
            """
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

            from . import profile_save

            # A profile voice is saved into itself.  A built-in voice becomes
            # a profile of its own name, which the voice list shows as
            # "<name> (profile)"; the built-in voice itself stays as it is.
            curVoice = getattr(synth, "_curVoice", "") or "Adam"
            VOICE_PROFILE_PREFIX = "profile:"
            fromProfile = curVoice.startswith(VOICE_PROFILE_PREFIX)
            profileName = curVoice[len(VOICE_PROFILE_PREFIX):] if fromProfile else curVoice

            # What is heard is the voice source the driver gives the player
            # for this voice and these sliders: _applyVoicingTone composes a
            # profile's stored values with the sliders, and nothing is worked
            # out a second time here.
            synth._lastAppliedVoicingTone = None
            synth._applyVoicingTone(profileName if fromProfile else "")
            tone = getattr(synth, "_lastAppliedVoicingTone", None)
            if tone is None:
                wx.MessageBox(
                    _("The voice that is playing could not be read, so nothing was saved."),
                    _("Cannot Save"),
                    wx.OK | wx.ICON_WARNING,
                    self,
                )
                evt.Skip()
                return
            heard = {key: getattr(tone, key) for key in profile_save.TONE_KEYS}
            sliders = {slider[0]: getattr(synth, slider[0]) for slider in profile_save.SLIDERS}
            # Without a voicingTone block of its own a profile plays the
            # engine's voice source and the sliders are the values, as on a
            # built-in voice.
            hasStoredTone = fromProfile and frontend.hasExplicitVoicingTone()
            toneSet, toneRemove = profile_save.toneChanges(heard, sliders, hasStoredTone)

            if fromProfile and not toneSet:
                wx.MessageBox(
                    _("No voice slider is away from its neutral position, so profile '{}' "
                      "already holds the voice you hear. Nothing was saved.").format(profileName),
                    _("Save Voice Profile Settings"),
                    wx.OK | wx.ICON_INFORMATION,
                    self,
                )
                evt.Skip()
                return

            # A new profile made from a built-in voice takes that voice's
            # shape (the preset's frame multipliers, which are not applied to
            # a profile voice) as class scales.
            profileExists = fromProfile or profileName in (frontend.getVoiceProfileNames() or [])
            classScales = None
            notCarriedNote = ""
            whisperNote = ""
            if not profileExists:
                from .constants import voices
                preset = voices.get(profileName) or {}
                scales, notCarried = profile_save.presetClassScales(preset)
                if scales:
                    classScales = {cls: scales for cls in profile_save.ROOT_CLASSES}
                if notCarried:
                    notCarriedNote = _(
                        "These values of the built-in voice have no place in a profile's class scales "
                        "and are not carried over: {}. Use the phoneme editor to adjust the profile."
                    ).format(", ".join(notCarried))
                # A whisper voice's profile is not that voice
                # (profile_save.isWhisper says why, with the measurement).
                # Saving is not refused; the listener is told first, in
                # plain words, and decides.
                if profile_save.isWhisper(preset):
                    whisperNote = _(
                        "{} is a whisper voice, and this button cannot put a whisper into a profile. "
                        "The saved profile will not sound like {}: it will be quieter and partly voiced. "
                        "To make a whisper profile, use the phoneme editor."
                    ).format(profileName, profileName)

            # Build confirmation message
            if fromProfile:
                parts = [_("Save the voice you hear now into profile '{}'?").format(profileName)]
            elif profileExists:
                parts = [_(
                    "A profile named '{}' already exists. Replace its voice source with the built-in "
                    "voice of that name, as you hear it now? Its class scales and phoneme overrides stay."
                ).format(profileName)]
            elif whisperNote:
                parts = [whisperNote, _("Make a voice profile named '{}' from it all the same?").format(profileName)]
            else:
                parts = [_(
                    "Make a voice profile named '{}' from the built-in voice of that name, as you hear it now?"
                ).format(profileName)]
            parts.append(_("Voice source values to write:\n{}").format(
                "\n".join("  {}: {}".format(key, profile_save.formatValue(value)) for key, value in toneSet.items())
            ))
            if fromProfile:
                parts.append(_(
                    "The voice sliders you moved will go back to their neutral positions, "
                    "because the profile will hold their effect."
                ))
            elif classScales:
                parts.append(_("The built-in voice's own shape goes into the profile's class scales."))
            if notCarriedNote:
                parts.append(notCarriedNote)
            parts.append(_(
                "Voice quality sliders (creakiness, breathiness, jitter, shimmer, sharpness) "
                "are listener settings and are not saved."
            ))
            parts.append(_("This will update packs/phonemes.yaml."))

            result = wx.MessageBox(
                "\n\n".join(parts),
                _("Save Voice Profile Settings"),
                wx.YES_NO | wx.ICON_QUESTION,
                self,
            )

            if result != wx.YES:
                evt.Skip()
                return

            try:
                packDir = getattr(frontend, "_packDir", None) or _getPacksDir()
                phonemesPath = os.path.join(packDir, "packs", "phonemes.yaml")
                if not os.path.isfile(phonemesPath):
                    phonemesPath = os.path.join(packDir, "phonemes.yaml")
                # The file as it is, to put back if the frontend cannot
                # load what is saved.
                with open(phonemesPath, "rb") as f:
                    fileBefore = f.read()
                profile_save.saveProfile(phonemesPath, profileName, toneSet, toneRemove, classScales)
            except Exception as e:
                wx.MessageBox(
                    _("Error saving to phonemes.yaml: {}").format(_profileSaveReason(e)),
                    _("Error"),
                    wx.OK | wx.ICON_ERROR,
                    self,
                )
                evt.Skip()
                return

            # Trigger frontend reload and re-apply voice profile.
            # Through the driver, which knows the language "auto"
            # stands for: the frontend takes the literal "auto",
            # loads default.yaml alone and calls that a success (#194).
            #
            # Nothing is saved until the frontend has read the file back.
            # A reload that fails leaves the old profile playing: with the
            # sliders reset the voice would jump, and with them left moved
            # over a saved file the next reload would add their offset to
            # the saved value a second time.  So the file goes back as it
            # was, the sliders stay, and the listener is told.
            reloadError = ""
            try:
                reloaded = synth.reloadLanguagePack()
                if not reloaded:
                    reloadError = frontend.getLastError() or ""
            except Exception as e:
                reloaded = False
                reloadError = str(e) or e.__class__.__name__
            if not reloaded:
                try:
                    profile_save.writeFile(phonemesPath, fileBefore)
                except Exception as e:
                    wx.MessageBox(
                        _(
                            "TGSpeechBox could not load phonemes.yaml after the save, "
                            "and the file could not be put back as it was: {}"
                        ).format(str(e)),
                        _("Error"),
                        wx.OK | wx.ICON_ERROR,
                        self,
                    )
                    evt.Skip()
                    return
                # The frontend may hold some of what it could not finish
                # loading; it reads the file that was put back.
                try:
                    synth.reloadLanguagePack()
                    if fromProfile:
                        frontend.setVoiceProfile(profileName)
                        synth._applyVoicingTone(profileName)
                except Exception:
                    pass
                undone = [_(
                    "TGSpeechBox could not load phonemes.yaml after the save, so the save was undone: "
                    "the file is as it was before, and nothing was saved."
                )]
                if fromProfile:
                    undone.append(_("The voice sliders you moved are where you left them."))
                if reloadError:
                    undone.append(reloadError)
                wx.MessageBox(
                    "\n\n".join(undone),
                    _("Save Undone"),
                    wx.OK | wx.ICON_ERROR,
                    self,
                )
                evt.Skip()
                return

            try:
                # A new profile can be chosen at once.  NVDA builds a
                # driver's voice list one time and keeps it, so it is built
                # again here.
                names = frontend.getVoiceProfileNames()
                if names:
                    synth._voiceProfiles = names
                    synth._availableVoices = synth._getAvailableVoices()
                if fromProfile:
                    frontend.setVoiceProfile(profileName)
                    # The profile holds the sliders' effect now.  At rest
                    # they leave it as saved; left where they were, their
                    # offset would be applied a second time.  NVDA's saved
                    # copy follows, because Cancel in this dialog and a
                    # configuration profile switch replay it.
                    try:
                        import config
                        section = config.conf["speech"][synth.name]
                    except Exception:
                        section = None
                    for setting, toneKey, rest, store in profile_save.SLIDERS:
                        setattr(synth, setting, rest)
                        if section is not None and str(section.get(setting)) != str(rest):
                            section[setting] = rest
                    synth._applyVoicingTone(profileName)
                else:
                    # The built-in voice keeps its sliders.  Positions kept
                    # for the profile from earlier in this session belong to
                    # the values it had then.
                    for setting, toneKey, rest, store in profile_save.SLIDERS:
                        getattr(synth, store, {}).pop(VOICE_PROFILE_PREFIX + profileName, None)
            except Exception:
                pass

            if fromProfile:
                doneMsg = _(
                    "Saved the voice you hear into profile '{}'.\n\n"
                    "The voice sliders you had moved are back at their neutral positions: "
                    "the profile holds their effect now, so the voice sounds the same."
                ).format(profileName)
            else:
                doneMsg = _(
                    "Saved profile '{}'. It is in the voice list as \"{}\"; "
                    "the built-in voice and its sliders are unchanged."
                ).format(profileName, profileName + " (profile)")
                if whisperNote:
                    doneMsg += "\n\n" + whisperNote
                if notCarriedNote:
                    doneMsg += "\n\n" + notCarriedNote
            wx.MessageBox(
                doneMsg,
                _("Save Complete"),
                wx.OK | wx.ICON_INFORMATION,
                self,
            )

            evt.Skip()

        def _onExportPhonemeFileClick(self, evt):
            """Export phonemes.yaml to a user-chosen location via Save As dialog."""
            import os
            import shutil
            import wx

            # Find the phonemes.yaml inside the pack directory
            try:
                import synthDriverHandler
                synth = synthDriverHandler.getSynth()
                frontend = getattr(synth, "_frontend", None) if synth else None
                packDir = getattr(frontend, "_packDir", None) if frontend else None
            except Exception:
                packDir = None

            if not packDir:
                wx.MessageBox(
                    _("Cannot determine pack directory."),
                    _("Export Error"),
                    wx.OK | wx.ICON_ERROR,
                    self,
                )
                evt.Skip()
                return

            srcPath = os.path.join(packDir, "packs", "phonemes.yaml")
            if not os.path.isfile(srcPath):
                srcPath = os.path.join(packDir, "phonemes.yaml")
            if not os.path.isfile(srcPath):
                wx.MessageBox(
                    _("phonemes.yaml not found in pack directory."),
                    _("Export Error"),
                    wx.OK | wx.ICON_ERROR,
                    self,
                )
                evt.Skip()
                return

            dlg = wx.FileDialog(
                self,
                _("Save Voice Profile Sliders to YAML As"),
                defaultDir=os.path.expanduser("~"),
                defaultFile="phonemes.yaml",
                wildcard="YAML files (*.yaml)|*.yaml|All files (*.*)|*.*",
                style=wx.FD_SAVE | wx.FD_OVERWRITE_PROMPT,
            )
            if dlg.ShowModal() == wx.ID_OK:
                destPath = dlg.GetPath()
                try:
                    shutil.copy2(srcPath, destPath)
                    wx.MessageBox(
                        _("Saved to:\n{}").format(destPath),
                        _("Save Complete"),
                        wx.OK | wx.ICON_INFORMATION,
                        self,
                    )
                except Exception as e:
                    wx.MessageBox(
                        _("Failed to export: {}").format(str(e)),
                        _("Export Error"),
                        wx.OK | wx.ICON_ERROR,
                        self,
                    )
            dlg.Destroy()
            evt.Skip()

        def _onImportPhonemeFileClick(self, evt):
            """Import a phonemes.yaml from a user-chosen location, replacing the current one."""
            import os
            import shutil
            import wx

            # Find the destination phonemes.yaml inside the pack directory
            try:
                import synthDriverHandler
                synth = synthDriverHandler.getSynth()
                frontend = getattr(synth, "_frontend", None) if synth else None
                packDir = getattr(frontend, "_packDir", None) if frontend else None
            except Exception:
                packDir = None

            if not packDir:
                wx.MessageBox(
                    _("Cannot determine pack directory."),
                    _("Import Error"),
                    wx.OK | wx.ICON_ERROR,
                    self,
                )
                evt.Skip()
                return

            destPath = os.path.join(packDir, "packs", "phonemes.yaml")
            if not os.path.isdir(os.path.dirname(destPath)):
                destPath = os.path.join(packDir, "phonemes.yaml")

            dlg = wx.FileDialog(
                self,
                _("Import Voice Profile Sliders"),
                defaultDir=os.path.expanduser("~"),
                defaultFile="phonemes.yaml",
                wildcard="YAML files (*.yaml)|*.yaml|All files (*.*)|*.*",
                style=wx.FD_OPEN | wx.FD_FILE_MUST_EXIST,
            )
            if dlg.ShowModal() == wx.ID_OK:
                srcPath = dlg.GetPath()
                result = wx.MessageBox(
                    _("Replace the current phonemes.yaml with:\n{}\n\n"
                      "Your current voice profile settings will be overwritten.\n"
                      "The voice will reload automatically.").format(srcPath),
                    _("Confirm Import"),
                    wx.YES_NO | wx.ICON_QUESTION,
                    self,
                )
                if result == wx.YES:
                    try:
                        shutil.copy2(srcPath, destPath)
                        wx.MessageBox(
                            _("Imported successfully. Reloading voice..."),
                            _("Import Complete"),
                            wx.OK | wx.ICON_INFORMATION,
                            self,
                        )
                        # Reload the pack so the imported settings take effect
                        # (through the driver, for the language "auto", #194)
                        try:
                            synth.reloadLanguagePack()
                            curVoice = getattr(synth, "_curVoice", "")
                            if curVoice.startswith("profile:"):
                                profileName = curVoice[len("profile:"):]
                                frontend.setVoiceProfile(profileName)
                                synth._applyVoicingTone(profileName)
                        except Exception:
                            pass
                    except Exception as e:
                        wx.MessageBox(
                            _("Failed to import: {}").format(str(e)),
                            _("Import Error"),
                            wx.OK | wx.ICON_ERROR,
                            self,
                        )
            dlg.Destroy()
            evt.Skip()

        def _getUserDictPath(self):
            """Return the path to the user dictionary TSV for the current language."""
            import os
            try:
                import synthDriverHandler
                synth = synthDriverHandler.getSynth()
                # Use _resolvedLang (actual language) — _language may be "auto"
                langTag = getattr(synth, "_resolvedLang", None) \
                    or getattr(synth, "_language", "en-us") or "en-us"
                if langTag == "auto":
                    langTag = "en-us"
            except Exception:
                langTag = "en-us"
            return os.path.join(self._packsDir, "dict", langTag + "-user.tsv")

        def _onExportUserDictClick(self, evt):
            """Export the user dictionary TSV to a user-chosen location."""
            import os
            import shutil
            import wx

            srcPath = self._getUserDictPath()
            if not os.path.isfile(srcPath):
                wx.MessageBox(
                    _("No user dictionary found at:\n{}").format(srcPath),
                    _("Export Error"),
                    wx.OK | wx.ICON_WARNING,
                    self,
                )
                evt.Skip()
                return

            dlg = wx.FileDialog(
                self,
                _("Export User Dictionary"),
                defaultDir=os.path.expanduser("~"),
                defaultFile=os.path.basename(srcPath),
                wildcard="TSV files (*.tsv)|*.tsv|All files (*.*)|*.*",
                style=wx.FD_SAVE | wx.FD_OVERWRITE_PROMPT,
            )
            if dlg.ShowModal() == wx.ID_OK:
                try:
                    shutil.copy2(srcPath, dlg.GetPath())
                    wx.MessageBox(
                        _("User dictionary exported to:\n{}").format(dlg.GetPath()),
                        _("Export Complete"),
                        wx.OK | wx.ICON_INFORMATION,
                        self,
                    )
                except Exception as e:
                    wx.MessageBox(
                        _("Failed to export: {}").format(str(e)),
                        _("Export Error"),
                        wx.OK | wx.ICON_ERROR,
                        self,
                    )
            dlg.Destroy()
            evt.Skip()

        def _onImportUserDictClick(self, evt):
            """Import a user dictionary TSV from a user-chosen location."""
            import os
            import shutil
            import wx

            destPath = self._getUserDictPath()

            dlg = wx.FileDialog(
                self,
                _("Import User Dictionary"),
                defaultDir=os.path.expanduser("~"),
                defaultFile=os.path.basename(destPath),
                wildcard="TSV files (*.tsv)|*.tsv|All files (*.*)|*.*",
                style=wx.FD_OPEN | wx.FD_FILE_MUST_EXIST,
            )
            if dlg.ShowModal() == wx.ID_OK:
                srcPath = dlg.GetPath()

                # Validate that the file looks like a 5-column user dict,
                # not a stress dict (2 cols) or compound dict (2 cols).
                try:
                    with open(srcPath, "r", encoding="utf-8") as f:
                        for line in f:
                            line = line.rstrip("\n\r")
                            if not line or line.startswith("#"):
                                continue
                            ncols = len(line.split("\t"))
                            if ncols < 3:
                                wx.MessageBox(
                                    _("This file does not appear to be a user pronunciation dictionary.\n\n"
                                      "Expected 5 tab-separated columns (from_text, to_text, from_ipa, to_ipa, category) "
                                      "but the first data row has {} column(s).\n\n"
                                      "Make sure you are importing a user dictionary TSV, "
                                      "not a stress or compound dictionary.").format(ncols),
                                    _("Import Error"),
                                    wx.OK | wx.ICON_WARNING,
                                    self,
                                )
                                dlg.Destroy()
                                evt.Skip()
                                return
                            break  # first data row checked
                except Exception as e:
                    wx.MessageBox(
                        _("Failed to read file: {}").format(str(e)),
                        _("Import Error"),
                        wx.OK | wx.ICON_ERROR,
                        self,
                    )
                    dlg.Destroy()
                    evt.Skip()
                    return

                result = wx.MessageBox(
                    _("Replace the current user dictionary with:\n{}\n\n"
                      "Existing user entries will be overwritten.\n"
                      "The dictionary will reload automatically.").format(srcPath),
                    _("Confirm Import"),
                    wx.YES_NO | wx.ICON_QUESTION,
                    self,
                )
                if result == wx.YES:
                    try:
                        os.makedirs(os.path.dirname(destPath), exist_ok=True)
                        shutil.copy2(srcPath, destPath)
                        wx.MessageBox(
                            _("Imported successfully. Reloading..."),
                            _("Import Complete"),
                            wx.OK | wx.ICON_INFORMATION,
                            self,
                        )
                        # Reload the language to pick up new dict entries.
                        # Go through synth._set_language so "auto" is resolved
                        # and voice profile / eSpeak config are preserved.
                        try:
                            import synthDriverHandler
                            synth = synthDriverHandler.getSynth()
                            synth._set_language(synth._language)
                        except Exception:
                            pass
                    except Exception as e:
                        wx.MessageBox(
                            _("Failed to import: {}").format(str(e)),
                            _("Import Error"),
                            wx.OK | wx.ICON_ERROR,
                            self,
                        )
            dlg.Destroy()
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

    dlgCls = getattr(settingsDialogs, "NVDASettingsDialog", None)
    if dlgCls is None:
        return

    # Avoid duplicate registration.
    cats = getattr(dlgCls, "categoryClasses", None)
    if isinstance(cats, (list, tuple)) and panelCls in cats:
        return

    # NVDA 2024.4+: append to categoryClasses list.
    if cats is not None:
        cats.append(panelCls)
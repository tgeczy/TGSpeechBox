"""The NVDA driver's voicing tone for a voice profile (3.10 beta 10, #124).

Runs the real VoicingToneMixin._applyVoicingTone from the add-on with stand-ins
for NVDA, the frontend and the player, and checks the contract:

- a profile with its own voicingTone block: every stored value reaches the
  player with the listener's settings at neutral; a moved setting composes
  with it; moving it back restores it;
- a built-in voice, or a profile without a block: exactly the values the
  driver produced before the change (the settings are the values);
- a David-derived profile keeps its stored head size at the neutral setting
  (the built-in David's head-size 100 lives inside the profile now).

No NVDA, DLL or audio is needed.
"""
from __future__ import annotations

import math
import pathlib
import types

import pytest

REPO = pathlib.Path(__file__).resolve().parent.parent
SRC = REPO / "nvdaAddon" / "synthDrivers" / "tgSpeechBox" / "voicing_tone.py"

TONE_FIELDS = (
    "voicingPeakPos", "voicedPreEmphA", "voicedPreEmphMix",
    "highShelfGainDb", "highShelfFcHz", "highShelfQ",
    "voicedTiltDbPerOct", "noiseGlottalModDepth",
    "pitchSyncF1DeltaHz", "pitchSyncB1DeltaHz",
    "speedQuotient", "aspirationTiltDbPerOct", "cascadeBwScale", "tremorDepth",
    "nasalBwScale", "f4FreqScale", "nasalGainScale",
    "chorusDepth", "chorusDetuneHz",
)
DSP_DEFAULTS = dict(
    voicingPeakPos=0.91, voicedPreEmphA=0.92, voicedPreEmphMix=0.35,
    highShelfGainDb=4.0, highShelfFcHz=2000.0, highShelfQ=0.7,
    voicedTiltDbPerOct=0.0, noiseGlottalModDepth=0.0,
    pitchSyncF1DeltaHz=0.0, pitchSyncB1DeltaHz=0.0,
    speedQuotient=2.0, aspirationTiltDbPerOct=0.0, cascadeBwScale=0.9, tremorDepth=0.0,
    nasalBwScale=1.0, f4FreqScale=1.0, nasalGainScale=1.0,
    chorusDepth=0.0, chorusDetuneHz=2.0,
)


class _Tone:
    def __init__(self, **kw):
        for k in TONE_FIELDS:
            setattr(self, k, kw.get(k, DSP_DEFAULTS[k]))

    @classmethod
    def defaults(cls):
        return cls()


def _load_mixin():
    src = SRC.read_text(encoding="utf-8")
    src = src.replace("from logHandler import log", "")
    src = src.replace("from . import speechPlayer", "")
    src = src.replace("from .constants import VOICE_PROFILE_PREFIX", "VOICE_PROFILE_PREFIX = 'profile:'")
    ns = {
        "__name__": "voicing_tone_under_test",
        "log": types.SimpleNamespace(error=lambda *a, **k: None, debug=lambda *a, **k: None,
                                     warning=lambda *a, **k: None, info=lambda *a, **k: None),
        "speechPlayer": types.SimpleNamespace(VoicingTone=_Tone),
    }
    exec(compile(src, str(SRC), "exec"), ns)
    return ns["VoicingToneMixin"]


VoicingToneMixin = _load_mixin()


class _Player:
    def __init__(self):
        self.tone = None

    def hasVoicingToneSupport(self):
        return True

    def setVoicingTone(self, tone):
        self.tone = tone


class _Frontend:
    def __init__(self, profile_tone):
        self.profile_tone = profile_tone  # None = no voicingTone block

    def setVoiceProfile(self, name):
        pass

    def hasExplicitVoicingTone(self):
        return self.profile_tone is not None

    def getVoicingTone(self):
        return self.profile_tone


# Slider neutral positions in the driver.
NEUTRAL = dict(_curVoiceTilt=50, _curNoiseGlottalMod=0, _curPitchSyncF1=50, _curPitchSyncB1=50,
               _curSpeedQuotient=50, _curAspirationTilt=50, _curCascadeBwScale=50,
               _curVoiceTremor=0, _curHeadSize=50, _curChorusDepth=0, _curChorusDetune=33)


def _run(profile_tone, profile_name="Probe", **sliders):
    class Driver(VoicingToneMixin):
        def _pushFrameExDefaultsToFrontend(self):
            pass

    d = Driver()
    d._player = _Player()
    d._frontend = _Frontend(profile_tone)
    for k, v in {**NEUTRAL, **sliders}.items():
        setattr(d, k, v)
    d._applyVoicingTone(profile_name)
    assert d._player.tone is not None
    return d._player.tone


def _frontend_tone(**stored):
    """What nvspFrontend_getVoicingTone returns: its defaults plus the stored keys."""
    base = dict(DSP_DEFAULTS, highShelfGainDb=5.5, cascadeBwScale=1.0)
    base.update(stored)
    return _Tone(**base)


# Every composable key non-default (Astra's fixture values plus the rest).
STORED = dict(voicingPeakPos=0.88, voicedPreEmphA=0.9, voicedPreEmphMix=0.22,
              highShelfGainDb=2.0, highShelfFcHz=2400.0, highShelfQ=0.8,
              voicedTiltDbPerOct=-10.0, noiseGlottalModDepth=0.3,
              pitchSyncF1DeltaHz=12.0, pitchSyncB1DeltaHz=8.0,
              speedQuotient=1.6, aspirationTiltDbPerOct=3.0, cascadeBwScale=0.8, tremorDepth=0.1,
              nasalBwScale=1.3, f4FreqScale=1.08, nasalGainScale=1.1)


def _old_builtin(**s):
    """The driver's values for a built-in voice before the change."""
    s = {**NEUTRAL, **s}
    sq = s["_curSpeedQuotient"]
    sq = 0.5 + (sq / 50.0) * 1.5 if sq <= 50 else 2.0 + ((sq - 50.0) / 50.0) * 2.0
    bw = s["_curCascadeBwScale"]
    bw = 2.0 - (bw / 50.0) if bw <= 50 else 1.0 - ((bw - 50.0) / 50.0) * 0.7
    hs = s["_curHeadSize"]
    f4 = 1.25 - (hs / 50.0) * 0.25 if hs <= 50 else 1.0 - ((hs - 50.0) / 50.0) * 0.15
    return dict(
        voicedTiltDbPerOct=max(-24.0, min(24.0, (s["_curVoiceTilt"] - 50.0) * 0.48)),
        noiseGlottalModDepth=s["_curNoiseGlottalMod"] / 100.0,
        pitchSyncF1DeltaHz=(s["_curPitchSyncF1"] - 50.0) * 1.2,
        pitchSyncB1DeltaHz=(s["_curPitchSyncB1"] - 50.0) * 1.0,
        speedQuotient=sq,
        aspirationTiltDbPerOct=(s["_curAspirationTilt"] - 50.0) * 0.24,
        cascadeBwScale=max(0.3, min(2.0, bw)),
        tremorDepth=max(0.0, min(0.5, s["_curVoiceTremor"] / 100.0 * 0.4)),
        f4FreqScale=max(0.7, min(1.5, f4)),
        chorusDepth=s["_curChorusDepth"] / 100.0,
        chorusDetuneHz=0.5 + s["_curChorusDetune"] / 100.0 * 4.5,
    )


def test_profile_values_reach_the_player_at_neutral_settings():
    tone = _run(_frontend_tone(**STORED))
    for k, v in STORED.items():
        assert math.isclose(getattr(tone, k), v, abs_tol=1e-12), k


@pytest.mark.parametrize("sliders,field,expected", [
    (dict(_curSpeedQuotient=25), "speedQuotient", 1.6 * (1.25 / 2.0)),
    (dict(_curHeadSize=75), "f4FreqScale", 1.08 * 0.925),
    (dict(_curVoiceTilt=60), "voicedTiltDbPerOct", -10.0 + 4.8),
    (dict(_curNoiseGlottalMod=20), "noiseGlottalModDepth", 0.3 + 0.2),
    (dict(_curPitchSyncF1=60), "pitchSyncF1DeltaHz", 12.0 + 12.0),
    (dict(_curAspirationTilt=25), "aspirationTiltDbPerOct", 3.0 - 6.0),
    (dict(_curCascadeBwScale=75), "cascadeBwScale", 0.8 * 0.65),
    (dict(_curVoiceTremor=50), "tremorDepth", 0.1 + 0.2),
])
def test_a_moved_setting_composes_with_the_profile(sliders, field, expected):
    tone = _run(_frontend_tone(**STORED), **sliders)
    assert math.isclose(getattr(tone, field), expected, abs_tol=1e-9), field
    # The rest of the profile is untouched by that one setting.
    for k, v in STORED.items():
        if k != field:
            assert math.isclose(getattr(tone, k), v, abs_tol=1e-12), k


def test_returning_a_setting_to_neutral_restores_the_profile():
    _run(_frontend_tone(**STORED), _curSpeedQuotient=10, _curHeadSize=90)
    tone = _run(_frontend_tone(**STORED))
    assert math.isclose(tone.speedQuotient, 1.6) and math.isclose(tone.f4FreqScale, 1.08)


@pytest.mark.parametrize("sliders", [
    {}, dict(_curSpeedQuotient=30, _curHeadSize=80), dict(_curVoiceTilt=20, _curCascadeBwScale=90),
    dict(_curNoiseGlottalMod=40, _curPitchSyncF1=70, _curPitchSyncB1=10, _curVoiceTremor=30),
    dict(_curChorusDepth=50, _curChorusDetune=80),
])
@pytest.mark.parametrize("profile_name,profile_tone", [("", None), ("NoBlock", None)])
def test_built_in_voices_and_blockless_profiles_are_unchanged(sliders, profile_name, profile_tone):
    tone = _run(profile_tone, profile_name=profile_name, **sliders)
    for k, v in _old_builtin(**sliders).items():
        assert math.isclose(getattr(tone, k), v, abs_tol=1e-12), k
    for k in ("voicingPeakPos", "voicedPreEmphA", "voicedPreEmphMix", "highShelfGainDb",
              "highShelfFcHz", "highShelfQ", "nasalBwScale", "nasalGainScale"):
        assert math.isclose(getattr(tone, k), DSP_DEFAULTS[k], abs_tol=1e-12), k


def test_david_derived_profile_keeps_its_head_size():
    # The editor saves the built-in David's head size (slider 100) as f4FreqScale 0.85.
    tone = _run(_frontend_tone(f4FreqScale=0.85, voicedTiltDbPerOct=-3.0, highShelfGainDb=4.0),
                profile_name="David variant")
    assert math.isclose(tone.f4FreqScale, 0.85, abs_tol=1e-12)
    assert math.isclose(tone.voicedTiltDbPerOct, -3.0, abs_tol=1e-12)


def test_chorus_stays_a_listener_setting():
    tone = _run(_frontend_tone(**STORED), _curChorusDepth=40, _curChorusDetune=60)
    assert math.isclose(tone.chorusDepth, 0.4) and math.isclose(tone.chorusDetuneHz, 0.5 + 0.6 * 4.5)

"""ctypes mirrors of the engine's C structs.

The FrameEx and VoicingTone field lists are generated from the C headers by
tools/gen_frame_ex.py and tools/gen_voicing_tone.py (the blocks between the
markers); tests/test_python_package.py runs both with --check.  Frame is the
47-double base frame, frozen since ABI v1 and checked against nvspFrontend.h.
"""
from __future__ import annotations

import ctypes
from ctypes import c_double, c_uint32

#: VoicingTone header values (voicingTone.h).  The DSP reads the layout from
#: structSize; the version stays at 3 on purpose, as in speechPlayer.py.
VOICINGTONE_MAGIC = 0x32544F56  # "VOT2"
VOICINGTONE_VERSION = 3

FRAME_FIELDS = (
    "voicePitch", "vibratoPitchOffset", "vibratoSpeed", "voiceTurbulenceAmplitude",
    "glottalOpenQuotient", "voiceAmplitude", "aspirationAmplitude",
    "cf1", "cf2", "cf3", "cf4", "cf5", "cf6", "cfN0", "cfNP",
    "cb1", "cb2", "cb3", "cb4", "cb5", "cb6", "cbN0", "cbNP",
    "caNP",
    "fricationAmplitude",
    "pf1", "pf2", "pf3", "pf4", "pf5", "pf6",
    "pb1", "pb2", "pb3", "pb4", "pb5", "pb6",
    "pa1", "pa2", "pa3", "pa4", "pa5", "pa6",
    "parallelBypass", "preFormantGain", "outputGain", "endVoicePitch",
)


class Frame(ctypes.Structure):
    """One synthesis frame (nvspFrontend_Frame / speechPlayer_frame_t)."""
    _fields_ = [(name, c_double) for name in FRAME_FIELDS]

    def to_dict(self) -> dict:
        return {name: getattr(self, name) for name in FRAME_FIELDS}


class FrameEx(ctypes.Structure):
    """Per-frame voice-quality extensions (nvspFrontend_FrameEx)."""
    # >>> AUTO-GENERATED FROM src/frame.h - DO NOT EDIT MANUALLY (regenerate: python tools/gen_frame_ex.py) >>>
    _fields_ = [
        # Voice quality parameters (DSP v5)
        ("creakiness", c_double),  # laryngealization / creaky voice (e.g. Danish stød)
        ("breathiness", c_double),  # breath noise mixed into voicing
        ("jitter", c_double),  # pitch period variation (irregular F0)
        ("shimmer", c_double),  # amplitude variation (irregular loudness)
        ("sharpness", c_double),  # glottal closure sharpness MULTIPLIER (0=use SR default, 0.5-2.0 typical)
        # Formant end targets for within-frame ramping (NAN = no ramp)
        ("endCf1", c_double),  # Cascade F1 end target (Hz), NAN = no ramp
        ("endCf2", c_double),  # Cascade F2 end target (Hz), NAN = no ramp
        ("endCf3", c_double),  # Cascade F3 end target (Hz), NAN = no ramp
        ("endPf1", c_double),  # Parallel F1 end target (Hz), NAN = no ramp
        ("endPf2", c_double),  # Parallel F2 end target (Hz), NAN = no ramp
        ("endPf3", c_double),  # Parallel F3 end target (Hz), NAN = no ramp
        # Fujisaki-Bartman pitch contour model (DSP v6+)
        ("fujisakiEnabled", c_double),  # 0.0 = off (legacy behavior), >0.5 = on
        ("fujisakiReset", c_double),  # rising edge resets model filter state
        ("fujisakiPhraseAmp", c_double),  # phrase command amplitude (e.g. 1.3)
        ("fujisakiPhraseLen", c_double),  # phrase filter L (samples to peak). 0 = use default
        ("fujisakiAccentAmp", c_double),  # accent command amplitude (e.g. 0.4)
        ("fujisakiAccentDur", c_double),  # accent duration D (samples). 0 = use default
        ("fujisakiAccentLen", c_double),  # accent filter L (samples to peak). 0 = use default
        # Per-parameter transition speed scales (DSP v7). 0.0 = no override.
        ("transF1Scale", c_double),  # cf1, pf1, cb1, pb1
        ("transF2Scale", c_double),  # cf2, pf2, cb2, pb2
        ("transF3Scale", c_double),  # cf3, pf3, cb3, pb3
        ("transNasalScale", c_double),  # cfN0, cfNP, cbN0, cbNP, caNP
        # Amplitude crossfade mode (DSP v7.1). 0=linear, 1=equal-power.
        ("transAmplitudeMode", c_double),
        # Higher cascade formants F7/F8 (DSP v8, Rabiner 1968 defaults)
        ("cf7", c_double),  # F7 frequency (Hz).  Default 6500.0
        ("cb7", c_double),  # F7 bandwidth (Hz).  Default 720.0
        ("cf8", c_double),  # F8 frequency (Hz).  Default 7500.0
        ("cb8", c_double),  # F8 bandwidth (Hz).  Default 1250.0
        # Source amplitude timing (DSP v8). 0.0 = legacy, no hold.
        ("transSourceHoldRatio", c_double),
        # Voicing onset hold (DSP v8). 0.0 = legacy, no hold.
        ("transVoicingHoldRatio", c_double),
        # Frication spectral tilt (DSP v9). 0=flat, negative=darken high-freq parallels.
        ("fricationTiltDb", c_double),
        # Voice amplitude end target (DSP v9). NAN=hold flat; finite=per-sample linear ramp like endVoicePitch.
        ("endVoiceAmplitude", c_double),
        # Amplitude onset glide ms (DSP v9). 0=legacy step; >0 = voiceAmplitude+outputGain glide in from the previous values.
        ("amplitudeOnsetMs", c_double),
    ]
    # <<< END AUTO-GENERATED <<<

    def to_dict(self) -> dict:
        return {name: getattr(self, name) for name, _ in self._fields_}


class VoicingTone(ctypes.Structure):
    """The DSP's voice-source settings (speechPlayer_voicingTone_t)."""
    _fields_ = [
        ("magic", c_uint32),
        ("structSize", c_uint32),
        ("structVersion", c_uint32),
        ("dspVersion", c_uint32),
        # >>> AUTO-GENERATED FROM src/voicingTone.h - DO NOT EDIT MANUALLY (regenerate: python tools/gen_voicing_tone.py) >>>
        # V1 parameters
        ("voicingPeakPos", c_double),
        ("voicedPreEmphA", c_double),
        ("voicedPreEmphMix", c_double),
        ("highShelfGainDb", c_double),
        ("highShelfFcHz", c_double),
        ("highShelfQ", c_double),
        ("voicedTiltDbPerOct", c_double),
        # V2 parameters
        ("noiseGlottalModDepth", c_double),
        ("pitchSyncF1DeltaHz", c_double),
        ("pitchSyncB1DeltaHz", c_double),
        # V3 parameters
        ("speedQuotient", c_double),
        ("aspirationTiltDbPerOct", c_double),
        ("cascadeBwScale", c_double),
        ("tremorDepth", c_double),
        # V4 parameters - vocal tract shape
        ("nasalBwScale", c_double),
        ("f4FreqScale", c_double),
        ("nasalGainScale", c_double),
        # V5 parameters - dual-oscillator chorus (vocal fold asymmetry)
        ("chorusDepth", c_double),
        ("chorusDetuneHz", c_double),
        # <<< END AUTO-GENERATED <<<
    ]

    def to_dict(self) -> dict:
        return {name: getattr(self, name) for name, _ in self._fields_[4:]}


class FrontendVoicingTone(ctypes.Structure):
    """A voice profile's voice source as the frontend reports it
    (nvspFrontend_VoicingTone: the same fields, no header)."""
    _fields_ = VoicingTone._fields_[4:]

    def to_dict(self) -> dict:
        return {name: getattr(self, name) for name, _ in self._fields_}


#: void (*)(void* userData, const Frame*, const FrameEx*, double durationMs,
#:          double fadeMs, int userIndex)
FrameExCallback = ctypes.CFUNCTYPE(
    None, ctypes.c_void_p, ctypes.POINTER(Frame), ctypes.POINTER(FrameEx),
    c_double, c_double, ctypes.c_int)

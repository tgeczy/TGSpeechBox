#!/usr/bin/env python3
"""
formant_trajectory.py

Accurate simulation of TGSpeechBox's frame manager and formant synthesis,
with visualization of formant trajectories over time.

This models:
- frame.cpp: Frame queuing, interpolation, fade logic, pitch ramping
- speechWaveGenerator.cpp: Synthesis chain (for optional audio output)
- pack.cpp: Language YAML loading with all ~120 settings

Usage:
  python formant_trajectory.py --packs /path/to/packs --lang en-us --text "hello world" --out trajectory.png
  python formant_trajectory.py --packs /path/to/packs --lang hu --ipa "həˈləʊ" --out trajectory.png --wav out.wav
"""

from __future__ import annotations

import argparse
import math
import subprocess
import sys
import wave
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable, Optional

# Ensure UTF-8 output on Windows consoles (IPA characters etc.)
if sys.stdout and hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
if sys.stderr and hasattr(sys.stderr, "reconfigure"):
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

import numpy as np

# Import the comprehensive language pack parser
from lang_pack import (
    load_pack_set, PackSet, LanguagePack, PhonemeDef,
    FIELD_NAMES as FRAME_PARAM_NAMES, FIELD_ID, FRAME_FIELD_COUNT,
    format_pack_summary,
)

# Optional matplotlib for visualization
try:
    import matplotlib.pyplot as plt
    import matplotlib.patches as mpatches
    HAS_MATPLOTLIB = True
except ImportError:
    HAS_MATPLOTLIB = False


FRAME_PARAM_COUNT = FRAME_FIELD_COUNT


# =============================================================================
# Frame structure (mirrors frame.h)
# =============================================================================

@dataclass
class Frame:
    """Mirrors speechPlayer_frame_t from frame.h"""
    voicePitch: float = 0.0
    vibratoPitchOffset: float = 0.0
    vibratoSpeed: float = 0.0
    voiceTurbulenceAmplitude: float = 0.0
    glottalOpenQuotient: float = 0.0
    voiceAmplitude: float = 0.0
    aspirationAmplitude: float = 0.0
    cf1: float = 0.0
    cf2: float = 0.0
    cf3: float = 0.0
    cf4: float = 0.0
    cf5: float = 0.0
    cf6: float = 0.0
    cfN0: float = 0.0
    cfNP: float = 0.0
    cb1: float = 0.0
    cb2: float = 0.0
    cb3: float = 0.0
    cb4: float = 0.0
    cb5: float = 0.0
    cb6: float = 0.0
    cbN0: float = 0.0
    cbNP: float = 0.0
    caNP: float = 0.0
    fricationAmplitude: float = 0.0
    pf1: float = 0.0
    pf2: float = 0.0
    pf3: float = 0.0
    pf4: float = 0.0
    pf5: float = 0.0
    pf6: float = 0.0
    pb1: float = 0.0
    pb2: float = 0.0
    pb3: float = 0.0
    pb4: float = 0.0
    pb5: float = 0.0
    pb6: float = 0.0
    pa1: float = 0.0
    pa2: float = 0.0
    pa3: float = 0.0
    pa4: float = 0.0
    pa5: float = 0.0
    pa6: float = 0.0
    parallelBypass: float = 0.0
    preFormantGain: float = 0.0
    outputGain: float = 0.0
    endVoicePitch: float = 0.0

    def get_param(self, idx: int) -> float:
        return getattr(self, FRAME_PARAM_NAMES[idx])

    def set_param(self, idx: int, val: float):
        setattr(self, FRAME_PARAM_NAMES[idx], val)

    def copy(self) -> "Frame":
        f = Frame()
        for name in FRAME_PARAM_NAMES:
            setattr(f, name, getattr(self, name))
        return f

    def to_array(self) -> np.ndarray:
        return np.array([getattr(self, name) for name in FRAME_PARAM_NAMES], dtype=np.float64)

    @staticmethod
    def from_array(arr: np.ndarray) -> "Frame":
        f = Frame()
        for i, name in enumerate(FRAME_PARAM_NAMES):
            setattr(f, name, float(arr[i]))
        return f


# =============================================================================
# Frame Request (mirrors frameRequest_t from frame.cpp)
# =============================================================================

@dataclass
class FrameRequest:
    min_num_samples: int = 0
    num_fade_samples: int = 0
    is_null: bool = True
    frame: Frame = field(default_factory=Frame)
    voice_pitch_inc: float = 0.0
    user_index: int = -1
    label: str = ""  # For visualization

    # FrameEx voice quality params (DSP v5+)
    has_frame_ex: bool = False
    frame_ex: dict = field(default_factory=lambda: _default_frame_ex())

    # Formant end targets for exponential smoothing (DECTalk-style)
    # NaN = no ramping for that formant
    end_cf1: float = float('nan')
    end_cf2: float = float('nan')
    end_cf3: float = float('nan')
    end_pf1: float = float('nan')
    end_pf2: float = float('nan')
    end_pf3: float = float('nan')
    formant_alpha: float = 0.0


def _default_frame_ex() -> dict:
    """Mirrors speechPlayer_frameEx_defaults from frame.h"""
    return {
        "creakiness": 0.0,
        "breathiness": 0.0,
        "jitter": 0.0,
        "shimmer": 0.0,
        "sharpness": 0.0,
        "endCf1": float('nan'),
        "endCf2": float('nan'),
        "endCf3": float('nan'),
        "endPf1": float('nan'),
        "endPf2": float('nan'),
        "endPf3": float('nan'),
        "fujisakiEnabled": 0.0,
        "fujisakiReset": 0.0,
        "fujisakiPhraseAmp": 0.0,
        "fujisakiPhraseLen": 0.0,
        "fujisakiAccentAmp": 0.0,
        "fujisakiAccentDur": 0.0,
        "fujisakiAccentLen": 0.0,
    }


# Index of first Fujisaki field — these get stepped, not interpolated
_FRAME_EX_KEYS = list(_default_frame_ex().keys())
_FUJISAKI_START_IDX = _FRAME_EX_KEYS.index("fujisakiEnabled")

# Exponential smoothing alpha for formant ramping (~10-15ms time constant)
_FORMANT_ALPHA = 0.004


# =============================================================================
# Place of Articulation (port of pass_common.h getPlace)
# =============================================================================

class Place:
    UNKNOWN = 0
    LABIAL = 1
    ALVEOLAR = 2
    PALATAL = 3
    VELAR = 4

_LABIALS = frozenset(["p", "b", "m", "f", "v", "w", "\u028d", "\u0278", "\u03b2"])
_ALVEOLARS = frozenset([
    "t", "d", "n", "s", "z", "l", "r", "\u0279", "\u027e",
    "\u03b8", "\u00f0", "\u026c", "\u026e", "\u027b",
    "\u0256", "\u0288", "\u0273", "\u027d",
])
_PALATALS = frozenset([
    "\u0283", "\u0292",                           # ʃ ʒ
    "t\u0283", "d\u0292",                         # tʃ dʒ
    "t\u0361\u0283", "d\u0361\u0292",             # t͡ʃ d͡ʒ
    "j", "\u0272", "\u00e7", "\u029d",            # j ɲ ç ʝ
    "c", "\u025f", "\u028e",                      # c ɟ ʎ
])
_VELARS = frozenset(["k", "g", "\u014b", "x", "\u0263", "\u0270"])


def get_place(key: str) -> int:
    """Determine place of articulation from phoneme key (IPA string)."""
    if key in _LABIALS:
        return Place.LABIAL
    if key in _ALVEOLARS:
        return Place.ALVEOLAR
    if key in _PALATALS:
        return Place.PALATAL
    if key in _VELARS:
        return Place.VELAR
    return Place.UNKNOWN


# =============================================================================
# Emission Token (lightweight carrier for micro-frame dispatch)
# =============================================================================

@dataclass
class EmissionToken:
    """Lightweight token for micro-frame emission dispatch.

    Mirrors the subset of C++ Token fields needed by frame_emit.cpp.
    Built by build_tokens() from IPA tokenization + pack lookup.
    """
    pdef: Optional[PhonemeDef] = None
    frame: Optional[Frame] = None

    # Timing
    duration_ms: float = 0.0
    fade_ms: float = 10.0

    # Label for visualization
    label: str = ""

    # Stress level (0=none, 1=primary, 2=secondary)
    stress: int = 0

    # Flags
    silence: bool = False
    pre_stop_gap: bool = False
    voiced_closure: bool = False
    post_stop_aspiration: bool = False
    coda_fric_stop_blend: bool = False
    word_start: bool = False
    is_diphthong_glide: bool = False

    # Diphthong end targets (set by collapse_diphthongs)
    has_end_cf1: bool = False
    has_end_cf2: bool = False
    has_end_cf3: bool = False
    end_cf1: float = 0.0
    end_cf2: float = 0.0
    end_cf3: float = 0.0
    has_end_pf1: bool = False
    has_end_pf2: bool = False
    has_end_pf3: bool = False
    end_pf1: float = 0.0
    end_pf2: float = 0.0
    end_pf3: float = 0.0
    has_end_cb1: bool = False
    has_end_cb2: bool = False
    has_end_cb3: bool = False
    end_cb1: float = 0.0
    end_cb2: float = 0.0
    end_cb3: float = 0.0
    has_end_pb1: bool = False
    has_end_pb2: bool = False
    has_end_pb3: bool = False
    end_pb1: float = 0.0
    end_pb2: float = 0.0
    end_pb3: float = 0.0


# =============================================================================
# Adaptive Onset Hold (port of frame_emit.cpp adaptiveOnsetHold)
# =============================================================================

def adaptive_onset_hold(
    base_hold: float,
    start_cf1: float, end_cf1: float,
    start_cf2: float, end_cf2: float,
    total_dur_ms: float,
    next_token: Optional[EmissionToken],
) -> float:
    """Scale diphthong onset hold exponent based on sweep width and context.

    Wide formant sweeps need earlier motion onset. Following stops/silence
    need the offglide to complete in-token. Minimum ~40ms offglide enforced.
    Port of frame_emit.cpp lines 47-98.
    """
    hold = base_hold
    if hold <= 1.0:
        return hold

    # 1. Sweep-width scaling
    sweep_f2 = abs(end_cf2 - start_cf2)
    sweep_f1 = abs(end_cf1 - start_cf1)
    sweep_max = max(sweep_f2, sweep_f1)

    if sweep_max > 300.0:
        width_frac = min((sweep_max - 300.0) / 300.0, 1.0)
        hold = hold + (1.0 - hold) * width_frac

    # 2. Following-context check
    next_is_abrupt = False
    if next_token is None or next_token.silence or next_token.pre_stop_gap:
        next_is_abrupt = True
    elif next_token.pdef and next_token.pdef.is_stop:
        next_is_abrupt = True

    # 3. Minimum offglide duration (~40ms)
    min_offglide_ms = 40.0
    if (total_dur_ms > min_offglide_ms and hold > 1.0
            and (next_is_abrupt or sweep_max > 400.0)):
        sweep_start = pow(0.15, 1.0 / max(hold, 1.001))
        effective_sweep_ms = total_dur_ms * (1.0 - sweep_start)
        if effective_sweep_ms < min_offglide_ms:
            target_start = 1.0 - (min_offglide_ms / total_dur_ms)
            if 0.01 < target_start < 0.99:
                hold = math.log(0.15) / math.log(target_start)
                if hold < 1.0:
                    hold = 1.0

    return hold


# =============================================================================
# Frame Manager (accurate port of frame.cpp)
# =============================================================================

def lerp(old: float, new: float, ratio: float) -> float:
    """calculateValueAtFadePosition from utils.h"""
    return old + (new - old) * ratio


def cosine_smooth(ratio: float) -> float:
    """cosineSmooth from utils.h — S-curve easing for spectral params."""
    return 0.5 * (1.0 - math.cos(math.pi * ratio))


def freq_lerp(old: float, new: float, ratio: float) -> float:
    """calculateFreqAtFadePosition from utils.h — log-domain interpolation for Hz values.
    Falls back to linear lerp if either value is <= 0."""
    if old <= 0.0 or new <= 0.0:
        return lerp(old, new, ratio)
    log_old = math.log(old)
    log_new = math.log(new)
    return math.exp(log_old + (log_new - log_old) * ratio)


def is_frequency_param(idx: int) -> bool:
    """Mirrors isFrequencyParam() from frame.cpp — identifies Hz-valued parameters
    that need log-domain interpolation. Everything else gets linear."""
    name = FRAME_PARAM_NAMES[idx]
    # voicePitch and endVoicePitch
    if name in ("voicePitch", "endVoicePitch"):
        return True
    # Cascade formant frequencies: cf1 through cfNP
    if name.startswith("cf"):
        return True
    # Parallel formant frequencies: pf1 through pf6
    if name.startswith("pf"):
        return True
    return False


# Precompute the frequency param set for hot-loop performance
_FREQ_PARAM_SET = frozenset(i for i in range(FRAME_FIELD_COUNT) if is_frequency_param(i))


class FrameManager:
    """
    Accurate Python port of FrameManagerImpl from frame.cpp.
    
    This handles:
    - Frame queuing with duration (minNumSamples) and fade (numFadeSamples)
    - Dual interpolation during fades: log-domain + cosine easing for frequency
      params (voicePitch, cf1-cfNP, pf1-pf6), linear for everything else
    - FrameEx interpolation with Fujisaki trigger step-through
    - Pitch ramping via voicePitchInc during hold phase
    - Exponential formant smoothing (endCf1/2/3, endPf1/2/3) during hold phase
    - NULL frame handling (silence transitions)
    """
    
    def __init__(self):
        self.frame_queue: list[FrameRequest] = []
        self.old_request: FrameRequest = FrameRequest()
        self.new_request: Optional[FrameRequest] = None
        self.cur_frame: Frame = Frame()
        self.cur_frame_ex: dict = _default_frame_ex()
        self.cur_frame_is_null: bool = True
        self.cur_has_frame_ex: bool = False
        self.sample_counter: int = 0
        self.last_user_index: int = -1

    def queue_frame(
        self,
        frame: Optional[Frame],
        min_num_samples: int,
        num_fade_samples: int,
        user_index: int = -1,
        purge_queue: bool = False,
        label: str = "",
        frame_ex: Optional[dict] = None,
    ):
        """Queue a frame for synthesis.
        
        frame_ex: optional dict with FrameEx voice quality params (creakiness,
                  breathiness, endCf1, fujisakiEnabled, etc.)
        """
        req = FrameRequest()
        req.min_num_samples = min_num_samples
        # Enforce minimum of 1 to prevent divide-by-zero (matches C++)
        req.num_fade_samples = max(num_fade_samples, 1)
        req.user_index = user_index
        req.label = label

        if frame is not None:
            req.is_null = False
            req.frame = frame.copy()
            if min_num_samples > 0:
                req.voice_pitch_inc = (frame.endVoicePitch - frame.voicePitch) / min_num_samples
            else:
                req.voice_pitch_inc = 0.0
        else:
            req.is_null = True
            req.frame = Frame()
            req.voice_pitch_inc = 0.0

        # FrameEx handling
        if frame_ex is not None:
            req.has_frame_ex = True
            req.frame_ex = {**_default_frame_ex(), **frame_ex}
            # Extract formant end targets for exponential smoothing
            has_any_target = False
            for attr, key in [("end_cf1", "endCf1"), ("end_cf2", "endCf2"),
                              ("end_cf3", "endCf3"), ("end_pf1", "endPf1"),
                              ("end_pf2", "endPf2"), ("end_pf3", "endPf3")]:
                v = req.frame_ex.get(key, float('nan'))
                if math.isfinite(v):
                    setattr(req, attr, v)
                    has_any_target = True
            if has_any_target:
                req.formant_alpha = _FORMANT_ALPHA

        if purge_queue:
            self.frame_queue.clear()
            self.sample_counter = self.old_request.min_num_samples
            if not self.cur_frame_is_null:
                self.old_request.is_null = False
                self.old_request.frame = self.cur_frame.copy()
                self.old_request.has_frame_ex = self.cur_has_frame_ex
                self.old_request.frame_ex = dict(self.cur_frame_ex)
            if self.new_request is not None:
                self.new_request = None

        self.frame_queue.append(req)

    def get_current_frame(self) -> Optional[Frame]:
        """
        Advance one sample and return the current interpolated frame.
        Returns None if in silence.
        """
        self._update_current_frame()
        return None if self.cur_frame_is_null else self.cur_frame

    def get_current_frame_ex(self) -> Optional[dict]:
        """Return the current FrameEx dict, or None if no FrameEx is active."""
        return dict(self.cur_frame_ex) if self.cur_has_frame_ex else None

    def _update_current_frame(self):
        self.sample_counter += 1

        if self.new_request is not None:
            # === Branch 1: During fade between old and new ===
            if self.sample_counter > self.new_request.num_fade_samples:
                # Fade complete — snap to new frame
                self.old_request = self.new_request
                self.new_request = None
                self.cur_frame = self.old_request.frame.copy()
                self.cur_frame_ex = dict(self.old_request.frame_ex)
                self.cur_has_frame_ex = self.old_request.has_frame_ex
            else:
                # Interpolate frame params: log-domain + cosine for frequencies,
                # linear for amplitudes/bandwidths/gains
                linear_ratio = self.sample_counter / self.new_request.num_fade_samples
                cosine_ratio = cosine_smooth(linear_ratio)
                for i in range(FRAME_PARAM_COUNT):
                    old_val = self.old_request.frame.get_param(i)
                    new_val = self.new_request.frame.get_param(i)
                    if i in _FREQ_PARAM_SET:
                        self.cur_frame.set_param(i, freq_lerp(old_val, new_val, cosine_ratio))
                    else:
                        self.cur_frame.set_param(i, lerp(old_val, new_val, linear_ratio))

                # Interpolate FrameEx params
                if self.old_request.has_frame_ex or self.new_request.has_frame_ex:
                    self.cur_has_frame_ex = True
                    for j, key in enumerate(_FRAME_EX_KEYS):
                        if j >= _FUJISAKI_START_IDX:
                            # Fujisaki triggers: step immediately to new values
                            self.cur_frame_ex[key] = self.new_request.frame_ex[key]
                        else:
                            # Voice quality params: linear interpolation
                            old_v = self.old_request.frame_ex[key]
                            new_v = self.new_request.frame_ex[key]
                            # Skip NaN-valued end targets (they're not interpolatable)
                            if math.isfinite(old_v) and math.isfinite(new_v):
                                self.cur_frame_ex[key] = lerp(old_v, new_v, linear_ratio)
                            else:
                                self.cur_frame_ex[key] = new_v
                else:
                    self.cur_has_frame_ex = False
                    self.cur_frame_ex = _default_frame_ex()

        elif self.sample_counter > self.old_request.min_num_samples:
            # === Branch 3: Hold expired — pop next frame from queue ===
            if self.frame_queue:
                was_from_silence = self.cur_frame_is_null or self.old_request.is_null
                self.cur_frame_is_null = False
                self.new_request = self.frame_queue.pop(0)

                if self.new_request.is_null:
                    # Transitioning to silence — copy old frame, zero gain
                    self.new_request.frame = self.old_request.frame.copy()
                    self.new_request.frame.preFormantGain = 0.0
                    self.new_request.frame.voicePitch = self.cur_frame.voicePitch
                    self.new_request.voice_pitch_inc = 0.0
                    # Carry FrameEx through silence fades
                    self.new_request.frame_ex = dict(self.old_request.frame_ex)
                    self.new_request.has_frame_ex = self.old_request.has_frame_ex
                elif self.old_request.is_null:
                    # Transitioning from silence — copy new frame, zero gain on old
                    self.old_request.frame = self.new_request.frame.copy()
                    self.old_request.frame.preFormantGain = 0.0
                    self.old_request.is_null = False
                    self.old_request.frame_ex = dict(self.new_request.frame_ex)
                    self.old_request.has_frame_ex = self.new_request.has_frame_ex

                if self.new_request is not None:
                    if self.new_request.user_index != -1:
                        self.last_user_index = self.new_request.user_index
                    self.sample_counter = 0
                    # On from-silence: snap curFrame to old (which has preFormantGain=0)
                    if was_from_silence:
                        self.cur_frame = self.old_request.frame.copy()
                        self.cur_frame_ex = dict(self.old_request.frame_ex)
                        self.cur_has_frame_ex = self.old_request.has_frame_ex
                    # Apply pitch increment over fade
                    self.new_request.frame.voicePitch += (
                        self.new_request.voice_pitch_inc * self.new_request.num_fade_samples
                    )
            else:
                # No more frames — go to silence
                self.cur_frame_is_null = True
                self.old_request.is_null = True
                self.cur_has_frame_ex = False
                self.cur_frame_ex = _default_frame_ex()
        else:
            # === Branch 2: Still within current frame hold ===
            # Per-sample linear pitch ramping
            self.cur_frame.voicePitch += self.old_request.voice_pitch_inc
            self.old_request.frame.voicePitch = self.cur_frame.voicePitch

            # Per-sample exponential formant ramping (DECTalk-style)
            alpha = self.old_request.formant_alpha
            if alpha > 0:
                for attr, frame_field in [
                    ("end_cf1", "cf1"), ("end_cf2", "cf2"), ("end_cf3", "cf3"),
                    ("end_pf1", "pf1"), ("end_pf2", "pf2"), ("end_pf3", "pf3"),
                ]:
                    target = getattr(self.old_request, attr)
                    if math.isfinite(target):
                        cur = getattr(self.cur_frame, frame_field)
                        new_val = cur + alpha * (target - cur)
                        setattr(self.cur_frame, frame_field, new_val)
                        setattr(self.old_request.frame, frame_field, new_val)


# =============================================================================
# Trajectory Recorder
# =============================================================================

@dataclass
class TrajectoryPoint:
    time_ms: float
    frame: Frame
    label: str
    is_silence: bool
    frame_ex: Optional[dict] = None


class TrajectoryRecorder:
    """
    Wraps FrameManager to record formant trajectories at specified resolution.
    """

    def __init__(self, sample_rate: int = 16000, resolution_ms: float = 1.0):
        self.sample_rate = sample_rate
        self.resolution_ms = resolution_ms
        self.fm = FrameManager()
        self.points: list[TrajectoryPoint] = []

    def queue_frame(
        self,
        frame: Optional[Frame],
        duration_ms: float,
        fade_ms: float,
        label: str = "",
        frame_ex: Optional[dict] = None,
    ):
        """Queue a frame with timing in milliseconds."""
        min_samples = int(duration_ms * self.sample_rate / 1000.0)
        fade_samples = int(fade_ms * self.sample_rate / 1000.0)
        self.fm.queue_frame(frame, min_samples, fade_samples, label=label,
                            frame_ex=frame_ex)

    def run(self) -> list[TrajectoryPoint]:
        """
        Run the frame manager and record trajectories.
        Returns list of TrajectoryPoint at the specified resolution.
        """
        self.points = []
        samples_per_point = int(self.resolution_ms * self.sample_rate / 1000.0)
        if samples_per_point < 1:
            samples_per_point = 1

        sample_idx = 0
        time_ms = 0.0

        # Keep running until we hit silence
        silence_count = 0
        max_silence = int(50 * self.sample_rate / 1000.0)  # 50ms of silence to stop

        while silence_count < max_silence:
            f = self.fm.get_current_frame()

            if f is None:
                silence_count += 1
            else:
                silence_count = 0

            # Record at resolution intervals
            if sample_idx % samples_per_point == 0:
                pt = TrajectoryPoint(
                    time_ms=time_ms,
                    frame=f.copy() if f else Frame(),
                    label=self.fm.old_request.label if self.fm.old_request else "",
                    is_silence=(f is None),
                    frame_ex=self.fm.get_current_frame_ex(),
                )
                self.points.append(pt)

            sample_idx += 1
            time_ms = sample_idx * 1000.0 / self.sample_rate

        return self.points


# =============================================================================
# Synthesis (simplified, for audio preview)
# =============================================================================

class NoiseGenerator:
    def __init__(self, seed: int = 0):
        self.rng = np.random.RandomState(seed)
        self.last_value = 0.0

    def reset(self):
        self.last_value = 0.0

    def get_next(self) -> float:
        x = self.rng.random() - 0.5
        self.last_value = x + 0.75 * self.last_value
        return self.last_value


class FrequencyGenerator:
    def __init__(self, sample_rate: int):
        self.sample_rate = sample_rate
        self.last_cycle_pos = 0.0

    def reset(self):
        self.last_cycle_pos = 0.0

    def get_next(self, frequency: float) -> float:
        if frequency <= 0:
            return self.last_cycle_pos
        cycle_pos = ((frequency / self.sample_rate) + self.last_cycle_pos) % 1.0
        self.last_cycle_pos = cycle_pos
        return cycle_pos


class Resonator:
    """Matches src/resonator.h: bilinear-transform DF1 (all-pole) + FIR (anti)."""

    def __init__(self, sample_rate: int, anti: bool = False):
        self.sample_rate = sample_rate
        self.anti = anti
        self.frequency = 0.0
        self.bandwidth = 0.0
        self.disabled = True
        self.set_once = False

        # All-pole resonator: DF1 output history and coefficients
        self.y1 = 0.0
        self.y2 = 0.0
        self.dfB0 = 0.0
        self.dfFb1 = 0.0
        self.dfFb2 = 0.0

        # FIR anti-resonator state and coefficients
        self.firA = 1.0
        self.firB = 0.0
        self.firC = 0.0
        self.z1 = 0.0
        self.z2 = 0.0

    def reset(self):
        self.y1 = 0.0
        self.y2 = 0.0
        self.z1 = 0.0
        self.z2 = 0.0
        self.set_once = False

    def decay(self, factor: float):
        """Drain residual energy during silence (matches resonator.h:148)."""
        self.y1 *= factor
        self.y2 *= factor

    def set_params(self, frequency: float, bandwidth: float):
        if (not self.set_once) or (frequency != self.frequency) or (bandwidth != self.bandwidth):
            self.frequency = frequency
            self.bandwidth = bandwidth

            nyquist = 0.5 * self.sample_rate
            invalid = not (math.isfinite(frequency) and math.isfinite(bandwidth))
            off = (frequency <= 0.0 or bandwidth <= 0.0 or frequency >= nyquist)

            if invalid or off:
                self.disabled = True
                if self.anti:
                    self.firA = 1.0; self.firB = 0.0; self.firC = 0.0
                else:
                    self.dfB0 = 0.0; self.dfFb1 = 0.0; self.dfFb2 = 0.0
                self.set_once = True
                return

            self.disabled = False

            if self.anti:
                # FIR anti-resonator (resonator.h:65-82)
                r = math.exp(-math.pi / self.sample_rate * bandwidth)
                cos_theta = math.cos(2.0 * math.pi * frequency / self.sample_rate)
                res_a = 1.0 - 2.0 * r * cos_theta + r * r
                if not math.isfinite(res_a) or abs(res_a) < 1e-12:
                    self.firA = 1.0; self.firB = 0.0; self.firC = 0.0
                else:
                    inv_a = 1.0 / res_a
                    self.firA = inv_a
                    self.firB = -2.0 * r * cos_theta * inv_a
                    self.firC = r * r * inv_a
            else:
                # Bilinear-transform all-pole (resonator.h:83-109)
                g = math.tan(math.pi * frequency / self.sample_rate)
                g2 = g * g
                R = math.exp(-2.0 * math.pi * bandwidth / self.sample_rate)
                k = (1.0 - R) * (1.0 + g2) / (g * (1.0 + R))
                D = 1.0 + k * g + g2
                self.dfB0 = 4.0 * g2 / D
                self.dfFb1 = 2.0 * (1.0 - g2) / D
                self.dfFb2 = -(1.0 - k * g + g2) / D

            self.set_once = True

    def resonate(self, inp: float, frequency: float, bandwidth: float) -> float:
        self.set_params(frequency, bandwidth)

        if self.disabled:
            return inp

        if self.anti:
            out = self.firA * inp + self.firB * self.z1 + self.firC * self.z2
            self.z2 = self.z1
            self.z1 = inp
            return out
        else:
            out = self.dfB0 * inp + self.dfFb1 * self.y1 + self.dfFb2 * self.y2
            self.y2 = self.y1
            self.y1 = out
            return out


class OnePoleLowpass:
    """One-pole lowpass filter matching dspCommon.h:223-245."""

    def __init__(self, sample_rate: int):
        self.sr = sample_rate
        self.alpha = 0.0
        self.z = 0.0

    def set_cutoff_hz(self, fc: float):
        fc = max(fc, 10.0)
        fc = min(fc, 0.95 * 0.5 * self.sr)
        self.alpha = math.exp(-2.0 * math.pi * fc / self.sr)

    def process(self, x: float) -> float:
        self.z = (1.0 - self.alpha) * x + self.alpha * self.z
        return self.z

    def reset(self):
        self.z = 0.0


class SimpleSynthesizer:
    """
    Synthesizer for audio preview.
    Mirrors the essential parts of speechWaveGenerator.cpp + voiceGenerator.h.
    """

    # Tuning constants from voiceGenerator.h / dspCommon.h
    K_VOICING_PEAK_POS = 0.91        # voicingPeakPos default
    K_SPEED_QUOTIENT = 2.0           # default SQ (no peak shift at 2.0)
    K_FLOW_SCALE = 1.6               # voiceGenerator.h:853
    K_DERIV_SATURATION = 0.6         # voiceGenerator.h:882
    K_TURBULENCE_FLOW_POWER = 1.5    # voiceGenerator.h:914
    K_VOICED_PRE_EMPH_A = 0.92       # voiceGenerator.h:294
    K_VOICED_PRE_EMPH_MIX = 0.35     # voiceGenerator.h:294
    K_DC_POLE = 0.9995               # voiceGenerator.h:948
    K_FRIC_NOISE_SCALE = 0.175
    K_RADIATION_DERIV_GAIN_BASE = 5.0       # dspCommon.h:76
    K_RADIATION_DERIV_GAIN_REF_SR = 22050.0 # dspCommon.h:77

    def __init__(self, sample_rate: int = 16000):
        self.sample_rate = sample_rate
        self.pitch_gen = FrequencyGenerator(sample_rate)
        self.vibrato_gen = FrequencyGenerator(sample_rate)
        self.asp_gen = NoiseGenerator(0)
        self.fric_gen = NoiseGenerator(1)

        self.cascade = [Resonator(sample_rate) for _ in range(6)]
        self.nasal_zero = Resonator(sample_rate, anti=True)
        self.nasal_pole = Resonator(sample_rate)

        self.parallel = [Resonator(sample_rate) for _ in range(6)]

        # Glottal source state
        self.last_flow = 0.0
        self.last_voiced_src = 0.0  # for pre-emphasis
        self.glottis_open = False

        # DC blocker state (voiced path + final output)
        self.last_voiced_in = 0.0
        self.last_voiced_out = 0.0
        self.last_input = 0.0
        self.last_output = 0.0

        # Radiation: SR-scaled derivative gain (dspCommon.h:76-77)
        self.radiation_deriv_gain = (self.K_RADIATION_DERIV_GAIN_BASE *
                                     (sample_rate / self.K_RADIATION_DERIV_GAIN_REF_SR))
        # Baseline radiation mix at tilt=0 (voiceGenerator.h:172-175)
        self.radiation_mix = 0.30 * min(1.0, sample_rate / 16000.0)

        # LF closing-phase base sharpness (voiceGenerator.h:783-794)
        if sample_rate >= 44100:
            self.lf_base_sharpness = 10.0
        elif sample_rate >= 32000:
            self.lf_base_sharpness = 8.0
        elif sample_rate >= 22050:
            self.lf_base_sharpness = 4.0
        elif sample_rate >= 16000:
            self.lf_base_sharpness = 3.0
        else:
            self.lf_base_sharpness = 2.5

        # LF/cosine blend ratio (voiceGenerator.h:820-828)
        if sample_rate <= 11025:
            self.lf_blend = 0.30
        elif sample_rate >= 16000:
            self.lf_blend = 1.0
        else:
            self.lf_blend = 0.30 + 0.70 * (sample_rate - 11025) / (16000.0 - 11025.0)

        # Anti-alias 2-pole lowpass (voiceGenerator.h:328-349)
        self.aa_lp1 = OnePoleLowpass(sample_rate)
        self.aa_lp2 = OnePoleLowpass(sample_rate)
        if sample_rate < 44100:
            self.aa_active = True
            if sample_rate <= 11025:
                aa_fc = 4000.0
            elif sample_rate <= 16000:
                t = (sample_rate - 11025) / (16000.0 - 11025.0)
                aa_fc = 4000.0 + t * 1000.0
            else:
                t = (sample_rate - 16000) / (22050.0 - 16000.0)
                aa_fc = 5000.0 + t * 1500.0
                if t > 1.0:
                    aa_fc = 6500.0
            self.aa_lp1.set_cutoff_hz(aa_fc)
            self.aa_lp2.set_cutoff_hz(aa_fc)
        else:
            self.aa_active = False

        # Aspiration amplitude smoothing (voiceGenerator.h:960-976)
        self.smooth_asp_amp = 0.0
        self.smooth_asp_amp_init = False
        asp_attack_ms = 1.0
        asp_release_ms = 12.0
        self.asp_attack_coeff = 1.0 - math.exp(-1.0 / (0.001 * asp_attack_ms * sample_rate))
        self.asp_release_coeff = 1.0 - math.exp(-1.0 / (0.001 * asp_release_ms * sample_rate))

        # Pre-formant gain smoothing
        self.smooth_pre_gain = 0.0
        attack_ms = 1.0
        release_ms = 0.5
        self.pre_gain_attack_alpha = 1.0 - math.exp(-1.0 / (sample_rate * attack_ms * 0.001))
        self.pre_gain_release_alpha = 1.0 - math.exp(-1.0 / (sample_rate * release_ms * 0.001))

    def reset(self):
        self.pitch_gen.reset()
        self.vibrato_gen.reset()
        self.asp_gen.reset()
        self.fric_gen.reset()
        for r in self.cascade:
            r.reset()
        self.nasal_zero.reset()
        self.nasal_pole.reset()
        for r in self.parallel:
            r.reset()
        self.last_flow = 0.0
        self.last_voiced_src = 0.0
        self.last_voiced_in = 0.0
        self.last_voiced_out = 0.0
        self.last_input = 0.0
        self.last_output = 0.0
        self.glottis_open = False
        self.aa_lp1.reset()
        self.aa_lp2.reset()
        self.smooth_asp_amp = 0.0
        self.smooth_asp_amp_init = False
        self.smooth_pre_gain = 0.0

    def generate_sample(self, f: Frame, frame_ex: Optional[dict] = None) -> float:
        """Generate one audio sample from a frame.

        Matches voiceGenerator.h hybrid LF glottal model + radiation chain.
        frame_ex: optional dict with 'breathiness', 'creakiness' etc.
        """
        sr = self.sample_rate

        # Extract voice quality from FrameEx
        breathiness = 0.0
        creakiness = 0.0
        if frame_ex:
            breathiness = max(0.0, min(1.0, frame_ex.get("breathiness", 0.0)))
            creakiness = max(0.0, min(1.0, frame_ex.get("creakiness", 0.0)))

        # Perceptual curve for breathiness (voiceGenerator.h:477-478)
        if breathiness > 0.0:
            breathiness = breathiness ** 0.55

        # Vibrato
        vibrato = (math.sin(self.vibrato_gen.get_next(f.vibratoSpeed) * 2 * math.pi) *
                   0.06 * f.vibratoPitchOffset) + 1.0
        pitch_hz = f.voicePitch * vibrato

        # Creakiness lowers F0 (voiceGenerator.h:591-593)
        if creakiness > 0.0:
            pitch_hz *= (1.0 - 0.12 * creakiness)

        cycle_pos = self.pitch_gen.get_next(pitch_hz if pitch_hz > 0 else 0)

        # Aspiration noise (base gain 0.1, breathiness lifts to 0.25)
        asp_base = 0.10 + (0.15 * breathiness)
        aspiration = self.asp_gen.get_next() * asp_base

        # ----------------------------------------------------------------
        # Open quotient with breathiness/creakiness modulation
        # (voiceGenerator.h:655-687)
        # ----------------------------------------------------------------
        effective_oq = f.glottalOpenQuotient
        if effective_oq <= 0:
            effective_oq = 0.4
        effective_oq = max(0.10, min(0.95, effective_oq))

        if creakiness > 0.0:
            effective_oq += 0.10 * creakiness
            effective_oq = min(effective_oq, 0.95)
        if breathiness > 0.0:
            effective_oq -= 0.35 * breathiness
            effective_oq = max(effective_oq, 0.05)

        self.glottis_open = (pitch_hz > 0) and (cycle_pos >= effective_oq)

        # ----------------------------------------------------------------
        # Hybrid LF glottal flow (voiceGenerator.h:716-854)
        # ----------------------------------------------------------------
        flow = 0.0
        if self.glottis_open:
            open_len = 1.0 - effective_oq
            if open_len < 0.0001:
                open_len = 0.0001

            # Peak position with breathiness/creakiness modulation
            peak_pos = self.K_VOICING_PEAK_POS + (0.02 * breathiness) - (0.05 * creakiness)

            dt = pitch_hz / sr if pitch_hz > 0 else 0
            denom = max(0.0001, open_len - dt)
            phase = max(0.0, min(1.0, (cycle_pos - effective_oq) / denom))

            # Min closure sample clamping (voiceGenerator.h:734-742)
            if pitch_hz > 0.0:
                period_samples = sr / pitch_hz
                min_close_frac = 2.0 / (period_samples * open_len)
                min_close_frac = min(min_close_frac, 0.5)
                limit_peak = 1.0 - min_close_frac
                if limit_peak < peak_pos:
                    peak_pos = limit_peak
                if peak_pos < 0.50:
                    peak_pos = 0.50

            # Symmetric cosine component (voiceGenerator.h:753-758)
            if phase < peak_pos:
                flow_cosine = 0.5 * (1.0 - math.cos(phase * math.pi / peak_pos))
            else:
                flow_cosine = 0.5 * (1.0 + math.cos(
                    (phase - peak_pos) * math.pi / (1.0 - peak_pos)))

            # LF-inspired asymmetric component (voiceGenerator.h:766-813)
            if phase < peak_pos:
                # Opening: polynomial rise with modified smoothstep
                t = phase / peak_pos
                # openPower = 2.0 at default SQ=2.0
                open_power = 2.0
                t_pow = t ** open_power
                flow_lf = t_pow * (3.0 - 2.0 * t)
            else:
                # Closing: sharp fall with SR-dependent sharpness
                t = (phase - peak_pos) / (1.0 - peak_pos)
                # sqFactor = 1.0 at default SQ=2.0
                sharpness = self.lf_base_sharpness
                flow_lf = (1.0 - t) ** sharpness

            # Blend LF and cosine (voiceGenerator.h:820-850)
            flow = (1.0 - self.lf_blend) * flow_cosine + self.lf_blend * flow_lf

        flow *= self.K_FLOW_SCALE

        # ----------------------------------------------------------------
        # Radiation derivative with tanh saturation (voiceGenerator.h:856-889)
        # ----------------------------------------------------------------
        d_flow = flow - self.last_flow
        self.last_flow = flow

        src_deriv = d_flow * self.radiation_deriv_gain
        kds = self.K_DERIV_SATURATION
        src_deriv = kds * math.tanh(src_deriv / kds) if kds > 0 else 0.0

        rm = self.radiation_mix
        voiced_src = (flow + rm * src_deriv) / (1.0 + rm * 0.5)

        # Pre-emphasis (voiceGenerator.h:892-894)
        pre = voiced_src - (self.K_VOICED_PRE_EMPH_A * self.last_voiced_src)
        self.last_voiced_src = voiced_src
        voiced_src = ((1.0 - self.K_VOICED_PRE_EMPH_MIX) * voiced_src +
                      self.K_VOICED_PRE_EMPH_MIX * pre)

        # ----------------------------------------------------------------
        # Breathiness: turbulence + voice amplitude (voiceGenerator.h:899-931)
        # ----------------------------------------------------------------
        voice_turb_amp = f.voiceTurbulenceAmplitude
        if breathiness > 0.0:
            voice_turb_amp = min(voice_turb_amp + 0.5 * breathiness, 1.0)

        turbulence = aspiration * voice_turb_amp
        if self.glottis_open:
            flow01 = max(0.0, min(1.0, flow / self.K_FLOW_SCALE))
            turbulence *= flow01 ** self.K_TURBULENCE_FLOW_POWER
        else:
            turbulence = 0.0

        voice_amp = max(0.0, min(1.0, f.voiceAmplitude))
        if creakiness > 0.0:
            voice_amp *= (1.0 - 0.35 * creakiness)
        if breathiness > 0.0:
            voice_amp *= (1.0 - 0.98 * breathiness)

        # Voice amp applied ONLY to voiced pulse, NOT turbulence
        voiced_in = (voiced_src * voice_amp) + turbulence

        # DC blocker (voiceGenerator.h:948-951)
        voiced = (voiced_in - self.last_voiced_in +
                  self.K_DC_POLE * self.last_voiced_out)
        self.last_voiced_in = voiced_in
        self.last_voiced_out = voiced

        # Anti-alias lowpass (voiceGenerator.h:956-958)
        if self.aa_active:
            voiced = self.aa_lp2.process(self.aa_lp1.process(voiced))

        # ----------------------------------------------------------------
        # Aspiration with breathiness boost (voiceGenerator.h:960-978)
        # ----------------------------------------------------------------
        target_asp_amp = max(0.0, min(1.0, f.aspirationAmplitude))
        if breathiness > 0.0:
            target_asp_amp = min(target_asp_amp + breathiness, 1.0)

        if not self.smooth_asp_amp_init:
            self.smooth_asp_amp = target_asp_amp
            self.smooth_asp_amp_init = True
        else:
            coeff = (self.asp_attack_coeff if target_asp_amp > self.smooth_asp_amp
                     else self.asp_release_coeff)
            self.smooth_asp_amp += (target_asp_amp - self.smooth_asp_amp) * coeff

        asp_out = aspiration * self.smooth_asp_amp

        # PreFormant gain smoothing
        target = f.preFormantGain
        alpha = (self.pre_gain_attack_alpha if target > self.smooth_pre_gain
                 else self.pre_gain_release_alpha)
        self.smooth_pre_gain += (target - self.smooth_pre_gain) * alpha

        voice = asp_out + voiced

        # Cascade formants (F6->F1, high to low)
        cascade_in = voice * self.smooth_pre_gain / 2.0
        n0_out = self.nasal_zero.resonate(cascade_in, f.cfN0, f.cbN0)
        np_out = self.nasal_pole.resonate(n0_out, f.cfNP, f.cbNP)
        cascade_out = lerp(cascade_in, np_out, f.caNP)

        cf = [f.cf1, f.cf2, f.cf3, f.cf4, f.cf5, f.cf6]
        cb = [f.cb1, f.cb2, f.cb3, f.cb4, f.cb5, f.cb6]
        for i in range(5, -1, -1):
            cascade_out = self.cascade[i].resonate(cascade_out, cf[i], cb[i])

        # Parallel formants (frication)
        fric = self.fric_gen.get_next() * self.K_FRIC_NOISE_SCALE * f.fricationAmplitude
        parallel_in = fric * self.smooth_pre_gain / 2.0

        pf = [f.pf1, f.pf2, f.pf3, f.pf4, f.pf5, f.pf6]
        pb = [f.pb1, f.pb2, f.pb3, f.pb4, f.pb5, f.pb6]
        pa = [f.pa1, f.pa2, f.pa3, f.pa4, f.pa5, f.pa6]
        parallel_out = 0.0
        for i in range(6):
            parallel_out += (self.parallel[i].resonate(parallel_in, pf[i], pb[i]) - parallel_in) * pa[i]
        parallel_out = lerp(parallel_out, parallel_in, f.parallelBypass)

        out = (cascade_out + parallel_out) * f.outputGain

        # Final DC blocker
        filtered = out - self.last_input + self.K_DC_POLE * self.last_output
        self.last_input = out
        self.last_output = filtered

        return filtered


def synthesize_from_trajectory(points: list[TrajectoryPoint], sample_rate: int = 16000) -> np.ndarray:
    """
    Synthesize audio from trajectory points.
    Note: This is approximate since we're synthesizing from sampled points.
    """
    synth = SimpleSynthesizer(sample_rate)

    # Calculate total samples
    if not points:
        return np.zeros(0, dtype=np.float32)

    # Points are at resolution intervals - expand to full sample rate
    resolution_ms = points[1].time_ms - points[0].time_ms if len(points) > 1 else 1.0
    samples_per_point = int(resolution_ms * sample_rate / 1000.0)

    total_samples = len(points) * samples_per_point
    audio = np.zeros(total_samples, dtype=np.float32)

    sample_idx = 0
    for i, pt in enumerate(points):
        if pt.is_silence:
            synth.reset()
            for _ in range(samples_per_point):
                if sample_idx < total_samples:
                    audio[sample_idx] = 0.0
                    sample_idx += 1
        else:
            # Interpolate between this point and next
            next_pt = points[i + 1] if i + 1 < len(points) else pt
            for j in range(samples_per_point):
                if sample_idx >= total_samples:
                    break
                ratio = j / samples_per_point
                # Interpolate frame
                cur_arr = pt.frame.to_array()
                next_arr = next_pt.frame.to_array() if not next_pt.is_silence else cur_arr
                interp_arr = cur_arr + (next_arr - cur_arr) * ratio
                interp_frame = Frame.from_array(interp_arr)

                # Interpolate FrameEx (breathiness, creakiness, etc.)
                interp_ex = None
                cur_ex = pt.frame_ex
                next_ex = next_pt.frame_ex if not next_pt.is_silence else cur_ex
                if cur_ex or next_ex:
                    ex_a = cur_ex or _default_frame_ex()
                    ex_b = next_ex or _default_frame_ex()
                    interp_ex = {}
                    for key in ex_a:
                        va = ex_a[key]
                        vb = ex_b.get(key, va)
                        if isinstance(va, (int, float)) and math.isfinite(va) and math.isfinite(vb):
                            interp_ex[key] = va + (vb - va) * ratio
                        else:
                            interp_ex[key] = va

                audio[sample_idx] = synth.generate_sample(interp_frame, interp_ex)
                sample_idx += 1

    return audio


# =============================================================================
# Phoneme Duration and Frame Building (using lang_pack)
# =============================================================================

def get_phoneme_duration_ms(
    pdef: PhonemeDef,
    pack: PackSet,
    speed: float = 1.0,
    stress: int = 0,
    lengthened: bool = False,
) -> float:
    """
    Get phoneme duration using pack language parameters.
    Mirrors timing logic from ipa_engine.cpp.
    """
    lp = pack.lang

    # Base duration by phoneme type
    if pdef.is_vowel:
        base = 115.0
    elif pdef.is_stop:
        base = 55.0
    elif pdef.is_affricate:
        base = 70.0
    elif pdef.is_semivowel:
        base = 60.0
    elif pdef.is_liquid:
        base = 70.0
    elif pdef.is_nasal:
        base = 70.0
    elif pdef.is_tap:
        base = 35.0
    elif pdef.is_trill:
        base = lp.trill_modulation_ms if lp.trill_modulation_ms > 0 else 80.0
    else:
        base = 90.0  # Default (fricatives, etc.)

    dur = base / speed

    # Stress scaling from pack
    if stress == 1:
        dur *= lp.primary_stress_div
    elif stress == 2:
        dur *= lp.secondary_stress_div

    # Length mark scaling from pack
    if lengthened:
        if pdef.is_vowel or not lp.apply_lengthened_scale_to_vowels_only:
            dur *= lp.lengthened_scale

    return dur


def get_fade_ms(
    pdef: PhonemeDef,
    pack: PackSet,
    speed: float = 1.0,
    prev_pdef: Optional[PhonemeDef] = None,
) -> float:
    """
    Get fade/crossfade duration using pack boundary smoothing settings.
    """
    lp = pack.lang
    base_fade = 10.0

    if not lp.boundary_smoothing_enabled:
        return base_fade / speed

    if prev_pdef is not None:
        prev_vowel_like = prev_pdef.is_vowel or prev_pdef.is_semivowel
        cur_stop = pdef.is_stop or pdef.is_affricate
        cur_fric = pdef.get_field("fricationAmplitude") > 0.3

        if prev_vowel_like and cur_stop:
            base_fade = lp.boundary_smoothing_vowel_to_stop_ms
        elif (prev_pdef.is_stop or prev_pdef.is_affricate) and (pdef.is_vowel or pdef.is_semivowel):
            base_fade = lp.boundary_smoothing_stop_to_vowel_ms
        elif prev_vowel_like and cur_fric:
            base_fade = lp.boundary_smoothing_vowel_to_fric_ms

    return base_fade / speed


def get_stop_closure_gap(
    pdef: PhonemeDef,
    pack: PackSet,
    speed: float = 1.0,
    prev_pdef: Optional[PhonemeDef] = None,
) -> tuple[float, float]:
    """
    Determine stop closure gap timing based on pack settings.
    Returns: (gap_ms, fade_ms) - both 0.0 if no gap should be inserted
    """
    lp = pack.lang

    if not (pdef.is_stop or pdef.is_affricate):
        return 0.0, 0.0

    mode = lp.stop_closure_mode
    if mode == "none":
        return 0.0, 0.0

    after_vowel = prev_pdef is not None and (prev_pdef.is_vowel or prev_pdef.is_semivowel)
    in_cluster = prev_pdef is not None and not after_vowel and not prev_pdef.is_vowel

    # Check for nasal before stop
    if prev_pdef and prev_pdef.is_nasal and not lp.stop_closure_after_nasals_enabled:
        return 0.0, 0.0

    if mode == "always":
        pass
    elif mode == "after-vowel":
        if not after_vowel:
            return 0.0, 0.0
    elif mode == "vowel-and-cluster":
        if not (after_vowel or (in_cluster and lp.stop_closure_cluster_gaps_enabled)):
            return 0.0, 0.0

    if after_vowel:
        gap = lp.stop_closure_vowel_gap_ms
        fade = lp.stop_closure_vowel_fade_ms
    else:
        gap = lp.stop_closure_cluster_gap_ms
        fade = lp.stop_closure_cluster_fade_ms

    return gap / speed, fade / speed


def build_frame_from_phoneme(
    pdef: PhonemeDef,
    pack: PackSet,
    f0: float = 140.0,
) -> Frame:
    """Build a Frame from a PhonemeDef using pack defaults."""
    lp = pack.lang
    f = Frame()
    f.voicePitch = f0
    f.endVoicePitch = f0

    # Copy all explicitly set fields from phoneme definition
    for i, name in enumerate(FRAME_PARAM_NAMES):
        if pdef.has_field(name):
            setattr(f, name, pdef.fields[i])

    # Apply pack defaults for unset output parameters
    if not pdef.has_field("preFormantGain"):
        f.preFormantGain = lp.default_pre_formant_gain
    if not pdef.has_field("outputGain"):
        f.outputGain = lp.default_output_gain
    if not pdef.has_field("vibratoPitchOffset"):
        f.vibratoPitchOffset = lp.default_vibrato_pitch_offset
    if not pdef.has_field("vibratoSpeed"):
        f.vibratoSpeed = lp.default_vibrato_speed
    if not pdef.has_field("voiceTurbulenceAmplitude"):
        f.voiceTurbulenceAmplitude = lp.default_voice_turbulence_amplitude
    if not pdef.has_field("glottalOpenQuotient"):
        f.glottalOpenQuotient = lp.default_glottal_open_quotient

    return f


# =============================================================================
# IPA Tokenization
# =============================================================================

TRANSPARENT_IPA = {"ˈ", "ˌ", "ː", "ˑ", ".", "‿", "͡", " ", "\t", "\n", "\r"}


def espeak_ipa(voice: str, text: str) -> str:
    cmd = ["espeak-ng", "-q", "--ipa", "-v", voice, text]
    try:
        out = subprocess.check_output(cmd, text=True, stderr=subprocess.DEVNULL)
    except FileNotFoundError:
        cmd[0] = "espeak"
        out = subprocess.check_output(cmd, text=True, stderr=subprocess.DEVNULL)
    return out.strip()


def tokenize_ipa(ipa: str, phoneme_keys: set[str]) -> list[str]:
    """Greedy tokenizer for IPA string."""
    keys = sorted(phoneme_keys, key=len, reverse=True)
    out = []
    i = 0
    while i < len(ipa):
        ch = ipa[i]
        if ch.isspace():
            out.append(" ")
            i += 1
            continue
        if ch in TRANSPARENT_IPA:
            out.append(ch)
            i += 1
            continue

        matched = None
        for k in keys:
            if ipa.startswith(k, i):
                matched = k
                break
        if matched is None:
            out.append(ch)
            i += 1
        else:
            out.append(matched)
            i += len(matched)

    # Clean duplicate spaces
    cleaned = []
    for t in out:
        if t == " " and cleaned and cleaned[-1] == " ":
            continue
        cleaned.append(t)
    return cleaned


# =============================================================================
# Visualization
# =============================================================================

def plot_formant_trajectory(
    points: list[TrajectoryPoint],
    title: str = "Formant Trajectory",
    show_bandwidths: bool = False,
) -> Optional[Any]:
    """Plot F1, F2, F3 trajectories over time."""
    if not HAS_MATPLOTLIB:
        print("matplotlib not available for plotting")
        return None

    times = [p.time_ms for p in points]
    f1 = [p.frame.cf1 if not p.is_silence else 0 for p in points]
    f2 = [p.frame.cf2 if not p.is_silence else 0 for p in points]
    f3 = [p.frame.cf3 if not p.is_silence else 0 for p in points]
    voice_amp = [p.frame.voiceAmplitude if not p.is_silence else 0 for p in points]
    fric_amp = [p.frame.fricationAmplitude if not p.is_silence else 0 for p in points]

    fig, axes = plt.subplots(4, 1, figsize=(14, 10), sharex=True)

    # F1, F2, F3 trajectories
    ax = axes[0]
    ax.plot(times, f1, label="F1", color="#e74c3c", linewidth=2)
    ax.plot(times, f2, label="F2", color="#3498db", linewidth=2)
    ax.plot(times, f3, label="F3", color="#2ecc71", linewidth=2)
    ax.set_ylabel("Frequency (Hz)")
    ax.set_title(title)
    ax.legend(loc="upper right")
    ax.grid(True, alpha=0.3)
    ax.set_ylim(0, 4000)

    # Voice amplitude
    ax = axes[1]
    ax.fill_between(times, voice_amp, alpha=0.5, color="#9b59b6", label="Voice Amp")
    ax.set_ylabel("Voice Amplitude")
    ax.set_ylim(0, 1.2)
    ax.legend(loc="upper right")
    ax.grid(True, alpha=0.3)

    # Frication amplitude
    ax = axes[2]
    ax.fill_between(times, fric_amp, alpha=0.5, color="#e67e22", label="Fric Amp")
    ax.set_ylabel("Frication Amplitude")
    ax.set_ylim(0, 1.2)
    ax.legend(loc="upper right")
    ax.grid(True, alpha=0.3)

    # Pitch
    pitch = [p.frame.voicePitch if not p.is_silence else 0 for p in points]
    ax = axes[3]
    ax.plot(times, pitch, color="#1abc9c", linewidth=2, label="F0")
    ax.set_ylabel("Pitch (Hz)")
    ax.set_xlabel("Time (ms)")
    ax.legend(loc="upper right")
    ax.grid(True, alpha=0.3)

    # Add phoneme labels
    current_label = ""
    label_positions = []
    for i, p in enumerate(points):
        if p.label and p.label != current_label:
            label_positions.append((times[i], p.label))
            current_label = p.label

    for ax in axes:
        for t, lbl in label_positions:
            ax.axvline(x=t, color="gray", linestyle="--", alpha=0.5, linewidth=0.5)
        if ax == axes[0]:
            for t, lbl in label_positions:
                ax.annotate(lbl, (t, ax.get_ylim()[1]), fontsize=8, ha="left", va="top")

    plt.tight_layout()
    return fig


def plot_vowel_space(points: list[TrajectoryPoint], title: str = "Vowel Space (F1 × F2)") -> Optional[Any]:
    """Plot F1 vs F2 vowel quadrilateral."""
    if not HAS_MATPLOTLIB:
        print("matplotlib not available for plotting")
        return None

    fig, ax = plt.subplots(figsize=(10, 8))

    vowel_points = []
    current_label = ""
    for p in points:
        if p.is_silence:
            continue
        if p.frame.voiceAmplitude > 0.5 and p.frame.fricationAmplitude < 0.3:
            if p.frame.cf1 > 100 and p.frame.cf2 > 100:
                if p.label != current_label:
                    vowel_points.append((p.frame.cf2, p.frame.cf1, p.label))
                    current_label = p.label

    for f2, f1, label in vowel_points:
        ax.scatter(f2, f1, s=150, alpha=0.7)
        ax.annotate(label, (f2, f1), fontsize=12, ha="left", va="bottom",
                   xytext=(5, 5), textcoords="offset points")

    if len(vowel_points) > 1:
        f2s = [vp[0] for vp in vowel_points]
        f1s = [vp[1] for vp in vowel_points]
        ax.plot(f2s, f1s, "k--", alpha=0.3, linewidth=1)

    ax.invert_xaxis()
    ax.invert_yaxis()
    ax.set_xlabel("F2 (Hz) ← front ... back →")
    ax.set_ylabel("F1 (Hz) ← close ... open →")
    ax.set_title(title)
    ax.grid(True, alpha=0.3)

    return fig


# =============================================================================
# Main pipeline
# =============================================================================

def build_tokens(
    ipa_tokens: list[str],
    pack: PackSet,
    f0: float,
    speed: float,
) -> list[EmissionToken]:
    """Build EmissionToken list from IPA tokens with timing, frames, and flags.

    Handles stress/length markers, stop closure gaps (with voiced_closure
    and pre_stop_gap flags), post-stop aspiration tokens, and word boundaries.
    """
    result: list[EmissionToken] = []
    stress = 0
    tie_next = False
    lengthened = False
    prev_pdef: Optional[PhonemeDef] = None
    at_word_start = True

    for tok in ipa_tokens:
        if tok == " ":
            et = EmissionToken(
                silence=True, duration_ms=35.0 / speed, fade_ms=5.0, label=" ")
            result.append(et)
            prev_pdef = None
            at_word_start = True
            continue
        if tok == "ˈ":
            stress = 1
            continue
        if tok == "ˌ":
            stress = 2
            continue
        if tok == "͡":
            tie_next = True
            continue
        if tok in {"ː", "ˑ"}:
            lengthened = True
            continue
        if tok in {".", "‿"}:
            continue

        pdef = pack.get_phoneme(tok)
        if pdef is None:
            continue

        # Stop closure gap
        gap_ms, gap_fade = get_stop_closure_gap(pdef, pack, speed, prev_pdef)
        if gap_ms > 0:
            is_voiced = pdef.is_voiced
            gap_tok = EmissionToken(
                pdef=pdef,
                silence=not is_voiced,
                pre_stop_gap=not is_voiced,
                voiced_closure=is_voiced,
                duration_ms=gap_ms,
                fade_ms=gap_fade,
                label="",
            )
            # Coda fric-stop blend: if prev token was a fricative
            if (prev_pdef is not None
                    and prev_pdef.get_field("fricationAmplitude") > 0.1
                    and not prev_pdef.is_stop and not prev_pdef.is_affricate):
                gap_tok.coda_fric_stop_blend = True
            result.append(gap_tok)

        # Duration
        dur = get_phoneme_duration_ms(pdef, pack, speed, stress, lengthened)
        if tie_next:
            dur *= 0.4
            tie_next = False

        # Pitch
        pitch = f0
        if stress == 1:
            pitch *= 1.05
        elif stress == 2:
            pitch *= 1.02

        # Fade
        fade = get_fade_ms(pdef, pack, speed, prev_pdef)

        # Frame
        frame = build_frame_from_phoneme(pdef, pack, f0=pitch)
        frame.endVoicePitch = pitch

        et = EmissionToken(
            pdef=pdef,
            frame=frame,
            duration_ms=dur,
            fade_ms=fade,
            label=tok,
            stress=stress,
            word_start=at_word_start,
        )

        # Post-stop aspiration: insert after voiceless stops
        if (pack.lang.post_stop_aspiration_enabled
                and (pdef.is_stop or pdef.is_affricate)
                and not pdef.is_voiced):
            asp_dur = pack.lang.post_stop_aspiration_duration_ms / speed
            asp_pdef = pack.get_phoneme(pack.lang.post_stop_aspiration_phoneme)
            if asp_pdef and asp_dur > 0:
                # Main stop token gets its duration
                result.append(et)
                # Aspiration token
                asp_frame = build_frame_from_phoneme(asp_pdef, pack, f0=pitch)
                asp_frame.endVoicePitch = pitch
                asp_tok = EmissionToken(
                    pdef=asp_pdef,
                    frame=asp_frame,
                    duration_ms=asp_dur,
                    fade_ms=min(fade, asp_dur),
                    label=tok,
                    post_stop_aspiration=True,
                    coda_fric_stop_blend=et.coda_fric_stop_blend,
                )
                result.append(asp_tok)
                prev_pdef = asp_pdef
                stress = 0
                lengthened = False
                at_word_start = False
                continue

        result.append(et)
        prev_pdef = pdef
        stress = 0
        lengthened = False
        at_word_start = False

    return result


def collapse_diphthongs(
    tokens: list[EmissionToken],
    pack: PackSet,
) -> list[EmissionToken]:
    """Detect and collapse tied vowel pairs into diphthong glide tokens.

    Handles explicit tie bars (U+0361) already present between tokens,
    and auto-ties adjacent vowels if pack.lang.auto_tie_diphthongs is True.
    Mirrors diphthong_collapse.cpp.
    """
    if not pack.lang.diphthong_collapse_enabled:
        return tokens

    # Pass 1: Mark tied pairs.
    # Tie bars in IPA become separate tokens during tokenization, but
    # build_tokens() doesn't emit them as EmissionTokens. Instead, we look
    # for adjacent vowel tokens and auto-tie if enabled.
    auto_tie = pack.lang.auto_tie_diphthongs
    tied: list[bool] = [False] * len(tokens)  # True = onset of a tied pair

    for i in range(len(tokens) - 1):
        a = tokens[i]
        b = tokens[i + 1]
        if a.pdef is None or b.pdef is None:
            continue
        if not a.pdef.is_vowel:
            continue
        if not (b.pdef.is_vowel or b.pdef.is_semivowel):
            continue
        if a.silence or b.silence or a.pre_stop_gap or b.pre_stop_gap:
            continue
        # Guard: syllabic nasals flagged _isVowel can false-trigger
        if a.pdef.is_nasal or b.pdef.is_nasal:
            continue
        if auto_tie:
            tied[i] = True

    # Pass 2: Collapse tied pairs into single diphthong glide tokens.
    result: list[EmissionToken] = []
    i = 0
    while i < len(tokens):
        if tied[i] and i + 1 < len(tokens):
            a = tokens[i]
            b = tokens[i + 1]

            # Merge duration with per-pair scaling (Gay 1968)
            merged_dur = a.duration_ms + b.duration_ms
            pair_scale = pack.lang.diphthong_duration_scale
            if pack.lang.diphthong_pair_scales and a.pdef and b.pdef:
                pair_key = f"{a.pdef.key} {b.pdef.key}"
                if pair_key in pack.lang.diphthong_pair_scales:
                    pair_scale = pack.lang.diphthong_pair_scales[pair_key]
            if pair_scale > 0.0 and pair_scale != 1.0:
                merged_dur *= pair_scale
            floor = pack.lang.diphthong_duration_floor_ms
            if merged_dur < floor:
                merged_dur = floor

            a.duration_ms = merged_dur
            a.is_diphthong_glide = True

            # Capture end targets from offset vowel (b)
            if b.pdef:
                for param, (has_attr, val_attr) in [
                    ("cf1", ("has_end_cf1", "end_cf1")),
                    ("cf2", ("has_end_cf2", "end_cf2")),
                    ("cf3", ("has_end_cf3", "end_cf3")),
                    ("pf1", ("has_end_pf1", "end_pf1")),
                    ("pf2", ("has_end_pf2", "end_pf2")),
                    ("pf3", ("has_end_pf3", "end_pf3")),
                    ("cb1", ("has_end_cb1", "end_cb1")),
                    ("cb2", ("has_end_cb2", "end_cb2")),
                    ("cb3", ("has_end_cb3", "end_cb3")),
                    ("pb1", ("has_end_pb1", "end_pb1")),
                    ("pb2", ("has_end_pb2", "end_pb2")),
                    ("pb3", ("has_end_pb3", "end_pb3")),
                ]:
                    if b.pdef.has_field(param):
                        setattr(a, has_attr, True)
                        setattr(a, val_attr, b.pdef.get_field(param))

            result.append(a)
            i += 2  # Skip the offset vowel
        else:
            result.append(tokens[i])
            i += 1

    return result


def _voice_quality_ex(pdef: Optional[Any]) -> Optional[dict]:
    """Build FrameEx dict from PhonemeDef voice quality fields (breathiness, creakiness)."""
    if pdef is None:
        return None
    ex = {}
    if getattr(pdef, 'has_breathiness', False) and pdef.breathiness != 0.0:
        ex["breathiness"] = pdef.breathiness
    if getattr(pdef, 'has_creakiness', False) and pdef.creakiness != 0.0:
        ex["creakiness"] = pdef.creakiness
    return ex if ex else None


def emit_micro_frames(
    tokens: list[EmissionToken],
    pack: PackSet,
    speed: float,
    recorder: TrajectoryRecorder,
) -> None:
    """Emit micro-frames for a token sequence into the recorder.

    Port of frame_emit.cpp emitFrames(). Each token is inspected for
    micro-event flags and the appropriate emission pattern is chosen.
    """
    lp = pack.lang
    prev_base: Optional[Frame] = None
    had_prev_frame = False
    prev_token_was_stop = False
    prev_token_was_tap = False

    for idx, t in enumerate(tokens):
        next_t = tokens[idx + 1] if idx + 1 < len(tokens) else None

        # 1. Voice bar (voiced stop closures)
        if t.voiced_closure and had_prev_frame and prev_base is not None:
            _emit_voice_bar(prev_base, t, pack, recorder)
            continue

        # 2. Coda noise taper (fricative→stop closure)
        if (t.pre_stop_gap and t.coda_fric_stop_blend
                and not t.voiced_closure and had_prev_frame
                and prev_base is not None):
            _emit_coda_taper(prev_base, t, pack, recorder)
            continue

        # 3. Silence / no pdef
        if t.silence or t.pdef is None or t.frame is None:
            recorder.queue_frame(
                None, duration_ms=t.duration_ms, fade_ms=t.fade_ms, label=t.label)
            continue

        # 4. Build base frame (already in t.frame), save prev_base
        base = t.frame
        prev_base = base.copy()

        # Rate-adaptive bandwidth widening
        hr_t = lp.high_rate_threshold
        bw_f = lp.high_rate_bandwidth_widening_factor
        if hr_t > 0 and bw_f > 1.0 and speed > hr_t:
            ceiling = hr_t * 1.8
            ramp = min((speed - hr_t) / (ceiling - hr_t), 1.0)
            bw_scale = 1.0 + ramp * (bw_f - 1.0)
            base.cb1 *= bw_scale
            base.cb2 *= bw_scale
            base.cb3 *= bw_scale

        # 5. Diphthong glide
        if t.is_diphthong_glide and t.duration_ms > 0:
            _emit_diphthong_glide(base, t, pack, speed, recorder, next_t)
            had_prev_frame = True
            prev_token_was_stop = False
            prev_token_was_tap = False
            continue

        # 6. Trill modulation
        if (lp.trill_modulation_ms > 0 and t.pdef.is_trill
                and t.duration_ms > 0):
            _emit_trill(base, t, pack, recorder)
            had_prev_frame = True
            prev_token_was_stop = False
            continue

        # 7. Stop burst
        is_stop = t.pdef.is_stop
        is_affricate = t.pdef.is_affricate
        if ((is_stop or is_affricate)
                and not t.pre_stop_gap and not t.post_stop_aspiration
                and not t.voiced_closure and t.duration_ms > 1.0):
            _emit_stop_burst(base, t, pack, speed, recorder)
            had_prev_frame = True
            prev_token_was_stop = True
            prev_token_was_tap = False
            continue

        # 8. Fricative envelope
        fric_amp = base.fricationAmplitude
        if (not is_stop and not is_affricate and not t.pre_stop_gap
                and not t.post_stop_aspiration and not t.voiced_closure
                and fric_amp > 0.0):
            _emit_fricative_envelope(
                base, t, pack, speed, recorder, prev_token_was_stop)
            had_prev_frame = True
            prev_token_was_stop = False
            prev_token_was_tap = False
            continue

        # 9. Release spread (post-stop aspiration)
        if t.post_stop_aspiration and t.pdef and t.duration_ms > 1.0:
            _emit_release_spread(base, t, pack, recorder)
            had_prev_frame = True
            prev_token_was_stop = True
            prev_token_was_tap = False
            continue

        # 10. Tap notch
        if t.pdef.is_tap and t.duration_ms > 2.0:
            _emit_tap_notch(base, t, pack, speed, recorder)
            had_prev_frame = True
            prev_token_was_tap = True
            prev_token_was_stop = False
            continue

        # 11. Word boundary dip + normal emission
        main_dur = t.duration_ms
        main_fade = t.fade_ms
        wb_dip_ms = lp.word_boundary_dip_ms
        vq_ex = _voice_quality_ex(t.pdef)

        if (wb_dip_ms > 0 and t.word_start and had_prev_frame
                and main_dur > wb_dip_ms + 1.0):
            dip = base.copy()
            depth = lp.word_boundary_dip_depth
            dip.voiceAmplitude *= depth
            dip.fricationAmplitude *= depth
            dip.aspirationAmplitude *= depth
            recorder.queue_frame(
                dip, duration_ms=wb_dip_ms, fade_ms=main_fade, label=t.label,
                frame_ex=vq_ex)
            main_dur -= wb_dip_ms
            main_fade = wb_dip_ms

        # Post-tap fade cap
        emit_fade = main_fade
        if prev_token_was_tap and main_dur > 0:
            emit_fade = min(emit_fade, main_dur * 0.35)

        recorder.queue_frame(
            base, duration_ms=main_dur, fade_ms=emit_fade, label=t.label,
            frame_ex=vq_ex)
        had_prev_frame = True
        prev_token_was_tap = False
        prev_token_was_stop = (
            t.pdef.is_stop or t.pdef.is_affricate or t.post_stop_aspiration)


# =============================================================================
# Individual Micro-Event Emitters
# =============================================================================

def _emit_voice_bar(
    prev_base: Frame,
    token: EmissionToken,
    pack: PackSet,
    recorder: TrajectoryRecorder,
) -> None:
    """Voiced stop closure: murmur from previous frame's resonators."""
    vb = prev_base.copy()
    vb_amp = (token.pdef.voice_bar_amplitude
              if token.pdef and token.pdef.has_voice_bar_amplitude else 0.3)
    vb_f1 = (token.pdef.voice_bar_f1
             if token.pdef and token.pdef.has_voice_bar_f1 else 150.0)

    vb.voiceAmplitude = vb_amp
    vb.fricationAmplitude = 0.0
    vb.aspirationAmplitude = 0.0
    vb.cf1 = vb_f1
    vb.pf1 = vb_f1
    vb.preFormantGain = vb_amp

    fade = max(token.fade_ms, 8.0)
    recorder.queue_frame(vb, duration_ms=token.duration_ms, fade_ms=fade,
                         label=token.label)


def _emit_coda_taper(
    prev_base: Frame,
    token: EmissionToken,
    pack: PackSet,
    recorder: TrajectoryRecorder,
) -> None:
    """Fricative→stop closure crossfade: 2 micro-frames (early sibilant, late aspirated)."""
    lp = pack.lang
    total_dur = token.duration_ms
    early_dur = total_dur * 0.45
    late_dur = total_dur - early_dur
    prev_fric = prev_base.fricationAmplitude

    # Early taper: sibilant tail
    early = prev_base.copy()
    early.voiceAmplitude = 0.0
    early.fricationAmplitude = prev_fric * lp.coda_noise_taper_early_fric_scale
    early.aspirationAmplitude = lp.coda_noise_taper_early_asp_amp
    early.preFormantGain = lp.coda_noise_taper_pre_gain
    recorder.queue_frame(early, duration_ms=early_dur, fade_ms=token.fade_ms,
                         label=token.label)

    # Late taper: aspirated transition
    late = prev_base.copy()
    late.voiceAmplitude = 0.0
    late.fricationAmplitude = prev_fric * lp.coda_noise_taper_late_fric_scale
    late.aspirationAmplitude = lp.coda_noise_taper_late_asp_amp
    late.preFormantGain = lp.coda_noise_taper_pre_gain

    # Blend formants toward stop's place (40%)
    if token.pdef:
        blend = 0.40
        for param in ("cf2", "cf3", "pf2", "pf3"):
            if token.pdef.has_field(param):
                cur = getattr(late, param)
                target = token.pdef.get_field(param)
                setattr(late, param, cur + (target - cur) * blend)

    late_fade = max(early_dur * 0.5, late_dur * 0.4)
    recorder.queue_frame(late, duration_ms=late_dur, fade_ms=late_fade,
                         label=token.label)


def _emit_stop_burst(
    base: Frame,
    token: EmissionToken,
    pack: PackSet,
    speed: float,
    recorder: TrajectoryRecorder,
) -> None:
    """Stop/affricate burst: 2 micro-frames (burst + decay)."""
    pdef = token.pdef
    place = get_place(pdef.key) if pdef else Place.UNKNOWN

    # Place-based defaults
    burst_ms = 7.0
    decay_rate = 0.5
    spectral_tilt = 0.0
    if place == Place.LABIAL:
        burst_ms, decay_rate, spectral_tilt = 5.0, 0.6, 0.1
    elif place == Place.ALVEOLAR:
        burst_ms, decay_rate, spectral_tilt = 7.0, 0.5, 0.0
    elif place == Place.VELAR:
        burst_ms, decay_rate, spectral_tilt = 11.0, 0.4, -0.15
    elif place == Place.PALATAL:
        burst_ms, decay_rate, spectral_tilt = 9.0, 0.45, -0.1

    # Phoneme-level overrides
    if pdef.has_burst_duration_ms:
        burst_ms = pdef.burst_duration_ms
    if pdef.has_burst_decay_rate:
        decay_rate = pdef.burst_decay_rate
    if pdef.has_burst_spectral_tilt:
        spectral_tilt = pdef.burst_spectral_tilt

    # Coda blend: longer burst, faster decay
    if token.coda_fric_stop_blend and not pdef.is_affricate:
        burst_ms = max(burst_ms, token.duration_ms * 0.6)
        decay_rate = 0.7

    # Clamp burst to 75% of token
    max_burst = token.duration_ms * 0.75
    if burst_ms > max_burst:
        burst_ms = max_burst

    total_dur = token.duration_ms
    start_pitch = base.voicePitch
    pitch_delta = base.endVoicePitch - start_pitch
    burst_frac = burst_ms / total_dur if total_dur > 0 else 0

    # Burst micro-frame
    seg1 = base.copy()
    seg1.voicePitch = start_pitch
    seg1.endVoicePitch = start_pitch + pitch_delta * burst_frac
    if spectral_tilt < 0:
        seg1.pa5 = min(1.0, seg1.pa5 * (1.0 - spectral_tilt))
        seg1.pa6 = min(1.0, seg1.pa6 * (1.0 - spectral_tilt * 0.7))
    elif spectral_tilt > 0:
        seg1.pa3 = min(1.0, seg1.pa3 * (1.0 + spectral_tilt))
        seg1.pa4 = min(1.0, seg1.pa4 * (1.0 + spectral_tilt * 0.7))
    recorder.queue_frame(seg1, duration_ms=burst_ms, fade_ms=token.fade_ms,
                         label=token.label)

    # Decay micro-frame
    seg2 = base.copy()
    seg2.voicePitch = start_pitch + pitch_delta * burst_frac
    seg2.endVoicePitch = start_pitch + pitch_delta
    if not pdef.is_affricate:
        seg2.fricationAmplitude *= (1.0 - decay_rate)
    decay_dur = total_dur - burst_ms
    decay_fade = min(burst_ms * 0.5, decay_dur)
    recorder.queue_frame(seg2, duration_ms=decay_dur, fade_ms=decay_fade,
                         label=token.label)


def _emit_fricative_envelope(
    base: Frame,
    token: EmissionToken,
    pack: PackSet,
    speed: float,
    recorder: TrajectoryRecorder,
    prev_was_stop: bool,
) -> None:
    """Fricative: 3 micro-frames (attack/sustain/decay)."""
    pdef = token.pdef
    attack_ms = pdef.fric_attack_ms if pdef.has_fric_attack_ms else 3.0
    decay_ms = pdef.fric_decay_ms if pdef.has_fric_decay_ms else 4.0

    if prev_was_stop:
        attack_ms = min(attack_ms, 2.5)

    # Only emit micro-frames if long enough
    if attack_ms + decay_ms + 2.0 >= token.duration_ms:
        # Too short — single frame fallback
        recorder.queue_frame(
            base, duration_ms=token.duration_ms, fade_ms=token.fade_ms,
            label=token.label)
        return

    fric_amp = base.fricationAmplitude
    total_dur = token.duration_ms
    sustain_dur = total_dur - attack_ms - decay_ms
    start_pitch = base.voicePitch
    pitch_delta = base.endVoicePitch - start_pitch
    attack_frac = attack_ms / total_dur
    sustain_end_frac = (attack_ms + sustain_dur) / total_dur

    # Attack: 10% → full
    seg1 = base.copy()
    seg1.fricationAmplitude = fric_amp * 0.1
    seg1.voicePitch = start_pitch
    seg1.endVoicePitch = start_pitch + pitch_delta * attack_frac
    recorder.queue_frame(seg1, duration_ms=attack_ms, fade_ms=token.fade_ms,
                         label=token.label)

    # Sustain: full amplitude
    seg2 = base.copy()
    seg2.voicePitch = start_pitch + pitch_delta * attack_frac
    seg2.endVoicePitch = start_pitch + pitch_delta * sustain_end_frac
    recorder.queue_frame(seg2, duration_ms=sustain_dur, fade_ms=attack_ms,
                         label=token.label)

    # Decay: full → 30%
    seg3 = base.copy()
    seg3.fricationAmplitude = fric_amp * 0.3
    seg3.voicePitch = start_pitch + pitch_delta * sustain_end_frac
    seg3.endVoicePitch = start_pitch + pitch_delta
    recorder.queue_frame(seg3, duration_ms=decay_ms, fade_ms=decay_ms * 0.5,
                         label=token.label)


def _emit_release_spread(
    base: Frame,
    token: EmissionToken,
    pack: PackSet,
    recorder: TrajectoryRecorder,
) -> None:
    """Post-stop aspiration: 2 micro-frames (ramp-in + full)."""
    pdef = token.pdef

    # Coda blend variant: single frame at 60% aspiration
    if token.coda_fric_stop_blend:
        seg = base.copy()
        seg.aspirationAmplitude *= 0.60
        recorder.queue_frame(seg, duration_ms=token.duration_ms,
                             fade_ms=token.duration_ms * 0.5, label=token.label)
        return

    spread_ms = (pdef.release_spread_ms
                 if pdef.has_release_spread_ms else 4.0)

    if spread_ms <= 0 or spread_ms >= token.duration_ms:
        recorder.queue_frame(base, duration_ms=token.duration_ms,
                             fade_ms=token.fade_ms, label=token.label)
        return

    total_dur = token.duration_ms
    start_pitch = base.voicePitch
    pitch_delta = base.endVoicePitch - start_pitch
    spread_frac = spread_ms / total_dur if total_dur > 0 else 0

    # Ramp-in: 15% amplitude
    seg1 = base.copy()
    seg1.fricationAmplitude *= 0.15
    seg1.aspirationAmplitude *= 0.15
    seg1.voicePitch = start_pitch
    seg1.endVoicePitch = start_pitch + pitch_delta * spread_frac
    recorder.queue_frame(seg1, duration_ms=spread_ms, fade_ms=token.fade_ms,
                         label=token.label)

    # Full aspiration
    seg2 = base.copy()
    seg2.voicePitch = start_pitch + pitch_delta * spread_frac
    seg2.endVoicePitch = start_pitch + pitch_delta
    full_dur = total_dur - spread_ms
    recorder.queue_frame(seg2, duration_ms=full_dur, fade_ms=spread_ms * 0.5,
                         label=token.label)


def _emit_tap_notch(
    base: Frame,
    token: EmissionToken,
    pack: PackSet,
    speed: float,
    recorder: TrajectoryRecorder,
) -> None:
    """Tap: 3 micro-frames (onset / amplitude notch / recovery)."""
    total_dur = token.duration_ms
    notch_floor_ms = 1.5
    notch_dur = max(total_dur * 0.50, notch_floor_ms)
    if notch_dur > total_dur * 0.80:
        notch_dur = total_dur * 0.80
    remain_dur = total_dur - notch_dur
    onset_dur = remain_dur * 0.5
    recov_dur = remain_dur - onset_dur

    notch_amp = base.voiceAmplitude * 0.50
    start_pitch = base.voicePitch
    pitch_delta = base.endVoicePitch - start_pitch
    onset_frac = onset_dur / total_dur if total_dur > 0 else 0
    notch_end_frac = (onset_dur + notch_dur) / total_dur if total_dur > 0 else 0
    micro_fade = max(0.5, 1.5 / max(0.5, speed))

    # Onset
    seg1 = base.copy()
    seg1.voicePitch = start_pitch
    seg1.endVoicePitch = start_pitch + pitch_delta * onset_frac
    recorder.queue_frame(seg1, duration_ms=onset_dur, fade_ms=token.fade_ms,
                         label=token.label)

    # Notch
    seg2 = base.copy()
    seg2.voiceAmplitude = notch_amp
    seg2.voicePitch = start_pitch + pitch_delta * onset_frac
    seg2.endVoicePitch = start_pitch + pitch_delta * notch_end_frac
    recorder.queue_frame(seg2, duration_ms=notch_dur, fade_ms=micro_fade,
                         label=token.label)

    # Recovery
    seg3 = base.copy()
    seg3.voicePitch = start_pitch + pitch_delta * notch_end_frac
    seg3.endVoicePitch = start_pitch + pitch_delta
    recorder.queue_frame(seg3, duration_ms=recov_dur, fade_ms=micro_fade,
                         label=token.label)


def _emit_diphthong_glide(
    base: Frame,
    token: EmissionToken,
    pack: PackSet,
    speed: float,
    recorder: TrajectoryRecorder,
    next_token: Optional[EmissionToken],
) -> None:
    """Diphthong: N cosine-smoothed formant sweep waypoints."""
    lp = pack.lang
    total_dur = token.duration_ms

    interval_ms = lp.diphthong_micro_frame_interval_ms
    pitch0 = base.voicePitch
    if pitch0 > 100.0:
        interval_ms *= (100.0 / pitch0)
        if interval_ms < 3.0:
            interval_ms = 3.0

    N = max(3, min(10, int(total_dur / interval_ms))) if interval_ms > 0 else 3

    start_cf1, start_cf2, start_cf3 = base.cf1, base.cf2, base.cf3
    start_pf1, start_pf2, start_pf3 = base.pf1, base.pf2, base.pf3
    start_cb1, start_cb2, start_cb3 = base.cb1, base.cb2, base.cb3
    start_pb1, start_pb2, start_pb3 = base.pb1, base.pb2, base.pb3

    end_cf1 = token.end_cf1 if token.has_end_cf1 else start_cf1
    end_cf2 = token.end_cf2 if token.has_end_cf2 else start_cf2
    end_cf3 = token.end_cf3 if token.has_end_cf3 else start_cf3
    end_pf1 = token.end_pf1 if token.has_end_pf1 else end_cf1
    end_pf2 = token.end_pf2 if token.has_end_pf2 else end_cf2
    end_pf3 = token.end_pf3 if token.has_end_pf3 else end_cf3
    end_cb1 = token.end_cb1 if token.has_end_cb1 else start_cb1
    end_cb2 = token.end_cb2 if token.has_end_cb2 else start_cb2
    end_cb3 = token.end_cb3 if token.has_end_cb3 else start_cb3
    end_pb1 = token.end_pb1 if token.has_end_pb1 else start_pb1
    end_pb2 = token.end_pb2 if token.has_end_pb2 else start_pb2
    end_pb3 = token.end_pb3 if token.has_end_pb3 else start_pb3

    start_pitch = base.voicePitch
    end_pitch = base.endVoicePitch
    pitch_delta = end_pitch - start_pitch
    dip_factor = lp.diphthong_amplitude_dip_factor
    base_seg_dur = total_dur / N

    # Adaptive onset hold
    onset_hold = adaptive_onset_hold(
        lp.diphthong_onset_hold_exponent,
        start_cf1, end_cf1, start_cf2, end_cf2,
        total_dur, next_token)

    # Onset settle
    seg0_dur = base_seg_dur
    other_seg_dur = base_seg_dur
    settle_ms = lp.diphthong_onset_settle_ms
    if settle_ms > 0 and N > 1:
        seg0_dur = min(base_seg_dur + settle_ms, total_dur * 0.5)
        other_seg_dur = (total_dur - seg0_dur) / (N - 1)

    for seg in range(N):
        frac = (seg / (N - 1)) if N > 1 else 0.0
        if onset_hold > 1.0:
            frac = pow(frac, onset_hold)
        s = cosine_smooth(frac)

        mf = base.copy()
        mf.cf1 = freq_lerp(start_cf1, end_cf1, s)
        mf.cf2 = freq_lerp(start_cf2, end_cf2, s)
        mf.cf3 = freq_lerp(start_cf3, end_cf3, s)
        mf.pf1 = freq_lerp(start_pf1, end_pf1, s)
        mf.pf2 = freq_lerp(start_pf2, end_pf2, s)
        mf.pf3 = freq_lerp(start_pf3, end_pf3, s)
        mf.cb1 = freq_lerp(start_cb1, end_cb1, s)
        mf.cb2 = freq_lerp(start_cb2, end_cb2, s)
        mf.cb3 = freq_lerp(start_cb3, end_cb3, s)
        mf.pb1 = freq_lerp(start_pb1, end_pb1, s)
        mf.pb2 = freq_lerp(start_pb2, end_pb2, s)
        mf.pb3 = freq_lerp(start_pb3, end_pb3, s)

        t0 = (seg / N) if N > 1 else 0.0
        t1 = ((seg + 1) / N) if N > 1 else 1.0
        mf.voicePitch = start_pitch + pitch_delta * t0
        mf.endVoicePitch = start_pitch + pitch_delta * t1

        if dip_factor > 0:
            amp_scale = 1.0 - dip_factor * math.sin(math.pi * frac)
            mf.voiceAmplitude *= amp_scale

        this_dur = seg0_dur if seg == 0 else other_seg_dur
        fade_in = token.fade_ms if seg == 0 else this_dur
        if fade_in > this_dur:
            fade_in = this_dur

        recorder.queue_frame(mf, duration_ms=this_dur, fade_ms=fade_in,
                             label=token.label)


def _emit_trill(
    base: Frame,
    token: EmissionToken,
    pack: PackSet,
    recorder: TrajectoryRecorder,
) -> None:
    """Trill: alternating open/closure micro-frames (~28ms cycle)."""
    lp = pack.lang
    total_dur = token.duration_ms

    K_CLOSE_FACTOR = 0.22
    K_CLOSE_FRAC = 0.28
    K_FRIC_FLOOR = 0.12
    K_MIN_PHASE_MS = 0.25
    K_FIXED_CYCLE_MS = 28.0

    cycle_ms = K_FIXED_CYCLE_MS
    if cycle_ms > total_dur:
        cycle_ms = total_dur

    close_ms = cycle_ms * K_CLOSE_FRAC
    open_ms = cycle_ms - close_ms
    if open_ms < K_MIN_PHASE_MS:
        open_ms = K_MIN_PHASE_MS
        close_ms = max(K_MIN_PHASE_MS, cycle_ms - open_ms)
    if close_ms < K_MIN_PHASE_MS:
        close_ms = K_MIN_PHASE_MS
        open_ms = max(K_MIN_PHASE_MS, cycle_ms - close_ms)

    # As frame_emit.cpp: a fixed fade, hard edges for the trill (the pack
    # setting trillModulationFadeMs was dead there and is gone, #183).
    micro_fade = 0.25

    base_voice_amp = base.voiceAmplitude
    base_fric_amp = base.fricationAmplitude
    start_pitch = base.voicePitch
    pitch_delta = base.endVoicePitch - start_pitch

    remaining = total_dur
    pos = 0.0
    high_phase = True
    first_phase = True

    while remaining > 1e-9:
        phase_dur = open_ms if high_phase else close_ms
        if phase_dur > remaining:
            phase_dur = remaining

        t0 = (pos / total_dur) if total_dur > 0 else 0
        t1 = ((pos + phase_dur) / total_dur) if total_dur > 0 else 1

        seg = base.copy()
        seg.voicePitch = start_pitch + pitch_delta * t0
        seg.endVoicePitch = start_pitch + pitch_delta * t1

        if not high_phase:
            seg.voiceAmplitude = base_voice_amp * K_CLOSE_FACTOR
            if base_fric_amp > 0:
                seg.fricationAmplitude = max(base_fric_amp, K_FRIC_FLOOR)

        fade_in = token.fade_ms if first_phase else micro_fade
        if fade_in <= 0:
            fade_in = micro_fade
        if fade_in > phase_dur:
            fade_in = phase_dur

        recorder.queue_frame(seg, duration_ms=phase_dur, fade_ms=fade_in,
                             label=token.label)

        remaining -= phase_dur
        pos += phase_dur
        high_phase = not high_phase
        first_phase = False


# =============================================================================
# Main pipeline (refactored: tokenize → build → collapse → emit)
# =============================================================================

def process_ipa(
    ipa: str,
    pack: PackSet,
    f0: float = 140.0,
    speed: float = 1.0,
    sample_rate: int = 16000,
) -> tuple[list[TrajectoryPoint], list[str]]:
    """
    Convert IPA string to trajectory points using pack parameters.
    Returns (points, ipa_tokens).

    Pipeline: tokenize_ipa → build_tokens → collapse_diphthongs → emit_micro_frames
    """
    ipa_tokens = tokenize_ipa(ipa, set(pack.phonemes.keys()))

    # Phase 1: Build emission tokens from IPA
    etokens = build_tokens(ipa_tokens, pack, f0, speed)

    # Phase 2: Collapse tied diphthong pairs
    etokens = collapse_diphthongs(etokens, pack)

    # Phase 3: Emit micro-frames into recorder
    recorder = TrajectoryRecorder(sample_rate=sample_rate, resolution_ms=0.5)
    emit_micro_frames(etokens, pack, speed, recorder)

    points = recorder.run()
    return points, ipa_tokens


def write_wav(path: Path, audio: np.ndarray, sample_rate: int):
    """Write audio to WAV file."""
    if audio.size == 0:
        audio = np.zeros(1, dtype=np.float32)

    peak = float(np.max(np.abs(audio)))
    if peak < 1e-9:
        peak = 1.0
    audio = audio / peak * 0.85

    pcm = np.clip(audio * 32767.0, -32767.0, 32767.0).astype(np.int16)

    with wave.open(str(path), "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sample_rate)
        wf.writeframes(pcm.tobytes())


# =============================================================================
# CLI
# =============================================================================

def main():
    ap = argparse.ArgumentParser(description="Formant Trajectory Visualizer for TGSpeechBox")
    ap.add_argument("--packs", required=True, help="Path to packs folder (contains packs/phonemes.yaml)")
    ap.add_argument("--lang", default="default", help="Language tag (e.g., en-us, hu, pl)")
    ap.add_argument("--voice", default="en-gb", help="eSpeak voice for --text")
    ap.add_argument("--text", help="Text to convert via eSpeak")
    ap.add_argument("--ipa", help="IPA string directly")
    ap.add_argument("--f0", type=float, default=140.0, help="Base pitch in Hz")
    ap.add_argument("--speed", type=float, default=1.0, help="Speed multiplier")
    ap.add_argument("--sr", type=int, default=16000, help="Sample rate")
    ap.add_argument("--out", help="Output PNG path for trajectory plot")
    ap.add_argument("--vowel-space", help="Output PNG path for vowel space plot")
    ap.add_argument("--wav", help="Output WAV path for synthesized audio")
    ap.add_argument("--show", action="store_true", help="Show plots interactively")
    ap.add_argument("--dump-settings", action="store_true", help="Dump language pack settings")
    args = ap.parse_args()

    # Load pack with all language parameters
    try:
        pack = load_pack_set(args.packs, args.lang)
    except FileNotFoundError as e:
        print(f"ERROR: {e}")
        return 1

    print(f"Loaded language: {pack.lang.lang_tag}")
    print(f"Phonemes: {len(pack.phonemes)}")

    # Show key settings being used
    lp = pack.lang
    print(f"\nKey settings:")
    print(f"  Stop closure mode: {lp.stop_closure_mode}")
    print(f"  Coarticulation: {'enabled' if lp.coarticulation_enabled else 'disabled'} (strength={lp.coarticulation_strength})")
    print(f"  Boundary smoothing: {'enabled' if lp.boundary_smoothing_enabled else 'disabled'}")
    print(f"  Primary stress div: {lp.primary_stress_div}")
    print(f"  Lengthened scale: {lp.lengthened_scale}")

    if args.dump_settings:
        print("\n" + format_pack_summary(pack))
        return 0

    # Get IPA
    if args.ipa:
        ipa = args.ipa
    elif args.text:
        ipa = espeak_ipa(args.voice, args.text)
    else:
        print("\nERROR: Provide --text or --ipa")
        return 1

    print(f"\nIPA: {ipa}")

    # Process
    points, tokens = process_ipa(
        ipa, pack,
        f0=args.f0, speed=args.speed, sample_rate=args.sr
    )
    print(f"Tokens: {' '.join([t for t in tokens if t.strip()])}")
    print(f"Trajectory points: {len(points)}")
    print(f"Duration: {points[-1].time_ms:.1f} ms" if points else "0 ms")

    # Plot trajectory
    if args.out or args.show:
        fig = plot_formant_trajectory(points, title=f"Formant Trajectory: {ipa}")
        if fig:
            if args.out:
                fig.savefig(args.out, dpi=150, bbox_inches="tight")
                print(f"Saved trajectory: {args.out}")
            if args.show:
                plt.show()

    # Plot vowel space
    if args.vowel_space or args.show:
        fig = plot_vowel_space(points, title=f"Vowel Space: {ipa}")
        if fig:
            if args.vowel_space:
                fig.savefig(args.vowel_space, dpi=150, bbox_inches="tight")
                print(f"Saved vowel space: {args.vowel_space}")
            if args.show:
                plt.show()

    # Synthesize audio
    if args.wav:
        print("Synthesizing audio...")
        audio = synthesize_from_trajectory(points, sample_rate=args.sr)
        write_wav(Path(args.wav), audio, args.sr)
        print(f"Saved audio: {args.wav}")

    return 0


if __name__ == "__main__":
    exit(main())
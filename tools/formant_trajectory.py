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
import wave
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable, Optional

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
    ):
        """Queue a frame with timing in milliseconds."""
        min_samples = int(duration_ms * self.sample_rate / 1000.0)
        fade_samples = int(fade_ms * self.sample_rate / 1000.0)
        self.fm.queue_frame(frame, min_samples, fade_samples, label=label)

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
    def __init__(self, sample_rate: int, anti: bool = False):
        self.sample_rate = sample_rate
        self.anti = anti
        self.frequency = 0.0
        self.bandwidth = 0.0
        self.a = 0.0
        self.b = 0.0
        self.c = 0.0
        self.p1 = 0.0
        self.p2 = 0.0
        self.set_once = False

    def reset(self):
        self.p1 = 0.0
        self.p2 = 0.0
        self.set_once = False

    def set_params(self, frequency: float, bandwidth: float):
        if (not self.set_once) or (frequency != self.frequency) or (bandwidth != self.bandwidth):
            self.frequency = frequency
            self.bandwidth = bandwidth

            if bandwidth <= 0:
                bandwidth = 50.0

            r = math.exp(-math.pi / self.sample_rate * bandwidth)
            self.c = -(r * r)
            self.b = r * math.cos((2 * math.pi / self.sample_rate) * -frequency) * 2.0
            self.a = 1.0 - self.b - self.c

            if self.anti and frequency != 0:
                self.a = 1.0 / self.a
                self.c *= -self.a
                self.b *= -self.a

            self.set_once = True

    def resonate(self, inp: float, frequency: float, bandwidth: float) -> float:
        self.set_params(frequency, bandwidth)
        out = self.a * inp + self.b * self.p1 + self.c * self.p2
        self.p2 = self.p1
        self.p1 = inp if self.anti else out
        return out


class SimpleSynthesizer:
    """
    Simplified synthesizer for audio preview.
    Mirrors the essential parts of speechWaveGenerator.cpp.
    """

    # Tuning constants from speechWaveGenerator.cpp
    K_BASE_PEAK_POS = 0.91
    K_RADIATION_MIX = 1.0
    K_FRIC_NOISE_SCALE = 0.175

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

        self.last_flow = 0.0
        self.last_voiced_in = 0.0
        self.last_voiced_out = 0.0
        self.last_input = 0.0
        self.last_output = 0.0
        self.glottis_open = False

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
        self.last_voiced_in = 0.0
        self.last_voiced_out = 0.0
        self.last_input = 0.0
        self.last_output = 0.0
        self.smooth_pre_gain = 0.0

    def generate_sample(self, f: Frame) -> float:
        """Generate one audio sample from a frame."""
        # Vibrato
        vibrato = (math.sin(self.vibrato_gen.get_next(f.vibratoSpeed) * 2 * math.pi) *
                   0.06 * f.vibratoPitchOffset) + 1.0
        pitch_hz = f.voicePitch * vibrato

        cycle_pos = self.pitch_gen.get_next(pitch_hz if pitch_hz > 0 else 0)

        aspiration = self.asp_gen.get_next() * 0.1

        # Glottal open quotient
        effective_oq = f.glottalOpenQuotient
        if effective_oq <= 0:
            effective_oq = 0.4
        effective_oq = max(0.10, min(0.95, effective_oq))

        self.glottis_open = (pitch_hz > 0) and (cycle_pos >= effective_oq)

        # Glottal flow
        flow = 0.0
        if self.glottis_open:
            open_len = 1.0 - effective_oq
            if open_len < 0.0001:
                open_len = 0.0001

            peak_pos = self.K_BASE_PEAK_POS
            dt = pitch_hz / self.sample_rate if pitch_hz > 0 else 0
            denom = max(0.0001, open_len - dt)
            phase = max(0.0, min(1.0, (cycle_pos - effective_oq) / denom))

            if phase < peak_pos:
                flow = 0.5 * (1.0 - math.cos(phase * math.pi / peak_pos))
            else:
                flow = 0.5 * (1.0 + math.cos((phase - peak_pos) * math.pi / (1.0 - peak_pos)))

        flow_scale = 1.6
        flow *= flow_scale

        d_flow = flow - self.last_flow
        self.last_flow = flow
        voiced_src = flow + d_flow * self.K_RADIATION_MIX

        # Turbulence
        turbulence = aspiration * f.voiceTurbulenceAmplitude
        if self.glottis_open:
            flow01 = max(0, min(1, flow / flow_scale))
            turbulence *= flow01 ** 1.5
        else:
            turbulence = 0.0

        voiced_in = (voiced_src + turbulence) * f.voiceAmplitude

        # DC blocker
        dc_pole = 0.9995
        voiced = voiced_in - self.last_voiced_in + dc_pole * self.last_voiced_out
        self.last_voiced_in = voiced_in
        self.last_voiced_out = voiced

        asp_out = aspiration * f.aspirationAmplitude

        # PreFormant gain smoothing
        target = f.preFormantGain
        alpha = self.pre_gain_attack_alpha if target > self.smooth_pre_gain else self.pre_gain_release_alpha
        self.smooth_pre_gain += (target - self.smooth_pre_gain) * alpha

        voice = asp_out + voiced

        # Cascade formants
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
        filtered = out - self.last_input + 0.9995 * self.last_output
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

                audio[sample_idx] = synth.generate_sample(interp_frame)
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
            base_fade = lp.boundary_smoothing_vowel_to_stop_fade_ms
        elif (prev_pdef.is_stop or prev_pdef.is_affricate) and (pdef.is_vowel or pdef.is_semivowel):
            base_fade = lp.boundary_smoothing_stop_to_vowel_fade_ms
        elif prev_vowel_like and cur_fric:
            base_fade = lp.boundary_smoothing_vowel_to_fric_fade_ms

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

def process_ipa(
    ipa: str,
    pack: PackSet,
    f0: float = 140.0,
    speed: float = 1.0,
    sample_rate: int = 16000,
) -> tuple[list[TrajectoryPoint], list[str]]:
    """
    Convert IPA string to trajectory points using pack parameters.
    Returns (points, tokens).
    """
    tokens = tokenize_ipa(ipa, set(pack.phonemes.keys()))

    recorder = TrajectoryRecorder(sample_rate=sample_rate, resolution_ms=0.5)

    stress = 0  # 0=none, 1=primary, 2=secondary
    tie_next = False
    lengthened = False
    prev_pdef: Optional[PhonemeDef] = None

    for tok in tokens:
        if tok == " ":
            # Word gap - small silence
            recorder.queue_frame(None, duration_ms=35.0 / speed, fade_ms=5.0, label=" ")
            prev_pdef = None
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

        # Check for stop closure gap
        gap_ms, gap_fade = get_stop_closure_gap(pdef, pack, speed, prev_pdef)
        if gap_ms > 0:
            recorder.queue_frame(None, duration_ms=gap_ms, fade_ms=gap_fade, label="")

        # Get duration using pack parameters
        dur = get_phoneme_duration_ms(pdef, pack, speed, stress, lengthened)

        # Tie (offglide) shortening
        if tie_next:
            dur *= 0.4
            tie_next = False

        # Pitch adjustment for stress
        pitch = f0
        if stress == 1:
            pitch *= 1.05
        elif stress == 2:
            pitch *= 1.02
        stress = 0
        lengthened = False

        # Get fade using pack parameters
        fade = get_fade_ms(pdef, pack, speed, prev_pdef)

        # Build frame using pack defaults
        frame = build_frame_from_phoneme(pdef, pack, f0=pitch)
        frame.endVoicePitch = pitch

        recorder.queue_frame(frame, duration_ms=dur, fade_ms=fade, label=tok)
        prev_pdef = pdef

    points = recorder.run()
    return points, tokens


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
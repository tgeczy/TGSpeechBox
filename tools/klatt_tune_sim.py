#!/usr/bin/env python3
"""
klatt_tune_sim.py  —  DSP v6 edition

Self-contained Klatt-style formant synth simulator with:
- The "engine" glottal model (mirrors speechWaveGenerator.cpp)
- A Rosenberg-style glottal flow model (Oq + Sq) for comparison
- Full FrameEx support (DSP v5/v6):
    * creakiness, breathiness, jitter, shimmer, sharpness
    * endCf1-3, endPf1-3 (within-segment formant trajectories)
    * spectral tilt (voiced + aspiration, driven by breathiness)

Why this exists:
- You can A/B how phoneme-level params change spectral balance and perceived
  quality, without needing real-time playback from the DLL.

Notes on glottalOpenQuotient:
- glottisOpen is true when cyclePos >= glottalOpenQuotient.
- The glottis is open for (1 - glottalOpenQuotient) of the cycle.
- Example: glottalOpenQuotient=0.40 -> open ~60% of the cycle.

Dependencies:
- numpy (for FFT + arrays)

Example:
  python klatt_tune_sim.py --phonemes packs/phonemes.yaml --phoneme a --out out.wav
  python klatt_tune_sim.py --phonemes packs/phonemes.yaml --phoneme a --creakiness 0.6 --out creaky.wav
  python klatt_tune_sim.py --phonemes packs/phonemes.yaml --phoneme a --breathiness 0.5 --out breathy.wav
  python klatt_tune_sim.py --phonemes packs/phonemes.yaml --phoneme a --end-cf1 700 --end-cf2 1800 --out ramp.wav
"""

from __future__ import annotations

import argparse
import copy
import math
import wave
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Any, Tuple, Optional

import numpy as np


# ─────────────────────────────────────────────────────────────────────────────
# Minimal YAML loader (handles phonemes.yaml structure)
# ─────────────────────────────────────────────────────────────────────────────

def _parse_simple_yaml_map_of_maps(path: str) -> Dict[str, Dict[str, Any]]:
    """
    Parses the phonemes.yaml structure:
      phonemes:
        KEY:
          field: value
          ...
    """
    text = Path(path).read_text(encoding="utf-8").splitlines()
    phonemes: Dict[str, Dict[str, Any]] = {}
    current_key: Optional[str] = None

    for raw in text:
        line = raw.split("#", 1)[0].rstrip()
        if not line.strip():
            continue
        indent = len(line) - len(line.lstrip(" "))
        s = line.strip()

        if indent == 0:
            continue

        if indent == 2 and s.endswith(":"):
            key = s[:-1].strip()
            if len(key) >= 2 and key[0] == key[-1] and key[0] in ("'", '"'):
                key = key[1:-1]
            current_key = key
            phonemes[current_key] = {}
            continue

        if indent == 4 and ":" in s and current_key is not None:
            field_name, valstr = s.split(":", 1)
            field_name = field_name.strip()
            valstr = valstr.strip()

            if valstr.startswith("["):
                valstr = valstr.strip("[]")
                parts = [x.strip() for x in valstr.split(",") if x.strip()]
                phonemes[current_key][field_name] = parts
            elif valstr in ("true", "True"):
                phonemes[current_key][field_name] = True
            elif valstr in ("false", "False"):
                phonemes[current_key][field_name] = False
            else:
                try:
                    if "." in valstr:
                        phonemes[current_key][field_name] = float(valstr)
                    else:
                        phonemes[current_key][field_name] = int(valstr)
                except ValueError:
                    if len(valstr) >= 2 and valstr[0] == valstr[-1] and valstr[0] in ("'", '"'):
                        valstr = valstr[1:-1]
                    phonemes[current_key][field_name] = valstr

    return phonemes


# ─────────────────────────────────────────────────────────────────────────────
# DSP building blocks (mirrors speechWaveGenerator.cpp)
# ─────────────────────────────────────────────────────────────────────────────

PITWO = math.pi * 2.0

# Breathiness macro tuning (matches C++ constants)
K_BREATHINESS_TILT_MAX_DB = 6.0
K_BREATHINESS_ASP_TILT_MAX_DB = -8.0
K_BREATHINESS_TILT_SMOOTH_MS = 8.0

# Radiation
K_RADIATION_DERIV_GAIN_BASE = 5.0
K_RADIATION_DERIV_GAIN_REF_SR = 22050.0

# Turbulence
K_TURBULENCE_FLOW_POWER = 1.5


def _clamp(v: float, lo: float, hi: float) -> float:
    if v < lo:
        return lo
    if v > hi:
        return hi
    return v


class FastRandom:
    """Thread-local LCG PRNG (mirrors C++ FastRandom)."""
    def __init__(self, seed: int = 12345):
        self.seed = seed & 0xFFFFFFFF

    def next_double(self) -> float:
        self.seed = (self.seed * 1664525 + 1013904223) & 0xFFFFFFFF
        return (self.seed >> 1) * (1.0 / 2147483648.0)

    def next_bipolar(self) -> float:
        self.seed = (self.seed * 1664525 + 1013904223) & 0xFFFFFFFF
        s = self.seed if self.seed < 0x80000000 else self.seed - 0x100000000
        return s * (1.0 / 2147483648.0)


class NoiseGenerator:
    def __init__(self, seed: int = 54321):
        self.last_value = 0.0
        self.rng = FastRandom(seed)

    def reset(self) -> None:
        self.last_value = 0.0

    def get_next(self) -> float:
        self.last_value = (self.rng.next_double() - 0.5) + 0.75 * self.last_value
        return self.last_value

    def white(self) -> float:
        return self.rng.next_bipolar()


class FrequencyGenerator:
    def __init__(self, sample_rate: int):
        self.sample_rate = sample_rate
        self.last_cycle_pos = 0.0

    def reset(self) -> None:
        self.last_cycle_pos = 0.0

    def get_next(self, frequency_hz: float) -> float:
        cycle_pos = math.fmod((frequency_hz / self.sample_rate) + self.last_cycle_pos, 1.0)
        self.last_cycle_pos = cycle_pos
        return cycle_pos


# ─────────────────────────────────────────────────────────────────────────────
# Frame data structures
# ─────────────────────────────────────────────────────────────────────────────

@dataclass
class FrameEx:
    """Optional per-frame voice quality parameters (DSP v5+).
    Matches speechPlayer_frameEx_t from frame.h."""
    creakiness: float = 0.0
    breathiness: float = 0.0
    jitter: float = 0.0
    shimmer: float = 0.0
    sharpness: float = 0.0
    endCf1: float = float('nan')
    endCf2: float = float('nan')
    endCf3: float = float('nan')
    endPf1: float = float('nan')
    endPf2: float = float('nan')
    endPf3: float = float('nan')


@dataclass
class Frame:
    """Matches speechPlayer_frame_t from frame.h."""
    voicePitch: float = 120.0
    vibratoPitchOffset: float = 0.0
    vibratoSpeed: float = 0.0
    voiceTurbulenceAmplitude: float = 0.0
    glottalOpenQuotient: float = 0.0
    voiceAmplitude: float = 0.0
    aspirationAmplitude: float = 0.0
    fricationAmplitude: float = 0.0
    cf1: float = 0.0; cf2: float = 0.0; cf3: float = 0.0
    cf4: float = 0.0; cf5: float = 0.0; cf6: float = 0.0
    cfN0: float = 0.0; cfNP: float = 0.0
    cb1: float = 0.0; cb2: float = 0.0; cb3: float = 0.0
    cb4: float = 0.0; cb5: float = 0.0; cb6: float = 0.0
    cbN0: float = 0.0; cbNP: float = 0.0
    caNP: float = 0.0
    pf1: float = 0.0; pf2: float = 0.0; pf3: float = 0.0
    pf4: float = 0.0; pf5: float = 0.0; pf6: float = 0.0
    pb1: float = 0.0; pb2: float = 0.0; pb3: float = 0.0
    pb4: float = 0.0; pb5: float = 0.0; pb6: float = 0.0
    pa1: float = 0.0; pa2: float = 0.0; pa3: float = 0.0
    pa4: float = 0.0; pa5: float = 0.0; pa6: float = 0.0
    parallelBypass: float = 0.0
    preFormantGain: float = 2.0
    outputGain: float = 1.5


# ─────────────────────────────────────────────────────────────────────────────
# VoiceGenerator — faithfully mirrors C++ VoiceGenerator
# ─────────────────────────────────────────────────────────────────────────────

class EngineVoiceSource:
    """Mirrors the C++ VoiceGenerator with full FrameEx support."""

    def __init__(self, sample_rate: int, seed: int = 0):
        self.sample_rate = sample_rate
        self.pitch_gen = FrequencyGenerator(sample_rate)
        self.vibrato_gen = FrequencyGenerator(sample_rate)
        self.asp_gen = NoiseGenerator(seed=seed + 54321)
        self.last_flow = 0.0
        self.last_voiced_in = 0.0
        self.last_voiced_out = 0.0
        self.last_voiced_src = 0.0
        self.last_asp_out = 0.0
        self.glottis_open = False

        # Per-cycle jitter/shimmer
        self.last_cycle_pos = 0.0
        self.jitter_mul = 1.0
        self.shimmer_mul = 1.0
        self.jitter_shimmer_rng = FastRandom(98765 + seed)

        # Voicing pulse defaults
        self.voicing_peak_pos = 0.91
        self.speed_quotient = 2.0

        # Spectral tilt (one-pole)
        self.tilt_target_db = 0.0
        self.tilt_db = 0.0
        self.tilt_pole = 0.0
        self.tilt_pole_target = 0.0
        self.tilt_state = 0.0
        self.tilt_ref_hz = min(3000.0, 0.5 * sample_rate * 0.95)
        self.tilt_last_for_targets = 1e9
        self.tilt_tl_alpha = 1.0 - math.exp(-1.0 / (sample_rate * 8.0 * 0.001))
        self.tilt_pole_alpha = 1.0 - math.exp(-1.0 / (sample_rate * 5.0 * 0.001))

        # Per-frame tilt offset from breathiness
        self.per_frame_tilt_offset = 0.0
        self.per_frame_tilt_offset_target = 0.0
        self.per_frame_tilt_offset_alpha = 1.0 - math.exp(-1.0 / (sample_rate * K_BREATHINESS_TILT_SMOOTH_MS * 0.001))

        # Aspiration/frication tilt
        self.asp_tilt_target_db = 0.0
        self.asp_tilt_smoothed_db = 0.0
        self.asp_tilt_smooth_alpha = 1.0 - math.exp(-1.0 / (sample_rate * 10.0 * 0.001))
        self.asp_lp_state = 0.0
        self.fric_lp_state = 0.0
        self.per_frame_asp_tilt_offset = 0.0
        self.per_frame_asp_tilt_offset_target = 0.0
        self.per_frame_asp_tilt_offset_alpha = self.per_frame_tilt_offset_alpha

        # Radiation
        self.radiation_deriv_gain = K_RADIATION_DERIV_GAIN_BASE * (sample_rate / K_RADIATION_DERIV_GAIN_REF_SR)
        self.radiation_mix = 0.0

        # Aspiration gain smoothing
        self.smooth_asp_amp = 0.0
        self.smooth_asp_amp_init = False
        self.asp_attack_coeff = 1.0 - math.exp(-1.0 / (0.001 * 1.0 * sample_rate))
        self.asp_release_coeff = 1.0 - math.exp(-1.0 / (0.001 * 3.0 * sample_rate))

        # Voiced pre-emphasis
        self.voiced_pre_emph_a = 0.92
        self.voiced_pre_emph_mix = 0.35

    def reset(self) -> None:
        self.pitch_gen.reset()
        self.vibrato_gen.reset()
        self.asp_gen.reset()
        self.last_flow = 0.0
        self.last_voiced_in = 0.0
        self.last_voiced_out = 0.0
        self.last_voiced_src = 0.0
        self.last_asp_out = 0.0
        self.glottis_open = False
        self.last_cycle_pos = 0.0
        self.jitter_mul = 1.0
        self.shimmer_mul = 1.0
        self.tilt_state = 0.0
        self.asp_lp_state = 0.0
        self.fric_lp_state = 0.0
        self.per_frame_tilt_offset = 0.0
        self.per_frame_tilt_offset_target = 0.0
        self.per_frame_asp_tilt_offset = 0.0
        self.per_frame_asp_tilt_offset_target = 0.0
        self.smooth_asp_amp = 0.0
        self.smooth_asp_amp_init = False

    # ── Tilt internals (matches C++ exactly) ──

    def _calc_pole_for_tilt_db(self, ref_hz: float, tilt_db: float) -> float:
        if abs(tilt_db) < 1e-5:
            return 0.0
        if tilt_db > 0.0:
            nyq = 0.5 * self.sample_rate
            r = max(1.0, min(ref_hz, nyq * 0.95))
            g = 10.0 ** (-tilt_db / 20.0)
            g2 = g * g
            w = PITWO * r / self.sample_rate
            cosw = math.cos(w)
            A = g2 - 1.0
            B = 2.0 * (1.0 - g2 * cosw)
            disc = B * B - 4.0 * A * A
            if disc < 0.0:
                return 0.0
            sqrt_disc = math.sqrt(disc)
            denom = 2.0 * A
            if abs(denom) < 1e-18:
                return 0.0
            a1 = (-B + sqrt_disc) / denom
            a2 = (-B - sqrt_disc) / denom
            ok1 = 0.0 <= a1 < 1.0
            ok2 = 0.0 <= a2 < 1.0
            if ok1 and ok2:
                a = min(a1, a2)
            elif ok1:
                a = a1
            elif ok2:
                a = a2
            else:
                a = a1
            return _clamp(a, 0.0, 0.9999)
        else:
            target_gain = 10.0 ** (-tilt_db / 20.0)
            a = (1.0 - target_gain) / (1.0 + target_gain)
            return _clamp(a, -0.9, -0.0001)

    def _update_tilt_targets(self, tl_db: float):
        tl = _clamp(tl_db, -24.0, 24.0)
        self.tilt_pole_target = self._calc_pole_for_tilt_db(self.tilt_ref_hz, tl)
        self.radiation_mix = _clamp(-tl / 10.0, 0.0, 1.0) if tl < 0.0 else 0.0

    def _apply_tilt(self, inp: float) -> float:
        self.per_frame_tilt_offset += (self.per_frame_tilt_offset_target - self.per_frame_tilt_offset) * self.per_frame_tilt_offset_alpha
        effective = self.tilt_target_db + self.per_frame_tilt_offset
        self.tilt_db += (effective - self.tilt_db) * self.tilt_tl_alpha
        if abs(self.tilt_db - self.tilt_last_for_targets) > 0.01:
            self._update_tilt_targets(self.tilt_db)
            self.tilt_last_for_targets = self.tilt_db
        self.tilt_pole += (self.tilt_pole_target - self.tilt_pole) * self.tilt_pole_alpha
        out = (1.0 - self.tilt_pole) * inp + self.tilt_pole * self.tilt_state
        self.tilt_state = out
        return out

    def _one_pole_alpha_from_fc(self, fc_hz: float) -> float:
        fc = max(20.0, min(fc_hz, 0.5 * self.sample_rate * 0.95))
        return math.exp(-PITWO * fc / self.sample_rate)

    def _apply_asp_tilt(self, x: float) -> float:
        self.per_frame_asp_tilt_offset += (self.per_frame_asp_tilt_offset_target - self.per_frame_asp_tilt_offset) * self.per_frame_asp_tilt_offset_alpha
        self.asp_tilt_smoothed_db += (self.asp_tilt_target_db - self.asp_tilt_smoothed_db) * self.asp_tilt_smooth_alpha
        t = self.asp_tilt_smoothed_db + self.per_frame_asp_tilt_offset
        amt = _clamp(abs(t) / 18.0, 0.0, 1.0) ** 0.65
        fc = 6000.0 - 4500.0 * amt
        a = self._one_pole_alpha_from_fc(fc)
        self.asp_lp_state = (1.0 - a) * x + a * self.asp_lp_state
        lp = self.asp_lp_state
        hp = x - lp
        return x + hp * (1.25 * (amt if t > 0.0 else 0.0) - (amt if t < 0.0 else 0.0))

    def apply_frication_tilt(self, x: float) -> float:
        t = self.asp_tilt_smoothed_db
        amt = _clamp(abs(t) / 18.0, 0.0, 1.0) ** 0.65
        fc = 6000.0 - 4500.0 * amt
        a = self._one_pole_alpha_from_fc(fc)
        self.fric_lp_state = (1.0 - a) * x + a * self.fric_lp_state
        lp = self.fric_lp_state
        hp = x - lp
        return x + hp * (1.25 * (amt if t > 0.0 else 0.0) - (amt if t < 0.0 else 0.0))

    # ── Main sample generation ──

    def get_next(self, f: Frame, fx: Optional[FrameEx] = None) -> float:
        sr = self.sample_rate

        # Read FrameEx values
        creakiness = breathiness = jitter_param = shimmer_param = frame_ex_sharpness = 0.0
        if fx is not None:
            creakiness = _clamp(fx.creakiness if math.isfinite(fx.creakiness) else 0.0, 0.0, 1.0)
            breathiness = _clamp(fx.breathiness if math.isfinite(fx.breathiness) else 0.0, 0.0, 1.0)
            jitter_param = _clamp(fx.jitter if math.isfinite(fx.jitter) else 0.0, 0.0, 1.0)
            shimmer_param = _clamp(fx.shimmer if math.isfinite(fx.shimmer) else 0.0, 0.0, 1.0)
            frame_ex_sharpness = _clamp(fx.sharpness if math.isfinite(fx.sharpness) else 0.0, 0.0, 15.0)

            # Perceptual curve for breathiness
            if breathiness > 0.0:
                breathiness = breathiness ** 0.55

            self.per_frame_tilt_offset_target = breathiness * K_BREATHINESS_TILT_MAX_DB
            self.per_frame_asp_tilt_offset_target = breathiness * K_BREATHINESS_ASP_TILT_MAX_DB
        else:
            self.per_frame_tilt_offset_target = 0.0
            self.per_frame_asp_tilt_offset_target = 0.0

        # ── Pitch ──
        base_pitch = max(0.0, f.voicePitch) if math.isfinite(f.voicePitch) else 0.0
        vibrato = (math.sin(self.vibrato_gen.get_next(f.vibratoSpeed) * PITWO) * 0.06 * f.vibratoPitchOffset) + 1.0
        pitch_hz = base_pitch * vibrato

        if creakiness > 0.0:
            pitch_hz *= (1.0 - 0.12 * creakiness)

        if pitch_hz <= 0.0:
            self.jitter_mul = 1.0
            self.shimmer_mul = 1.0

        pitch_hz *= self.jitter_mul
        cycle_pos = self.pitch_gen.get_next(pitch_hz if pitch_hz > 0.0 else 0.0)

        cycle_wrapped = (pitch_hz > 0.0) and (cycle_pos < self.last_cycle_pos)
        self.last_cycle_pos = cycle_pos

        if cycle_wrapped:
            jitter_rel = (jitter_param * 0.15) + (creakiness * 0.05)
            self.jitter_mul = max(0.2, 1.0 + self.jitter_shimmer_rng.next_bipolar() * jitter_rel) if jitter_rel > 0 else 1.0
            shimmer_rel = (shimmer_param * 0.70) + (creakiness * 0.12)
            self.shimmer_mul = max(0.0, 1.0 + self.jitter_shimmer_rng.next_bipolar() * shimmer_rel) if shimmer_rel > 0 else 1.0

        # ── Aspiration noise ──
        asp_base = 0.10 + (0.15 * breathiness)
        aspiration = self.asp_gen.white() * asp_base
        aspiration = self._apply_asp_tilt(aspiration)

        # ── Open quotient ──
        effective_oq = f.glottalOpenQuotient
        if effective_oq <= 0.0:
            effective_oq = 0.4
        effective_oq = _clamp(effective_oq, 0.10, 0.95)

        if creakiness > 0.0:
            effective_oq = min(0.95, effective_oq + 0.10 * creakiness)
        if breathiness > 0.0:
            effective_oq = max(0.05, effective_oq - 0.35 * breathiness)

        self.glottis_open = (pitch_hz > 0.0) and (cycle_pos >= effective_oq)

        # ── Glottal flow ──
        flow = 0.0
        if self.glottis_open:
            open_len = max(0.0001, 1.0 - effective_oq)
            sq_peak_delta = 0.0
            if self.speed_quotient != 2.0:
                sq_peak_delta = (self.speed_quotient / (1.0 + self.speed_quotient) - 2.0 / 3.0) * 0.6
            peak_pos = self.voicing_peak_pos + sq_peak_delta + 0.02 * breathiness - 0.05 * creakiness
            dt = (pitch_hz / sr) if pitch_hz > 0.0 else 0.0
            phase = _clamp((cycle_pos - effective_oq) / max(0.0001, open_len - dt), 0.0, 1.0)
            if pitch_hz > 0.0:
                period_samples = sr / pitch_hz
                peak_pos = max(0.50, min(peak_pos, 1.0 - min(0.5, 2.0 / (period_samples * open_len))))

            # Cosine
            if phase < peak_pos:
                flow_cos = 0.5 * (1.0 - math.cos(phase * math.pi / peak_pos))
            else:
                flow_cos = 0.5 * (1.0 + math.cos((phase - peak_pos) * math.pi / (1.0 - peak_pos)))

            # LF-inspired
            if phase < peak_pos:
                t = phase / peak_pos
                op = _clamp(2.0 + (self.speed_quotient - 2.0) * 0.5, 1.0, 4.0)
                flow_lf = (t ** op) * (3.0 - 2.0 * t)
            else:
                t = (phase - peak_pos) / (1.0 - peak_pos)
                if sr >= 44100:
                    bs = 10.0
                elif sr >= 32000:
                    bs = 8.0
                elif sr >= 22050:
                    bs = 4.0
                elif sr >= 16000:
                    bs = 3.0
                else:
                    bs = 2.5
                if frame_ex_sharpness > 0.0:
                    bs = _clamp(bs * frame_ex_sharpness, 1.0, 15.0)
                sq_f = _clamp(0.4 + (self.speed_quotient - 0.5) * 0.4, 0.3, 2.0)
                flow_lf = (1.0 - t) ** (bs * sq_f)

            # SR-dependent blend
            if sr <= 11025:
                lb = 0.30
            elif sr >= 16000:
                lb = 1.0
            else:
                lb = 0.30 + 0.70 * (sr - 11025) / (16000.0 - 11025.0)
            sm = frame_ex_sharpness if frame_ex_sharpness > 0.0 else 1.0
            lb = _clamp(lb * (_clamp(sm, 0.25, 3.0) ** 0.25), 0.0,
                        0.35 if sr <= 11025 else (0.85 if sr < 16000 else 1.0))
            flow = (1.0 - lb) * flow_cos + lb * flow_lf

        flow_scale = 1.6
        flow *= flow_scale
        dflow = flow - self.last_flow
        self.last_flow = flow

        # ── Radiation ──
        src_deriv = dflow * self.radiation_deriv_gain
        voiced_src = (1.0 - self.radiation_mix) * flow + self.radiation_mix * src_deriv

        # Pre-emphasis
        pre = voiced_src - self.voiced_pre_emph_a * self.last_voiced_src
        self.last_voiced_src = voiced_src
        voiced_src = (1.0 - self.voiced_pre_emph_mix) * voiced_src + self.voiced_pre_emph_mix * pre

        # Tilt
        voiced_src = self._apply_tilt(voiced_src)

        # ── Turbulence ──
        vta = _clamp(f.voiceTurbulenceAmplitude, 0.0, 1.0)
        if breathiness > 0.0:
            vta = _clamp(vta + 0.5 * breathiness, 0.0, 1.0)
        turbulence = aspiration * vta
        if self.glottis_open:
            turbulence *= _clamp(flow / flow_scale, 0.0, 1.0) ** K_TURBULENCE_FLOW_POWER
        else:
            turbulence = 0.0

        # ── Voice amplitude ──
        va = _clamp(f.voiceAmplitude, 0.0, 1.0)
        if creakiness > 0.0:
            va *= (1.0 - 0.35 * creakiness)
        if breathiness > 0.0:
            va *= (1.0 - 0.98 * breathiness)
        va *= self.shimmer_mul

        voiced_in = voiced_src * va + turbulence

        # DC blocker
        voiced = voiced_in - self.last_voiced_in + 0.9995 * self.last_voiced_out
        self.last_voiced_in = voiced_in
        self.last_voiced_out = voiced

        # ── Aspiration output ──
        taa = _clamp(f.aspirationAmplitude, 0.0, 1.0)
        if breathiness > 0.0:
            taa = _clamp(taa + breathiness, 0.0, 1.0)
        if not self.smooth_asp_amp_init:
            self.smooth_asp_amp = taa
            self.smooth_asp_amp_init = True
        else:
            c = self.asp_attack_coeff if taa > self.smooth_asp_amp else self.asp_release_coeff
            self.smooth_asp_amp += (taa - self.smooth_asp_amp) * c

        asp_out = aspiration * self.smooth_asp_amp
        self.last_asp_out = asp_out
        return asp_out + voiced


# ─────────────────────────────────────────────────────────────────────────────
# Rosenberg voice source (backward compatible)
# ─────────────────────────────────────────────────────────────────────────────

class RosenbergVoiceSource:
    def __init__(self, sample_rate: int, oq: float = 0.62, sq: float = 1.2, seed: int = 0):
        self.sample_rate = sample_rate
        self.oq = oq
        self.sq = sq
        self.pitch_gen = FrequencyGenerator(sample_rate)
        self.asp_gen = NoiseGenerator(seed=seed)
        self.last_flow = 0.0
        self.last_voiced_in = 0.0
        self.last_voiced_out = 0.0
        self.glottis_open = False
        self.last_asp_out = 0.0

    def reset(self):
        self.pitch_gen.reset()
        self.asp_gen.reset()
        self.last_flow = self.last_voiced_in = self.last_voiced_out = 0.0
        self.glottis_open = False
        self.last_asp_out = 0.0

    def get_next(self, f: Frame, fx: Optional[FrameEx] = None) -> float:
        oq = _clamp(self.oq, 0.10, 0.95)
        sq = max(0.20, self.sq)
        phase = self.pitch_gen.get_next(f.voicePitch if f.voicePitch > 0 else 0.0)
        aspiration = self.asp_gen.get_next() * 0.1
        Ta = oq * (sq / (sq + 1.0))
        Tc = oq
        if phase < 0:
            flow = 0.0
        elif phase < Ta:
            flow = 0.5 * (1.0 - math.cos(math.pi * phase / max(1e-6, Ta)))
        elif phase < Tc:
            flow = math.cos(math.pi / 2.0 * (phase - Ta) / max(1e-6, Tc - Ta))
        else:
            flow = 0.0
        self.glottis_open = flow > 0.0
        flow *= 1.6
        dflow = flow - self.last_flow
        self.last_flow = flow
        voiced_in = (flow + dflow) * f.voiceAmplitude + (aspiration * f.voiceTurbulenceAmplitude * (flow / 1.6 if self.glottis_open else 0.0))
        voiced = voiced_in - self.last_voiced_in + 0.9995 * self.last_voiced_out
        self.last_voiced_in = voiced_in
        self.last_voiced_out = voiced
        asp_out = aspiration * f.aspirationAmplitude
        self.last_asp_out = asp_out
        return asp_out + voiced

    def apply_frication_tilt(self, x: float) -> float:
        return x


# ─────────────────────────────────────────────────────────────────────────────
# Resonators
# ─────────────────────────────────────────────────────────────────────────────

class Resonator:
    def __init__(self, sample_rate: int, anti: bool = False):
        self.sample_rate = sample_rate
        self.anti = anti
        self.a = 1.0; self.b = 0.0; self.c = 0.0
        self.p1 = 0.0; self.p2 = 0.0
        self._freq = -1.0; self._bw = -1.0

    def set_params(self, freq: float, bw: float):
        if freq == self._freq and bw == self._bw:
            return
        self._freq = freq; self._bw = bw
        nyq = 0.5 * self.sample_rate
        if not math.isfinite(freq) or not math.isfinite(bw) or freq <= 0 or bw <= 0 or freq >= nyq:
            self.a, self.b, self.c = 1.0, 0.0, 0.0
            return
        r = math.exp(-math.pi / self.sample_rate * bw)
        self.c = -(r * r)
        self.b = r * math.cos(PITWO * freq / self.sample_rate) * 2.0
        self.a = 1.0 - self.b - self.c
        if self.anti:
            if abs(self.a) < 1e-12:
                self.a, self.b, self.c = 1.0, 0.0, 0.0
            else:
                inv_a = 1.0 / self.a
                self.a = inv_a
                self.b *= -inv_a
                self.c *= -inv_a

    def resonate(self, inp: float, freq: float, bw: float) -> float:
        self.set_params(freq, bw)
        out = self.a * inp + self.b * self.p1 + self.c * self.p2
        self.p2 = self.p1
        self.p1 = inp if self.anti else out
        return out

    def reset(self):
        self.p1 = self.p2 = 0.0
        self._freq = self._bw = -1.0


SWEEP_Q_MAX = {1: 10.0, 2: 18.0, 3: 20.0}
SWEEP_BW_MIN = {1: 30.0, 2: 40.0, 3: 60.0}

def _bw_for_sweep(freq: float, base_bw: float, n: int) -> float:
    q_max = SWEEP_Q_MAX.get(n, 20.0)
    bw_min = SWEEP_BW_MIN.get(n, 60.0)
    if not math.isfinite(freq) or not math.isfinite(base_bw) or freq <= 0 or base_bw <= 0:
        return base_bw
    bw = max(base_bw, freq / q_max)
    return _clamp(bw, bw_min, 1000.0)


class CascadeFormantGenerator:
    def __init__(self, sample_rate: int):
        self.sample_rate = sample_rate
        self.r1 = Resonator(sample_rate)
        self.r2 = Resonator(sample_rate)
        self.r3 = Resonator(sample_rate)
        self.r4 = Resonator(sample_rate)
        self.r5 = Resonator(sample_rate)
        self.r6 = Resonator(sample_rate)
        self.rN0 = Resonator(sample_rate, anti=True)
        self.rNP = Resonator(sample_rate)

    def reset(self):
        for r in (self.r1, self.r2, self.r3, self.r4, self.r5, self.r6, self.rN0, self.rNP):
            r.reset()

    def get_next(self, f: Frame, fx: Optional[FrameEx], glottis_open: bool, inp: float) -> float:
        inp /= 2.0
        n0 = self.rN0.resonate(inp, f.cfN0, f.cbN0)
        output = inp + (self.rNP.resonate(n0, f.cfNP, f.cbNP) - inp) * f.caNP

        cb1, cb2, cb3 = f.cb1, f.cb2, f.cb3
        if fx is not None:
            if math.isfinite(fx.endCf1): cb1 = _bw_for_sweep(f.cf1, cb1, 1)
            if math.isfinite(fx.endCf2): cb2 = _bw_for_sweep(f.cf2, cb2, 2)
            if math.isfinite(fx.endCf3): cb3 = _bw_for_sweep(f.cf3, cb3, 3)

        nyq = 0.5 * self.sample_rate
        def cfade(cf):
            if cf <= 0 or not math.isfinite(cf): return 1.0
            r = cf / nyq
            return 1.0 if r < 0.65 else (0.0 if r > 0.85 else 1.0 - (r - 0.65) / 0.20)

        for r, cf, cb in [(self.r6, f.cf6, f.cb6), (self.r5, f.cf5, f.cb5), (self.r4, f.cf4, f.cb4)]:
            pre = output
            output = r.resonate(output, cf, cb)
            fd = cfade(cf)
            output = pre + fd * (output - pre)

        output = self.r3.resonate(output, f.cf3, cb3)
        output = self.r2.resonate(output, f.cf2, cb2)
        output = self.r1.resonate(output, f.cf1, cb1)
        return output


class ParallelFormantGenerator:
    def __init__(self, sample_rate: int):
        self.sample_rate = sample_rate
        self.r1 = Resonator(sample_rate)
        self.r2 = Resonator(sample_rate)
        self.r3 = Resonator(sample_rate)
        self.r4 = Resonator(sample_rate)
        self.r5 = Resonator(sample_rate)
        self.r6 = Resonator(sample_rate)

    def reset(self):
        for r in (self.r1, self.r2, self.r3, self.r4, self.r5, self.r6):
            r.reset()

    def get_next(self, f: Frame, fx: Optional[FrameEx], glottis_open: bool, inp: float) -> float:
        inp /= 2.0
        pb1, pb2, pb3 = f.pb1, f.pb2, f.pb3
        if fx is not None:
            if math.isfinite(fx.endPf1): pb1 = _bw_for_sweep(f.pf1, pb1, 1)
            if math.isfinite(fx.endPf2): pb2 = _bw_for_sweep(f.pf2, pb2, 2)
            if math.isfinite(fx.endPf3): pb3 = _bw_for_sweep(f.pf3, pb3, 3)

        out = 0.0
        out += (self.r1.resonate(inp, f.pf1, pb1) - inp) * f.pa1
        out += (self.r2.resonate(inp, f.pf2, pb2) - inp) * f.pa2
        out += (self.r3.resonate(inp, f.pf3, pb3) - inp) * f.pa3
        out += (self.r4.resonate(inp, f.pf4, f.pb4) - inp) * f.pa4
        out += (self.r5.resonate(inp, f.pf5, f.pb5) - inp) * f.pa5
        out += (self.r6.resonate(inp, f.pf6, f.pb6) - inp) * f.pa6
        return out + (inp - out) * f.parallelBypass


class HighShelf:
    def __init__(self, sr: int, fc: float = 2500.0, gain_db: float = 3.0, Q: float = 0.7):
        self.in1 = self.in2 = self.out1 = self.out2 = 0.0
        fc = _clamp(fc, 20.0, 0.5 * sr * 0.95)
        Q = _clamp(Q, 0.1, 4.0)
        gain_db = _clamp(gain_db, -24.0, 24.0)
        A = 10.0 ** (gain_db / 40.0)
        w0 = PITWO * fc / sr
        cosw0, sinw0 = math.cos(w0), math.sin(w0)
        alpha = sinw0 / (2.0 * Q)
        a0 = (A + 1) - (A - 1) * cosw0 + 2 * math.sqrt(A) * alpha
        self.b0 = A * ((A + 1) + (A - 1) * cosw0 + 2 * math.sqrt(A) * alpha) / a0
        self.b1 = -2 * A * ((A - 1) + (A + 1) * cosw0) / a0
        self.b2 = A * ((A + 1) + (A - 1) * cosw0 - 2 * math.sqrt(A) * alpha) / a0
        self.a1 = 2 * ((A - 1) - (A + 1) * cosw0) / a0
        self.a2 = ((A + 1) - (A - 1) * cosw0 - 2 * math.sqrt(A) * alpha) / a0

    def apply(self, x: float) -> float:
        o = self.b0 * x + self.b1 * self.in1 + self.b2 * self.in2 - self.a1 * self.out1 - self.a2 * self.out2
        self.in2, self.in1 = self.in1, x
        self.out2, self.out1 = self.out1, o
        return o


# ─────────────────────────────────────────────────────────────────────────────
# Build frame from YAML
# ─────────────────────────────────────────────────────────────────────────────

def build_frame_from_phoneme(props: Dict[str, Any], f0: float,
                             defaults: Dict[str, float]) -> Tuple[Frame, FrameEx]:
    def getf(name: str) -> float:
        v = props.get(name, defaults.get(name, 0.0))
        try: return float(v)
        except: return defaults.get(name, 0.0)

    def getfx(name: str, default: float) -> float:
        if name in props:
            try: return float(props[name])
            except: pass
        return default

    frame = Frame(
        voicePitch=f0,
        vibratoPitchOffset=getf("vibratoPitchOffset"), vibratoSpeed=getf("vibratoSpeed"),
        voiceTurbulenceAmplitude=getf("voiceTurbulenceAmplitude"),
        glottalOpenQuotient=getf("glottalOpenQuotient"),
        voiceAmplitude=getf("voiceAmplitude"), aspirationAmplitude=getf("aspirationAmplitude"),
        fricationAmplitude=getf("fricationAmplitude"),
        cf1=getf("cf1"), cf2=getf("cf2"), cf3=getf("cf3"),
        cf4=getf("cf4"), cf5=getf("cf5"), cf6=getf("cf6"),
        cfN0=getf("cfN0"), cfNP=getf("cfNP"),
        cb1=getf("cb1"), cb2=getf("cb2"), cb3=getf("cb3"),
        cb4=getf("cb4"), cb5=getf("cb5"), cb6=getf("cb6"),
        cbN0=getf("cbN0"), cbNP=getf("cbNP"), caNP=getf("caNP"),
        pf1=getf("pf1"), pf2=getf("pf2"), pf3=getf("pf3"),
        pf4=getf("pf4"), pf5=getf("pf5"), pf6=getf("pf6"),
        pb1=getf("pb1"), pb2=getf("pb2"), pb3=getf("pb3"),
        pb4=getf("pb4"), pb5=getf("pb5"), pb6=getf("pb6"),
        pa1=getf("pa1"), pa2=getf("pa2"), pa3=getf("pa3"),
        pa4=getf("pa4"), pa5=getf("pa5"), pa6=getf("pa6"),
        parallelBypass=getf("parallelBypass"),
        preFormantGain=getf("preFormantGain"), outputGain=getf("outputGain"),
    )

    fx = FrameEx(
        creakiness=getfx("creakiness", 0.0), breathiness=getfx("breathiness", 0.0),
        jitter=getfx("jitter", 0.0), shimmer=getfx("shimmer", 0.0),
        sharpness=getfx("sharpness", 0.0),
        endCf1=getfx("endCf1", float('nan')), endCf2=getfx("endCf2", float('nan')),
        endCf3=getfx("endCf3", float('nan')), endPf1=getfx("endPf1", float('nan')),
        endPf2=getfx("endPf2", float('nan')), endPf3=getfx("endPf3", float('nan')),
    )
    return frame, fx


# ─────────────────────────────────────────────────────────────────────────────
# Formant trajectory smoother (matches frame.cpp exponential smoothing)
# ─────────────────────────────────────────────────────────────────────────────

K_FORMANT_ALPHA = 0.004  # matches C++ kFormantAlpha

def _apply_formant_trajectories(f: Frame, fx: FrameEx, n: int) -> list:
    """Pre-compute per-sample frames with exponential formant smoothing."""
    has = any(math.isfinite(getattr(fx, a)) for a in
              ('endCf1', 'endCf2', 'endCf3', 'endPf1', 'endPf2', 'endPf3'))
    if not has:
        return None

    alpha = K_FORMANT_ALPHA
    cur = copy.copy(f)
    frames = []
    for _ in range(n):
        if math.isfinite(fx.endCf1): cur.cf1 += alpha * (fx.endCf1 - cur.cf1)
        if math.isfinite(fx.endCf2): cur.cf2 += alpha * (fx.endCf2 - cur.cf2)
        if math.isfinite(fx.endCf3): cur.cf3 += alpha * (fx.endCf3 - cur.cf3)
        if math.isfinite(fx.endPf1): cur.pf1 += alpha * (fx.endPf1 - cur.pf1)
        if math.isfinite(fx.endPf2): cur.pf2 += alpha * (fx.endPf2 - cur.pf2)
        if math.isfinite(fx.endPf3): cur.pf3 += alpha * (fx.endPf3 - cur.pf3)
        frames.append(copy.copy(cur))
    return frames


# ─────────────────────────────────────────────────────────────────────────────
# Synthesis
# ─────────────────────────────────────────────────────────────────────────────

def synthesize(f: Frame, fx: FrameEx, duration_s: float, sample_rate: int,
               model: str, rosenberg_oq: float, rosenberg_sq: float) -> np.ndarray:
    n = int(duration_s * sample_rate)
    per_sample = _apply_formant_trajectories(f, fx, n)

    voice = (RosenbergVoiceSource(sample_rate, oq=rosenberg_oq, sq=rosenberg_sq, seed=0)
             if model == "rosenberg"
             else EngineVoiceSource(sample_rate, seed=0))

    fric = NoiseGenerator(seed=1)
    cascade = CascadeFormantGenerator(sample_rate)
    parallel = ParallelFormantGenerator(sample_rate)
    hs = HighShelf(sample_rate)

    last_in = last_out = smooth_pre = 0.0
    att_a = 1.0 - math.exp(-1.0 / (sample_rate * 0.001))
    rel_a = 1.0 - math.exp(-1.0 / (sample_rate * 0.0005))

    out = np.zeros(n, dtype=np.float32)
    for i in range(n):
        cf = per_sample[i] if per_sample else f
        a = att_a if cf.preFormantGain > smooth_pre else rel_a
        smooth_pre += (cf.preFormantGain - smooth_pre) * a

        v = voice.get_next(cf, fx)
        casc = cascade.get_next(cf, fx, getattr(voice, "glottis_open", False), v * smooth_pre)
        fr = fric.get_next() * 0.175 * cf.fricationAmplitude
        fr = voice.apply_frication_tilt(fr)
        par = parallel.get_next(cf, fx, getattr(voice, "glottis_open", False), fr * smooth_pre)
        mixed = (casc + par) * cf.outputGain
        filtered = mixed - last_in + 0.9995 * last_out
        last_in, last_out = mixed, filtered
        out[i] = hs.apply(filtered)
    return out


# ─────────────────────────────────────────────────────────────────────────────
# Analysis + WAV output
# ─────────────────────────────────────────────────────────────────────────────

def spectral_metrics(wav: np.ndarray, sample_rate: int) -> Dict[str, float]:
    w = wav[len(wav) // 2:].astype(np.float64)
    if len(w) < 32:
        return {"centroid_hz": 0.0}
    win = np.hanning(len(w))
    mag = np.abs(np.fft.rfft(w * win))
    freqs = np.fft.rfftfreq(len(w), 1.0 / sample_rate)
    p = mag ** 2
    total = float(np.sum(p)) or 1e-12
    def band(lo, hi):
        m = (freqs >= lo) & (freqs < hi)
        return float(np.sum(p[m]) / total)
    return {
        "centroid_hz": float(np.sum(freqs * p) / total),
        "band_0_1k": band(0, 1000), "band_1k_3k": band(1000, 3000),
        "band_3k_8k": band(3000, 8000), "peak_abs": float(np.max(np.abs(wav))),
    }


def write_wav(path: str, samples: np.ndarray, sample_rate: int) -> None:
    scaled = np.clip(samples.astype(np.float64) * 5000.0, -32767, 32767).astype(np.int16)
    p = Path(path)
    p.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(p), "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sample_rate)
        wf.writeframes(scaled.tobytes())


# ─────────────────────────────────────────────────────────────────────────────
# CLI
# ─────────────────────────────────────────────────────────────────────────────

def main() -> None:
    ap = argparse.ArgumentParser(description="Klatt formant synthesizer with FrameEx (DSP v6)")
    ap.add_argument("--phonemes", required=True, help="Path to packs/phonemes.yaml")
    ap.add_argument("--phoneme", required=True, help="Phoneme key (e.g. a, ʃ, t͡s)")
    ap.add_argument("--out", required=False, help="Output wav path")
    ap.add_argument("--model", choices=["engine", "rosenberg"], default="engine")
    ap.add_argument("--sr", type=int, default=16000)
    ap.add_argument("--f0", type=float, default=120.0)
    ap.add_argument("--dur", type=float, default=0.25)
    ap.add_argument("--oq", type=float, default=0.62, help="Rosenberg OQ")
    ap.add_argument("--sq", type=float, default=1.2, help="Rosenberg SQ")

    g = ap.add_argument_group("FrameEx voice quality (DSP v5+)")
    g.add_argument("--creakiness", type=float, default=None, help="Laryngealization 0-1")
    g.add_argument("--breathiness", type=float, default=None, help="Breathy voice 0-1")
    g.add_argument("--jitter", type=float, default=None, help="F0 perturbation 0-1")
    g.add_argument("--shimmer", type=float, default=None, help="Amp perturbation 0-1")
    g.add_argument("--sharpness", type=float, default=None, help="Closure sharpness multiplier")

    g2 = ap.add_argument_group("Formant end targets")
    for p in ('cf1', 'cf2', 'cf3', 'pf1', 'pf2', 'pf3'):
        g2.add_argument(f"--end-{p}", type=float, default=None, help=f"End target for {p} (Hz)")

    args = ap.parse_args()

    phonemes = _parse_simple_yaml_map_of_maps(args.phonemes)
    if args.phoneme not in phonemes:
        raise SystemExit(f"Unknown phoneme '{args.phoneme}'. Available count={len(phonemes)}")

    defaults = {"vibratoPitchOffset": 0.0, "vibratoSpeed": 0.0, "voiceTurbulenceAmplitude": 0.0,
                "glottalOpenQuotient": 0.0, "preFormantGain": 2.0, "outputGain": 1.5}

    frame, fx = build_frame_from_phoneme(phonemes[args.phoneme], f0=args.f0, defaults=defaults)

    # CLI overrides
    for attr in ('creakiness', 'breathiness', 'jitter', 'shimmer', 'sharpness'):
        v = getattr(args, attr)
        if v is not None: setattr(fx, attr, v)
    for p in ('cf1', 'cf2', 'cf3', 'pf1', 'pf2', 'pf3'):
        v = getattr(args, f"end_{p}")
        if v is not None: setattr(fx, f"end{p[0].upper()}{p[1:]}", v)

    wav = synthesize(frame, fx, args.dur, args.sr, args.model, args.oq, args.sq)
    m = spectral_metrics(wav, args.sr)

    print(f"phoneme={args.phoneme}  model={args.model}  sr={args.sr}  f0={args.f0}  dur={args.dur}")
    fx_parts = []
    for a in ('creakiness', 'breathiness', 'jitter', 'shimmer', 'sharpness'):
        v = getattr(fx, a)
        if v > 0: fx_parts.append(f"{a}={v:.2f}")
    for a in ('endCf1', 'endCf2', 'endCf3', 'endPf1', 'endPf2', 'endPf3'):
        v = getattr(fx, a)
        if math.isfinite(v): fx_parts.append(f"{a}={v:.0f}")
    print(f"frameEx: {', '.join(fx_parts) if fx_parts else '(none active)'}")
    print("metrics:")
    for k, v in m.items(): print(f"  {k}: {v:.6g}")
    if args.out:
        write_wav(args.out, wav, args.sr)
        print(f"wrote: {args.out}")


if __name__ == "__main__":
    main()
#!/usr/bin/env python3
"""
frame_inspector.py

Detailed frame-level analysis tool for TGSpeechBox tuning.

This tool lets you:
1. Inspect how frames interpolate sample-by-sample
2. Compare two phoneme transitions side-by-side
3. Dump frame parameters at specific time points
4. Analyze formant transition rates (Hz/ms)
5. View language pack settings and their effects

Useful for debugging coarticulation and understanding why certain
transitions sound "choppy" or "smooth".

Usage:
  python frame_inspector.py --packs /path/to/packs --lang en-us dump a
  python frame_inspector.py --packs /path/to/packs --lang hu compare a i --fade 15
  python frame_inspector.py --packs /path/to/packs --lang en-us settings
"""

from __future__ import annotations

import argparse
import math
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Optional

import numpy as np

# Import from formant_trajectory (which now uses lang_pack)
from formant_trajectory import (
    Frame, FrameRequest, FrameManager, TrajectoryRecorder, TrajectoryPoint,
    FRAME_PARAM_NAMES, lerp,
    build_frame_from_phoneme, get_phoneme_duration_ms, get_fade_ms, get_stop_closure_gap,
)

# Import from lang_pack
from lang_pack import (
    load_pack_set, PackSet, LanguagePack, PhonemeDef,
    FIELD_ID, format_pack_summary,
)

try:
    import matplotlib.pyplot as plt
    HAS_MATPLOTLIB = True
except ImportError:
    HAS_MATPLOTLIB = False


# =============================================================================
# Detailed Frame Dump
# =============================================================================

def dump_frame(f: Frame, label: str = "") -> str:
    """Format frame parameters for display."""
    lines = [f"=== Frame: {label} ===" if label else "=== Frame ==="]
    
    # Voice
    lines.append(f"  Pitch:     {f.voicePitch:.1f} Hz → {f.endVoicePitch:.1f} Hz")
    lines.append(f"  VoiceAmp:  {f.voiceAmplitude:.3f}")
    lines.append(f"  AspAmp:    {f.aspirationAmplitude:.3f}")
    lines.append(f"  FricAmp:   {f.fricationAmplitude:.3f}")
    lines.append(f"  GlotOQ:    {f.glottalOpenQuotient:.3f}")
    
    # Cascade formants
    lines.append(f"  F1: {f.cf1:6.0f} Hz  BW: {f.cb1:5.0f}")
    lines.append(f"  F2: {f.cf2:6.0f} Hz  BW: {f.cb2:5.0f}")
    lines.append(f"  F3: {f.cf3:6.0f} Hz  BW: {f.cb3:5.0f}")
    lines.append(f"  F4: {f.cf4:6.0f} Hz  BW: {f.cb4:5.0f}")
    lines.append(f"  F5: {f.cf5:6.0f} Hz  BW: {f.cb5:5.0f}")
    lines.append(f"  F6: {f.cf6:6.0f} Hz  BW: {f.cb6:5.0f}")
    
    # Nasal
    lines.append(f"  N0: {f.cfN0:6.0f} Hz  BW: {f.cbN0:5.0f}")
    lines.append(f"  NP: {f.cfNP:6.0f} Hz  BW: {f.cbNP:5.0f}  Amp: {f.caNP:.3f}")
    
    # Gains
    lines.append(f"  PreGain:   {f.preFormantGain:.3f}")
    lines.append(f"  OutGain:   {f.outputGain:.3f}")
    
    return "\n".join(lines)


def dump_phoneme_def(pdef: PhonemeDef) -> str:
    """Format phoneme definition for display."""
    lines = [f"=== Phoneme: {pdef.key} ==="]
    
    # Flags
    flags = []
    if pdef.is_vowel: flags.append("vowel")
    if pdef.is_voiced: flags.append("voiced")
    if pdef.is_stop: flags.append("stop")
    if pdef.is_affricate: flags.append("affricate")
    if pdef.is_nasal: flags.append("nasal")
    if pdef.is_liquid: flags.append("liquid")
    if pdef.is_semivowel: flags.append("semivowel")
    if pdef.is_tap: flags.append("tap")
    if pdef.is_trill: flags.append("trill")
    if pdef.copy_adjacent: flags.append("copyAdjacent")
    lines.append(f"  Flags: {', '.join(flags) if flags else '(none)'}")
    
    # Set fields
    lines.append("  Set fields:")
    for name in FRAME_PARAM_NAMES:
        if pdef.has_field(name):
            lines.append(f"    {name}: {pdef.get_field(name)}")
    
    return "\n".join(lines)


def compare_frames(f1: Frame, f2: Frame, label1: str = "A", label2: str = "B") -> str:
    """Show side-by-side comparison of two frames."""
    lines = [f"{'Parameter':<20} {label1:>12} {label2:>12} {'Δ':>12}"]
    lines.append("-" * 60)
    
    params_to_show = [
        ("voicePitch", "Hz"),
        ("voiceAmplitude", ""),
        ("aspirationAmplitude", ""),
        ("fricationAmplitude", ""),
        ("cf1", "Hz"),
        ("cf2", "Hz"),
        ("cf3", "Hz"),
        ("cb1", "Hz"),
        ("cb2", "Hz"),
        ("cb3", "Hz"),
        ("caNP", ""),
        ("preFormantGain", ""),
    ]
    
    for param, unit in params_to_show:
        v1 = getattr(f1, param)
        v2 = getattr(f2, param)
        delta = v2 - v1
        
        if unit == "Hz":
            lines.append(f"{param:<20} {v1:>10.1f} {unit} {v2:>10.1f} {unit} {delta:>+10.1f}")
        else:
            lines.append(f"{param:<20} {v1:>12.4f} {v2:>12.4f} {delta:>+12.4f}")
    
    return "\n".join(lines)


# =============================================================================
# Transition Analysis
# =============================================================================

@dataclass
class TransitionMetrics:
    """Metrics for a single phoneme-to-phoneme transition."""
    phoneme_a: str
    phoneme_b: str
    fade_ms: float
    
    # Formant deltas
    f1_delta: float  # Hz
    f2_delta: float
    f3_delta: float
    
    # Transition rates (Hz/ms)
    f1_rate: float
    f2_rate: float
    f3_rate: float
    
    # Voice amplitude change
    voice_amp_delta: float
    
    # Frication amplitude change
    fric_amp_delta: float


def analyze_transition(
    pdef_a: PhonemeDef,
    pdef_b: PhonemeDef,
    pack: PackSet,
    fade_ms: float = 10.0,
    f0: float = 140.0,
) -> TransitionMetrics:
    """Analyze the transition between two phonemes using pack settings."""
    frame_a = build_frame_from_phoneme(pdef_a, pack, f0=f0)
    frame_b = build_frame_from_phoneme(pdef_b, pack, f0=f0)
    
    f1_delta = frame_b.cf1 - frame_a.cf1
    f2_delta = frame_b.cf2 - frame_a.cf2
    f3_delta = frame_b.cf3 - frame_a.cf3
    
    # Rates (Hz per ms)
    rate = 1.0 / fade_ms if fade_ms > 0 else 0.0
    
    return TransitionMetrics(
        phoneme_a=pdef_a.key,
        phoneme_b=pdef_b.key,
        fade_ms=fade_ms,
        f1_delta=f1_delta,
        f2_delta=f2_delta,
        f3_delta=f3_delta,
        f1_rate=f1_delta * rate,
        f2_rate=f2_delta * rate,
        f3_rate=f3_delta * rate,
        voice_amp_delta=frame_b.voiceAmplitude - frame_a.voiceAmplitude,
        fric_amp_delta=frame_b.fricationAmplitude - frame_a.fricationAmplitude,
    )


def format_transition_metrics(m: TransitionMetrics, pack: PackSet) -> str:
    """Format transition metrics for display."""
    lp = pack.lang
    
    lines = [
        f"Transition: {m.phoneme_a} → {m.phoneme_b}",
        f"Fade time: {m.fade_ms:.1f} ms",
        "",
        f"Formant changes:",
        f"  F1: {m.f1_delta:+.0f} Hz  ({m.f1_rate:+.1f} Hz/ms)",
        f"  F2: {m.f2_delta:+.0f} Hz  ({m.f2_rate:+.1f} Hz/ms)",
        f"  F3: {m.f3_delta:+.0f} Hz  ({m.f3_rate:+.1f} Hz/ms)",
        "",
        f"Amplitude changes:",
        f"  Voice: {m.voice_amp_delta:+.3f}",
        f"  Fric:  {m.fric_amp_delta:+.3f}",
    ]
    
    # Flag potentially problematic transitions based on pack settings
    warnings = []
    
    # Check against trajectory limit settings
    if lp.trajectory_limit_enabled:
        cf2_limit = lp.trajectory_limit_max_hz_per_ms[FIELD_ID["cf2"]]
        cf3_limit = lp.trajectory_limit_max_hz_per_ms[FIELD_ID["cf3"]]
        if cf2_limit > 0 and abs(m.f2_rate) > cf2_limit:
            warnings.append(f"  ⚠ F2 rate {m.f2_rate:.0f} Hz/ms exceeds limit {cf2_limit:.0f}")
        if cf3_limit > 0 and abs(m.f3_rate) > cf3_limit:
            warnings.append(f"  ⚠ F3 rate {m.f3_rate:.0f} Hz/ms exceeds limit {cf3_limit:.0f}")
    else:
        if abs(m.f2_rate) > 100:
            warnings.append(f"  ⚠ F2 rate {m.f2_rate:.0f} Hz/ms may sound abrupt")
    
    if abs(m.voice_amp_delta) > 0.5:
        warnings.append(f"  ⚠ Large voice amp change may cause click")
    
    if warnings:
        lines.append("")
        lines.append("Warnings:")
        lines.extend(warnings)
    
    return "\n".join(lines)


# =============================================================================
# Sample-Level Inspection
# =============================================================================

def trace_interpolation(
    frame_a: Frame,
    frame_b: Frame,
    duration_ms: float,
    fade_ms: float,
    sample_rate: int = 16000,
    output_interval_ms: float = 1.0,
) -> list[dict]:
    """
    Trace the exact interpolation between two frames as the frame manager
    would compute it.
    
    Returns a list of dicts with time and interpolated values.
    """
    fm = FrameManager()
    
    # Queue first frame
    min_samples_a = int(duration_ms * sample_rate / 1000.0)
    fade_samples_a = int(fade_ms * sample_rate / 1000.0)
    fm.queue_frame(frame_a, min_samples_a, fade_samples_a, label="A")
    
    # Queue second frame
    min_samples_b = int(duration_ms * sample_rate / 1000.0)
    fade_samples_b = int(fade_ms * sample_rate / 1000.0)
    fm.queue_frame(frame_b, min_samples_b, fade_samples_b, label="B")
    
    output_interval_samples = int(output_interval_ms * sample_rate / 1000.0)
    if output_interval_samples < 1:
        output_interval_samples = 1
    
    results = []
    sample_idx = 0
    total_samples = min_samples_a + min_samples_b + fade_samples_b + 100
    
    while sample_idx < total_samples:
        f = fm.get_current_frame()
        
        if sample_idx % output_interval_samples == 0:
            if f is None:
                results.append({
                    "time_ms": sample_idx * 1000.0 / sample_rate,
                    "f1": 0, "f2": 0, "f3": 0,
                    "voice_amp": 0, "fric_amp": 0,
                    "pitch": 0,
                    "is_silence": True,
                })
            else:
                results.append({
                    "time_ms": sample_idx * 1000.0 / sample_rate,
                    "f1": f.cf1, "f2": f.cf2, "f3": f.cf3,
                    "voice_amp": f.voiceAmplitude, "fric_amp": f.fricationAmplitude,
                    "pitch": f.voicePitch,
                    "is_silence": False,
                })
        
        sample_idx += 1
        
        # Stop after enough silence
        if f is None and sample_idx > min_samples_a + min_samples_b + fade_samples_b:
            break
    
    return results


def plot_interpolation_trace(trace: list[dict], title: str = "Interpolation Trace") -> Optional[Any]:
    """Plot the interpolation trace."""
    if not HAS_MATPLOTLIB:
        print("matplotlib not available for plotting")
        return None
    
    times = [r["time_ms"] for r in trace]
    f1 = [r["f1"] for r in trace]
    f2 = [r["f2"] for r in trace]
    f3 = [r["f3"] for r in trace]
    
    fig, axes = plt.subplots(2, 1, figsize=(12, 6), sharex=True)
    
    ax = axes[0]
    ax.plot(times, f1, label="F1", linewidth=2)
    ax.plot(times, f2, label="F2", linewidth=2)
    ax.plot(times, f3, label="F3", linewidth=2)
    ax.set_ylabel("Frequency (Hz)")
    ax.set_title(title)
    ax.legend()
    ax.grid(True, alpha=0.3)
    
    ax = axes[1]
    voice_amp = [r["voice_amp"] for r in trace]
    ax.plot(times, voice_amp, label="Voice Amp", linewidth=2)
    ax.set_ylabel("Amplitude")
    ax.set_xlabel("Time (ms)")
    ax.legend()
    ax.grid(True, alpha=0.3)
    
    plt.tight_layout()
    return fig


# =============================================================================
# Phoneme Pair Grid Analysis
# =============================================================================

def analyze_phoneme_pair_grid(
    pack: PackSet,
    phoneme_keys: list[str],
    fade_ms: float = 10.0,
) -> dict[tuple[str, str], TransitionMetrics]:
    """Analyze all pairwise transitions between given phonemes."""
    grid = {}
    
    for a in phoneme_keys:
        pdef_a = pack.get_phoneme(a)
        if pdef_a is None:
            continue
        for b in phoneme_keys:
            if a == b:
                continue
            pdef_b = pack.get_phoneme(b)
            if pdef_b is None:
                continue
            
            metrics = analyze_transition(pdef_a, pdef_b, pack, fade_ms=fade_ms)
            grid[(a, b)] = metrics
    
    return grid


def format_pair_grid_summary(
    grid: dict[tuple[str, str], TransitionMetrics],
    metric: str = "f2_rate",
) -> str:
    """Format a summary of the pair grid, sorted by the given metric."""
    items = list(grid.items())
    items.sort(key=lambda x: abs(getattr(x[1], metric)), reverse=True)
    
    lines = [f"Top transitions by |{metric}|:"]
    lines.append("-" * 50)
    
    for (a, b), m in items[:20]:
        val = getattr(m, metric)
        lines.append(f"  {a:>4} → {b:<4}  {val:+8.1f}")
    
    return "\n".join(lines)


# =============================================================================
# Coarticulation Locus Analysis
# =============================================================================

def estimate_consonant_locus(
    pack: PackSet,
    consonant: str,
    vowels: list[str],
    formant: str = "cf2",
) -> dict[str, Any]:
    """
    Estimate the locus of a consonant by looking at its transitions
    with multiple vowels.
    """
    pdef_c = pack.get_phoneme(consonant)
    if pdef_c is None:
        return {"error": f"Consonant {consonant} not in phoneme map"}
    
    c_formant = pdef_c.get_field(formant)
    
    # Get pack locus values for comparison
    lp = pack.lang
    pack_loci = {
        "labial": lp.coarticulation_labial_f2_locus,
        "alveolar": lp.coarticulation_alveolar_f2_locus,
        "velar": lp.coarticulation_velar_f2_locus,
    }
    
    transitions = []
    for v in vowels:
        pdef_v = pack.get_phoneme(v)
        if pdef_v is None:
            continue
        v_formant = pdef_v.get_field(formant)
        
        if v_formant > 0:
            transitions.append({
                "vowel": v,
                "vowel_formant": v_formant,
                "delta": c_formant - v_formant,
            })
    
    if not transitions:
        return {"error": "No valid vowel transitions found"}
    
    avg_delta = np.mean([t["delta"] for t in transitions])
    
    # Estimate place based on F2
    if c_formant < 1200:
        estimated_place = "labial"
    elif c_formant < 2000:
        estimated_place = "alveolar"
    else:
        estimated_place = "velar"
    
    return {
        "consonant": consonant,
        "formant": formant,
        "consonant_value": c_formant,
        "avg_delta": avg_delta,
        "estimated_locus": c_formant,
        "estimated_place": estimated_place,
        "pack_locus_for_place": pack_loci.get(estimated_place, 0),
        "transitions": transitions,
    }


# =============================================================================
# Settings Display
# =============================================================================

def format_detailed_settings(pack: PackSet) -> str:
    """Format detailed language pack settings."""
    lp = pack.lang
    lines = [
        f"=== Language Pack Settings: {lp.lang_tag} ===",
        "",
        "=== Timing ===",
        f"  primary_stress_div: {lp.primary_stress_div}",
        f"  secondary_stress_div: {lp.secondary_stress_div}",
        f"  lengthened_scale: {lp.lengthened_scale}",
        f"  lengthened_scale_hu: {lp.lengthened_scale_hu}",
        f"  apply_lengthened_scale_to_vowels_only: {lp.apply_lengthened_scale_to_vowels_only}",
        "",
        "=== Stop Closure ===",
        f"  mode: {lp.stop_closure_mode}",
        f"  cluster_gaps_enabled: {lp.stop_closure_cluster_gaps_enabled}",
        f"  after_nasals_enabled: {lp.stop_closure_after_nasals_enabled}",
        f"  vowel_gap_ms: {lp.stop_closure_vowel_gap_ms}",
        f"  vowel_fade_ms: {lp.stop_closure_vowel_fade_ms}",
        f"  cluster_gap_ms: {lp.stop_closure_cluster_gap_ms}",
        f"  cluster_fade_ms: {lp.stop_closure_cluster_fade_ms}",
        "",
        "=== Coarticulation ===",
        f"  enabled: {lp.coarticulation_enabled}",
        f"  strength: {lp.coarticulation_strength}",
        f"  transition_extent: {lp.coarticulation_transition_extent}",
        f"  graduated: {lp.coarticulation_graduated}",
        f"  labial_f2_locus: {lp.coarticulation_labial_f2_locus} Hz",
        f"  alveolar_f2_locus: {lp.coarticulation_alveolar_f2_locus} Hz",
        f"  velar_f2_locus: {lp.coarticulation_velar_f2_locus} Hz",
        f"  velar_pinch_enabled: {lp.coarticulation_velar_pinch_enabled}",
        f"  velar_pinch_threshold: {lp.coarticulation_velar_pinch_threshold} Hz",
        f"  velar_pinch_f3: {lp.coarticulation_velar_pinch_f3} Hz",
        "",
        "=== Boundary Smoothing ===",
        f"  enabled: {lp.boundary_smoothing_enabled}",
        f"  vowel_to_stop_fade_ms: {lp.boundary_smoothing_vowel_to_stop_fade_ms}",
        f"  stop_to_vowel_fade_ms: {lp.boundary_smoothing_stop_to_vowel_fade_ms}",
        f"  vowel_to_fric_fade_ms: {lp.boundary_smoothing_vowel_to_fric_fade_ms}",
        "",
        "=== Trajectory Limiting ===",
        f"  enabled: {lp.trajectory_limit_enabled}",
        f"  window_ms: {lp.trajectory_limit_window_ms}",
        f"  cf2_max_hz_per_ms: {lp.trajectory_limit_max_hz_per_ms[FIELD_ID['cf2']]}",
        f"  cf3_max_hz_per_ms: {lp.trajectory_limit_max_hz_per_ms[FIELD_ID['cf3']]}",
        "",
        "=== Liquid Dynamics ===",
        f"  enabled: {lp.liquid_dynamics_enabled}",
        f"  lateral_onglide_f1_delta: {lp.liquid_dynamics_lateral_onglide_f1_delta}",
        f"  lateral_onglide_f2_delta: {lp.liquid_dynamics_lateral_onglide_f2_delta}",
        f"  rhotic_f3_dip_enabled: {lp.liquid_dynamics_rhotic_f3_dip_enabled}",
        "",
        "=== Phrase-Final Lengthening ===",
        f"  enabled: {lp.phrase_final_lengthening_enabled}",
        f"  final_syllable_scale: {lp.phrase_final_lengthening_final_syllable_scale}",
        f"  penultimate_syllable_scale: {lp.phrase_final_lengthening_penultimate_syllable_scale}",
        "",
        "=== Microprosody ===",
        f"  enabled: {lp.microprosody_enabled}",
        f"  voiceless_f0_raise_hz: {lp.microprosody_voiceless_f0_raise_hz}",
        f"  voiced_f0_lower_hz: {lp.microprosody_voiced_f0_lower_hz}",
        "",
        "=== Positional Allophones ===",
        f"  enabled: {lp.positional_allophones_enabled}",
        f"  lateral_darkness_pre_vocalic: {lp.positional_allophones_lateral_darkness_pre_vocalic}",
        f"  lateral_darkness_post_vocalic: {lp.positional_allophones_lateral_darkness_post_vocalic}",
        "",
        "=== Output Defaults ===",
        f"  pre_formant_gain: {lp.default_pre_formant_gain}",
        f"  output_gain: {lp.default_output_gain}",
        "",
        "=== Intonation Contours ===",
    ]
    
    for clause_type in [".", ",", "?", "!"]:
        if clause_type in lp.intonation:
            ic = lp.intonation[clause_type]
            lines.append(f"  '{clause_type}': nucleus {ic.nucleus_start}→{ic.nucleus_end}, tail {ic.tail_start}→{ic.tail_end}")
    
    return "\n".join(lines)


# =============================================================================
# CLI
# =============================================================================

def main():
    ap = argparse.ArgumentParser(description="Frame-level inspector for TGSpeechBox")
    ap.add_argument("--packs", required=True, help="Path to packs folder")
    ap.add_argument("--lang", default="default", help="Language tag (e.g., en-us, hu)")
    
    subparsers = ap.add_subparsers(dest="command", help="Command")
    
    # Dump single phoneme
    dump_parser = subparsers.add_parser("dump", help="Dump phoneme frame parameters")
    dump_parser.add_argument("phoneme")
    dump_parser.add_argument("--raw", action="store_true", help="Show raw phoneme definition")
    
    # Compare two phonemes
    compare_parser = subparsers.add_parser("compare", help="Compare two phonemes")
    compare_parser.add_argument("phoneme_a")
    compare_parser.add_argument("phoneme_b")
    compare_parser.add_argument("--fade", type=float, help="Fade time in ms (default: from pack)")
    
    # Trace interpolation
    trace_parser = subparsers.add_parser("trace", help="Trace interpolation between two phonemes")
    trace_parser.add_argument("phoneme_a")
    trace_parser.add_argument("phoneme_b")
    trace_parser.add_argument("--duration", type=float, default=100.0, help="Phoneme duration in ms")
    trace_parser.add_argument("--fade", type=float, help="Fade time in ms (default: from pack)")
    trace_parser.add_argument("--out", help="Output PNG path")
    trace_parser.add_argument("--show", action="store_true")
    
    # Pair grid
    grid_parser = subparsers.add_parser("grid", help="Analyze all pairwise transitions")
    grid_parser.add_argument("--phonemes", nargs="+", default=["a", "e", "i", "o", "u", "p", "t", "k", "s", "ʃ"])
    grid_parser.add_argument("--fade", type=float, default=10.0)
    grid_parser.add_argument("--metric", default="f2_rate")
    
    # Locus analysis
    locus_parser = subparsers.add_parser("locus", help="Estimate consonant locus")
    locus_parser.add_argument("consonant")
    locus_parser.add_argument("--vowels", nargs="+", default=["a", "e", "i", "o", "u", "æ", "ɪ", "ʊ", "ɛ", "ɔ"])
    locus_parser.add_argument("--formant", default="cf2")
    
    # Settings dump
    subparsers.add_parser("settings", help="Dump all language pack settings")
    
    # List phonemes
    list_parser = subparsers.add_parser("list", help="List available phonemes")
    list_parser.add_argument("--type", choices=["all", "vowels", "stops", "nasals", "fricatives"], default="all")
    
    args = ap.parse_args()
    
    if not args.command:
        ap.print_help()
        return 1
    
    # Load pack
    try:
        pack = load_pack_set(args.packs, args.lang)
    except FileNotFoundError as e:
        print(f"ERROR: {e}")
        return 1
    
    print(f"Loaded: {pack.lang.lang_tag} ({len(pack.phonemes)} phonemes)\n")
    
    if args.command == "settings":
        print(format_detailed_settings(pack))
        return 0
    
    if args.command == "list":
        phonemes = list(pack.phonemes.values())
        if args.type == "vowels":
            phonemes = [p for p in phonemes if p.is_vowel]
        elif args.type == "stops":
            phonemes = [p for p in phonemes if p.is_stop]
        elif args.type == "nasals":
            phonemes = [p for p in phonemes if p.is_nasal]
        elif args.type == "fricatives":
            phonemes = [p for p in phonemes if p.get_field("fricationAmplitude") > 0.3]
        
        print(f"Phonemes ({args.type}):")
        for p in sorted(phonemes, key=lambda x: x.key):
            flags = []
            if p.is_vowel: flags.append("V")
            if p.is_stop: flags.append("S")
            if p.is_nasal: flags.append("N")
            if p.is_liquid: flags.append("L")
            if p.is_voiced: flags.append("+")
            flag_str = "".join(flags) if flags else "-"
            print(f"  {p.key:6s} [{flag_str}]")
        return 0
    
    if args.command == "dump":
        pdef = pack.get_phoneme(args.phoneme)
        if pdef is None:
            print(f"Unknown phoneme: {args.phoneme}")
            available = sorted(pack.phonemes.keys())[:20]
            print(f"Available: {', '.join(available)}...")
            return 1
        
        if args.raw:
            print(dump_phoneme_def(pdef))
        else:
            frame = build_frame_from_phoneme(pdef, pack)
            print(dump_frame(frame, label=args.phoneme))
            print()
            
            # Show timing info from pack
            dur = get_phoneme_duration_ms(pdef, pack, speed=1.0)
            print(f"Duration (speed=1.0): {dur:.1f} ms")
    
    elif args.command == "compare":
        pdef_a = pack.get_phoneme(args.phoneme_a)
        pdef_b = pack.get_phoneme(args.phoneme_b)
        
        if pdef_a is None:
            print(f"Unknown phoneme: {args.phoneme_a}")
            return 1
        if pdef_b is None:
            print(f"Unknown phoneme: {args.phoneme_b}")
            return 1
        
        frame_a = build_frame_from_phoneme(pdef_a, pack)
        frame_b = build_frame_from_phoneme(pdef_b, pack)
        
        print(compare_frames(frame_a, frame_b, args.phoneme_a, args.phoneme_b))
        print()
        
        # Get fade from pack or argument
        if args.fade is not None:
            fade = args.fade
        else:
            fade = get_fade_ms(pdef_b, pack, speed=1.0, prev_pdef=pdef_a)
        
        metrics = analyze_transition(pdef_a, pdef_b, pack, fade_ms=fade)
        print(format_transition_metrics(metrics, pack))
        
        # Check for stop closure gap
        gap_ms, gap_fade = get_stop_closure_gap(pdef_b, pack, speed=1.0, prev_pdef=pdef_a)
        if gap_ms > 0:
            print(f"\n⚠ Stop closure gap would be inserted: {gap_ms:.1f}ms (fade {gap_fade:.1f}ms)")
    
    elif args.command == "trace":
        pdef_a = pack.get_phoneme(args.phoneme_a)
        pdef_b = pack.get_phoneme(args.phoneme_b)
        
        if pdef_a is None:
            print(f"Unknown phoneme: {args.phoneme_a}")
            return 1
        if pdef_b is None:
            print(f"Unknown phoneme: {args.phoneme_b}")
            return 1
        
        frame_a = build_frame_from_phoneme(pdef_a, pack)
        frame_b = build_frame_from_phoneme(pdef_b, pack)
        
        # Get fade from pack or argument
        if args.fade is not None:
            fade = args.fade
        else:
            fade = get_fade_ms(pdef_b, pack, speed=1.0, prev_pdef=pdef_a)
        
        trace = trace_interpolation(
            frame_a, frame_b,
            duration_ms=args.duration,
            fade_ms=fade,
        )
        
        print(f"Trace: {args.phoneme_a} → {args.phoneme_b}")
        print(f"Duration: {args.duration} ms, Fade: {fade:.1f} ms (from pack)")
        print(f"Points: {len(trace)}")
        print()
        
        print("Sample points:")
        print(f"{'Time':>8} {'F1':>8} {'F2':>8} {'F3':>8} {'VoiceAmp':>10}")
        print("-" * 50)
        step = max(1, len(trace) // 10)
        for i in range(0, len(trace), step):
            r = trace[i]
            print(f"{r['time_ms']:>8.1f} {r['f1']:>8.0f} {r['f2']:>8.0f} {r['f3']:>8.0f} {r['voice_amp']:>10.3f}")
        
        if args.out or args.show:
            fig = plot_interpolation_trace(
                trace,
                title=f"Interpolation: {args.phoneme_a} → {args.phoneme_b} (fade={fade:.1f}ms)"
            )
            if fig:
                if args.out:
                    fig.savefig(args.out, dpi=150, bbox_inches="tight")
                    print(f"\nSaved: {args.out}")
                if args.show:
                    plt.show()
    
    elif args.command == "grid":
        grid = analyze_phoneme_pair_grid(pack, args.phonemes, fade_ms=args.fade)
        print(format_pair_grid_summary(grid, metric=args.metric))
    
    elif args.command == "locus":
        result = estimate_consonant_locus(pack, args.consonant, args.vowels, formant=args.formant)
        
        if "error" in result:
            print(result["error"])
            return 1
        
        print(f"Locus analysis for /{result['consonant']}/")
        print(f"Formant: {result['formant']}")
        print(f"Consonant value: {result['consonant_value']:.0f} Hz")
        print(f"Estimated locus: {result['estimated_locus']:.0f} Hz")
        print(f"Estimated place: {result['estimated_place']}")
        print(f"Pack locus for {result['estimated_place']}: {result['pack_locus_for_place']:.0f} Hz")
        print(f"Average delta from vowels: {result['avg_delta']:+.0f} Hz")
        print()
        print("Transitions:")
        for t in result["transitions"]:
            print(f"  → /{t['vowel']}/  vowel={t['vowel_formant']:.0f} Hz  Δ={t['delta']:+.0f} Hz")
    
    return 0


if __name__ == "__main__":
    exit(main())

#!/usr/bin/env python3
"""
frame_inspector.py

Detailed frame-level analysis tool for NV Speech Player tuning.

This tool lets you:
1. Inspect how frames interpolate sample-by-sample
2. Compare two phoneme transitions side-by-side
3. Dump frame parameters at specific time points
4. Analyze formant transition rates (Hz/ms)

Useful for debugging coarticulation and understanding why certain
transitions sound "choppy" or "smooth".
"""

from __future__ import annotations

import argparse
import math
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Optional

import numpy as np

# Import the trajectory module
from formant_trajectory import (
    Frame, FrameRequest, FrameManager, TrajectoryRecorder, TrajectoryPoint,
    parse_phonemes_yaml, build_frame_from_phoneme, get_phoneme_duration_ms,
    FRAME_PARAM_NAMES, lerp,
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
    props_a: dict[str, Any],
    props_b: dict[str, Any],
    phoneme_a: str,
    phoneme_b: str,
    fade_ms: float = 10.0,
    f0: float = 140.0,
) -> TransitionMetrics:
    """Analyze the transition between two phonemes."""
    frame_a = build_frame_from_phoneme(props_a, f0=f0)
    frame_b = build_frame_from_phoneme(props_b, f0=f0)
    
    f1_delta = frame_b.cf1 - frame_a.cf1
    f2_delta = frame_b.cf2 - frame_a.cf2
    f3_delta = frame_b.cf3 - frame_a.cf3
    
    # Rates (Hz per ms)
    rate = 1.0 / fade_ms if fade_ms > 0 else 0.0
    
    return TransitionMetrics(
        phoneme_a=phoneme_a,
        phoneme_b=phoneme_b,
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


def format_transition_metrics(m: TransitionMetrics) -> str:
    """Format transition metrics for display."""
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
    
    # Flag potentially problematic transitions
    warnings = []
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
            time_ms = sample_idx * 1000.0 / sample_rate
            if f:
                results.append({
                    "time_ms": time_ms,
                    "f1": f.cf1,
                    "f2": f.cf2,
                    "f3": f.cf3,
                    "voice_amp": f.voiceAmplitude,
                    "pitch": f.voicePitch,
                    "pre_gain": f.preFormantGain,
                    "is_silence": False,
                })
            else:
                results.append({
                    "time_ms": time_ms,
                    "f1": 0,
                    "f2": 0,
                    "f3": 0,
                    "voice_amp": 0,
                    "pitch": 0,
                    "pre_gain": 0,
                    "is_silence": True,
                })
        
        sample_idx += 1
        if f is None and sample_idx > min_samples_a + fade_samples_a:
            # Hit silence after frames exhausted
            break
    
    return results


def plot_interpolation_trace(
    trace: list[dict],
    title: str = "Frame Interpolation Trace",
) -> Optional[Any]:
    """Plot the interpolation trace."""
    if not HAS_MATPLOTLIB:
        return None
    
    times = [r["time_ms"] for r in trace]
    f1 = [r["f1"] for r in trace]
    f2 = [r["f2"] for r in trace]
    f3 = [r["f3"] for r in trace]
    voice_amp = [r["voice_amp"] for r in trace]
    
    fig, axes = plt.subplots(2, 1, figsize=(12, 6), sharex=True)
    
    ax = axes[0]
    ax.plot(times, f1, label="F1", color="#e74c3c", linewidth=1.5)
    ax.plot(times, f2, label="F2", color="#3498db", linewidth=1.5)
    ax.plot(times, f3, label="F3", color="#2ecc71", linewidth=1.5)
    ax.set_ylabel("Frequency (Hz)")
    ax.set_title(title)
    ax.legend()
    ax.grid(True, alpha=0.3)
    
    ax = axes[1]
    ax.plot(times, voice_amp, label="Voice Amp", color="#9b59b6", linewidth=1.5)
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
    phoneme_map: dict[str, dict[str, Any]],
    phonemes: list[str],
    fade_ms: float = 10.0,
) -> dict[tuple[str, str], TransitionMetrics]:
    """
    Analyze all pairwise transitions between a set of phonemes.
    Returns a dict mapping (phoneme_a, phoneme_b) to TransitionMetrics.
    """
    results = {}
    
    for a in phonemes:
        if a not in phoneme_map:
            continue
        for b in phonemes:
            if b not in phoneme_map:
                continue
            if a == b:
                continue
            
            m = analyze_transition(
                phoneme_map[a], phoneme_map[b],
                a, b, fade_ms=fade_ms
            )
            results[(a, b)] = m
    
    return results


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
    phoneme_map: dict[str, dict[str, Any]],
    consonant: str,
    vowels: list[str],
    formant: str = "cf2",
) -> dict[str, Any]:
    """
    Estimate the locus of a consonant by looking at its transitions
    with multiple vowels.
    
    The locus is the frequency toward which the consonant "pulls" the
    vowel formant during coarticulation.
    """
    if consonant not in phoneme_map:
        return {"error": f"Consonant {consonant} not in phoneme map"}
    
    c_props = phoneme_map[consonant]
    c_formant = c_props.get(formant, 0)
    
    transitions = []
    for v in vowels:
        if v not in phoneme_map:
            continue
        v_props = phoneme_map[v]
        v_formant = v_props.get(formant, 0)
        
        if v_formant > 0:
            transitions.append({
                "vowel": v,
                "vowel_formant": v_formant,
                "delta": c_formant - v_formant,
            })
    
    if not transitions:
        return {"error": "No valid vowel transitions found"}
    
    # The locus is approximately where the consonant's formant is
    # We can also look at the consistency of the pull direction
    avg_delta = np.mean([t["delta"] for t in transitions])
    
    return {
        "consonant": consonant,
        "formant": formant,
        "consonant_value": c_formant,
        "avg_delta": avg_delta,
        "estimated_locus": c_formant,
        "transitions": transitions,
    }


# =============================================================================
# CLI
# =============================================================================

def main():
    ap = argparse.ArgumentParser(description="Frame-level inspector for NV Speech Player")
    ap.add_argument("--packs", required=True, help="Path to packs folder")
    
    subparsers = ap.add_subparsers(dest="command", help="Command")
    
    # Compare two phonemes
    compare_parser = subparsers.add_parser("compare", help="Compare two phonemes")
    compare_parser.add_argument("phoneme_a")
    compare_parser.add_argument("phoneme_b")
    compare_parser.add_argument("--fade", type=float, default=10.0, help="Fade time in ms")
    
    # Dump single phoneme
    dump_parser = subparsers.add_parser("dump", help="Dump phoneme frame parameters")
    dump_parser.add_argument("phoneme")
    
    # Trace interpolation
    trace_parser = subparsers.add_parser("trace", help="Trace interpolation between two phonemes")
    trace_parser.add_argument("phoneme_a")
    trace_parser.add_argument("phoneme_b")
    trace_parser.add_argument("--duration", type=float, default=100.0, help="Phoneme duration in ms")
    trace_parser.add_argument("--fade", type=float, default=10.0, help="Fade time in ms")
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
    
    args = ap.parse_args()
    
    if not args.command:
        ap.print_help()
        return 1
    
    # Load phonemes
    packs_path = Path(args.packs)
    phon_path = packs_path / "packs" / "phonemes.yaml"
    if not phon_path.exists():
        print(f"ERROR: {phon_path} not found")
        return 1
    
    phoneme_map = parse_phonemes_yaml(str(phon_path))
    print(f"Loaded {len(phoneme_map)} phonemes\n")
    
    if args.command == "dump":
        if args.phoneme not in phoneme_map:
            print(f"Unknown phoneme: {args.phoneme}")
            print(f"Available: {', '.join(sorted(phoneme_map.keys())[:20])}...")
            return 1
        
        frame = build_frame_from_phoneme(phoneme_map[args.phoneme])
        print(dump_frame(frame, label=args.phoneme))
    
    elif args.command == "compare":
        if args.phoneme_a not in phoneme_map:
            print(f"Unknown phoneme: {args.phoneme_a}")
            return 1
        if args.phoneme_b not in phoneme_map:
            print(f"Unknown phoneme: {args.phoneme_b}")
            return 1
        
        frame_a = build_frame_from_phoneme(phoneme_map[args.phoneme_a])
        frame_b = build_frame_from_phoneme(phoneme_map[args.phoneme_b])
        
        print(compare_frames(frame_a, frame_b, args.phoneme_a, args.phoneme_b))
        print()
        
        metrics = analyze_transition(
            phoneme_map[args.phoneme_a],
            phoneme_map[args.phoneme_b],
            args.phoneme_a, args.phoneme_b,
            fade_ms=args.fade
        )
        print(format_transition_metrics(metrics))
    
    elif args.command == "trace":
        if args.phoneme_a not in phoneme_map:
            print(f"Unknown phoneme: {args.phoneme_a}")
            return 1
        if args.phoneme_b not in phoneme_map:
            print(f"Unknown phoneme: {args.phoneme_b}")
            return 1
        
        frame_a = build_frame_from_phoneme(phoneme_map[args.phoneme_a])
        frame_b = build_frame_from_phoneme(phoneme_map[args.phoneme_b])
        
        trace = trace_interpolation(
            frame_a, frame_b,
            duration_ms=args.duration,
            fade_ms=args.fade,
        )
        
        # Print summary
        print(f"Trace: {args.phoneme_a} → {args.phoneme_b}")
        print(f"Duration: {args.duration} ms, Fade: {args.fade} ms")
        print(f"Points: {len(trace)}")
        print()
        
        # Show a few samples
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
                title=f"Interpolation: {args.phoneme_a} → {args.phoneme_b} (fade={args.fade}ms)"
            )
            if fig:
                if args.out:
                    fig.savefig(args.out, dpi=150, bbox_inches="tight")
                    print(f"\nSaved: {args.out}")
                if args.show:
                    plt.show()
    
    elif args.command == "grid":
        grid = analyze_phoneme_pair_grid(phoneme_map, args.phonemes, fade_ms=args.fade)
        print(format_pair_grid_summary(grid, metric=args.metric))
    
    elif args.command == "locus":
        result = estimate_consonant_locus(
            phoneme_map, args.consonant,
            args.vowels, formant=args.formant
        )
        
        if "error" in result:
            print(result["error"])
            return 1
        
        print(f"Locus analysis for /{result['consonant']}/")
        print(f"Formant: {result['formant']}")
        print(f"Consonant value: {result['consonant_value']:.0f} Hz")
        print(f"Estimated locus: {result['estimated_locus']:.0f} Hz")
        print(f"Average delta from vowels: {result['avg_delta']:+.0f} Hz")
        print()
        print("Transitions:")
        for t in result["transitions"]:
            print(f"  → /{t['vowel']}/  vowel={t['vowel_formant']:.0f} Hz  Δ={t['delta']:+.0f} Hz")
    
    return 0


if __name__ == "__main__":
    exit(main())

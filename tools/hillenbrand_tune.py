#!/usr/bin/env python3
"""
hillenbrand_tune.py — Tune TGSpeechBox vowel formants against the Hillenbrand 1995 dataset.

Parses Hillenbrand vowdata.dat (men's steady-state F1/F2/F3 averages) and compares
against current base phoneme definitions in packs/phonemes.yaml. Generates:
  1. A comparison table showing deltas
  2. Suggested new cf1/cf2/cf3 values
  3. A/B WAV pairs (current vs Hillenbrand-aligned) for ear-testing
  4. Copy-pasteable YAML patch

Usage:
  python hillenbrand_tune.py
  python hillenbrand_tune.py --synth          # also generate A/B comparison WAVs
  python hillenbrand_tune.py --correction 1.0 # synth correction factor (default 1.0)

Dependencies: numpy (for synthesis only)
"""

from __future__ import annotations

import argparse
import math
import os
import re
import sys
from pathlib import Path
from typing import Dict, List, Optional, Tuple

# ─────────────────────────────────────────────────────────────────────────────
# Hillenbrand vowel code → IPA mapping
# ─────────────────────────────────────────────────────────────────────────────

HILL_TO_IPA = {
    "iy": "i",    # heed
    "ih": "\u026a",  # ɪ  hid
    "ei": "e",    # hayed (onset of eɪ)
    "ae": "\u00e6",  # æ  had
    "eh": "\u025b",  # ɛ  head
    "ah": "\u0251",  # ɑ  hod/cot
    "aw": "\u0254",  # ɔ  hawed
    "er": "\u025d",  # ɝ  heard
    "oa": "o",    # hoed (onset of oʊ)
    "oo": "\u028a",  # ʊ  hood
    "uh": "\u028c",  # ʌ  hud
    "uw": "u",    # who'd
}

HILL_WORD = {
    "iy": "heed", "ih": "hid", "ei": "hayed", "ae": "had",
    "eh": "head", "ah": "hod", "aw": "hawed", "er": "heard",
    "oa": "hoed", "oo": "hood", "uh": "hud", "uw": "who'd",
}

# Display order (front high → back low → rhotics)
DISPLAY_ORDER = ["iy", "ih", "ei", "eh", "ae", "ah", "aw", "oa", "oo", "uw", "uh", "er"]


# ─────────────────────────────────────────────────────────────────────────────
# Parse Hillenbrand vowdata.dat
# ─────────────────────────────────────────────────────────────────────────────

def parse_hillenbrand(vowdata_path: str, gender: str = "m") -> Dict[str, Dict[str, float]]:
    """
    Parse vowdata.dat and compute mean F1/F2/F3 at steady state per vowel,
    filtered to the specified gender prefix ('m' for men, 'w' for women, etc.).

    Returns: {vowel_code: {"f0": mean, "F1": mean, "F2": mean, "F3": mean, "n": count}}
    """
    lines = Path(vowdata_path).read_text(encoding="utf-8", errors="replace").splitlines()

    # Skip header (everything before the first data line starting with a filename)
    data_start = 0
    for idx, line in enumerate(lines):
        stripped = line.strip()
        if stripped and re.match(r'^[mwbg]\d{2}[a-z]{2}\s', stripped):
            data_start = idx
            break

    # Accumulate per-vowel
    accum: Dict[str, Dict[str, List[float]]] = {}

    for line in lines[data_start:]:
        parts = line.split()
        if len(parts) < 7:
            continue

        filename = parts[0]
        if not filename.startswith(gender):
            continue

        # Extract vowel code (last 2 chars of filename)
        vowel_code = filename[3:5]
        if vowel_code not in HILL_TO_IPA:
            continue

        try:
            f0 = float(parts[2])
            f1 = float(parts[3])
            f2 = float(parts[4])
            f3 = float(parts[5])
        except (ValueError, IndexError):
            continue

        if vowel_code not in accum:
            accum[vowel_code] = {"f0": [], "F1": [], "F2": [], "F3": []}

        if f0 > 0:
            accum[vowel_code]["f0"].append(f0)
        if f1 > 0:
            accum[vowel_code]["F1"].append(f1)
        if f2 > 0:
            accum[vowel_code]["F2"].append(f2)
        if f3 > 0:
            accum[vowel_code]["F3"].append(f3)

    # Compute means
    result = {}
    for vc, data in accum.items():
        result[vc] = {}
        for key, vals in data.items():
            result[vc][key] = sum(vals) / len(vals) if vals else 0.0
        result[vc]["n"] = len(data["F1"])
    return result


# ─────────────────────────────────────────────────────────────────────────────
# Parse phonemes.yaml (base phoneme section only)
# ─────────────────────────────────────────────────────────────────────────────

def parse_base_phonemes(yaml_path: str) -> Dict[str, Dict[str, float]]:
    """
    Parse base phoneme definitions from phonemes.yaml.
    Returns: {ipa_key: {"cf1": val, "cf2": val, "cf3": val, "cb1": val, ...}}
    Only includes entries under the top-level `phonemes:` section (indent=2 keys).
    """
    text = Path(yaml_path).read_text(encoding="utf-8").splitlines()
    phonemes: Dict[str, Dict[str, float]] = {}
    current_key: Optional[str] = None
    in_phonemes = False
    in_voice_profiles = False

    for raw in text:
        line = raw.split("#", 1)[0].rstrip()
        if not line.strip():
            continue
        indent = len(line) - len(line.lstrip(" "))
        s = line.strip()

        # Track top-level sections
        if indent == 0:
            if s == "phonemes:":
                in_phonemes = True
                in_voice_profiles = False
            elif s.startswith("voiceProfiles:") or s.startswith("speakers:"):
                in_phonemes = False
                in_voice_profiles = True
            elif not s.startswith("#"):
                in_phonemes = False
                in_voice_profiles = False
            continue

        if not in_phonemes:
            continue

        # Phoneme key at indent=2
        if indent == 2 and s.endswith(":"):
            key = s[:-1].strip()
            if len(key) >= 2 and key[0] == key[-1] and key[0] in ("'", '"'):
                key = key[1:-1]
            current_key = key
            phonemes[current_key] = {}
            continue

        # Field at indent=4
        if indent == 4 and ":" in s and current_key is not None:
            field_name, valstr = s.split(":", 1)
            field_name = field_name.strip()
            valstr = valstr.strip()
            try:
                if "." in valstr:
                    phonemes[current_key][field_name] = float(valstr)
                else:
                    phonemes[current_key][field_name] = int(valstr)
            except ValueError:
                if valstr in ("true", "True"):
                    phonemes[current_key][field_name] = True
                elif valstr in ("false", "False"):
                    phonemes[current_key][field_name] = False

    return phonemes


# ─────────────────────────────────────────────────────────────────────────────
# Comparison
# ─────────────────────────────────────────────────────────────────────────────

def compare(hill: Dict, phonemes: Dict, correction: float = 1.0) -> List[Dict]:
    """Build comparison rows: current vs Hillenbrand for each vowel."""
    rows = []
    for vc in DISPLAY_ORDER:
        ipa = HILL_TO_IPA[vc]
        if vc not in hill:
            continue
        if ipa not in phonemes:
            rows.append({
                "vc": vc, "ipa": ipa, "word": HILL_WORD[vc],
                "missing": True,
            })
            continue

        h = hill[vc]
        p = phonemes[ipa]
        cur_cf1 = p.get("cf1", 0)
        cur_cf2 = p.get("cf2", 0)
        cur_cf3 = p.get("cf3", 0)

        # Hillenbrand target (with optional synth correction)
        tgt_cf1 = round(h["F1"] * correction)
        tgt_cf2 = round(h["F2"] * correction)
        tgt_cf3 = round(h["F3"] * correction)

        rows.append({
            "vc": vc, "ipa": ipa, "word": HILL_WORD[vc],
            "missing": False,
            "cur_cf1": cur_cf1, "cur_cf2": cur_cf2, "cur_cf3": cur_cf3,
            "hill_f1": h["F1"], "hill_f2": h["F2"], "hill_f3": h["F3"],
            "tgt_cf1": tgt_cf1, "tgt_cf2": tgt_cf2, "tgt_cf3": tgt_cf3,
            "d1": cur_cf1 - h["F1"], "d2": cur_cf2 - h["F2"], "d3": cur_cf3 - h["F3"],
            "cur_cb1": p.get("cb1", 0), "cur_cb2": p.get("cb2", 0), "cur_cb3": p.get("cb3", 0),
        })
    return rows


def print_comparison(rows: List[Dict]):
    """Print a formatted comparison table."""
    print()
    print("=" * 100)
    print("  HILLENBRAND vs CURRENT BASE PHONEMES  (delta = current - Hillenbrand)")
    print("=" * 100)
    print(f"  {'Code':<5} {'IPA':<4} {'Word':<7}  "
          f"{'cf1':>5} {'H.F1':>6} {'d1':>6}  "
          f"{'cf2':>5} {'H.F2':>6} {'d2':>6}  "
          f"{'cf3':>5} {'H.F3':>6} {'d3':>6}")
    print("-" * 100)

    for r in rows:
        if r["missing"]:
            print(f"  {r['vc']:<5} {r['ipa']:<4} {r['word']:<7}  ** NOT FOUND IN phonemes.yaml **")
            continue

        def flag(d):
            if abs(d) > 150: return "!!!"
            if abs(d) > 80:  return " ! "
            return "   "

        d1, d2, d3 = r["d1"], r["d2"], r["d3"]
        print(f"  {r['vc']:<5} {r['ipa']:<4} {r['word']:<7}  "
              f"{r['cur_cf1']:>5} {r['hill_f1']:>6.0f} {d1:>+6.0f}{flag(d1)} "
              f"{r['cur_cf2']:>5} {r['hill_f2']:>6.0f} {d2:>+6.0f}{flag(d2)} "
              f"{r['cur_cf3']:>5} {r['hill_f3']:>6.0f} {d3:>+6.0f}{flag(d3)}")

    print()
    print("  Legend:  !!! = >150 Hz off    ! = >80 Hz off")
    print()


def print_suggested(rows: List[Dict]):
    """Print suggested new base phoneme values as YAML snippets."""
    print()
    print("=" * 80)
    print("  SUGGESTED NEW BASE PHONEME VALUES (Hillenbrand-aligned)")
    print("=" * 80)
    print()

    for r in rows:
        if r["missing"]:
            continue
        ipa = r["ipa"]
        # Quote IPA chars that need it
        if any(ord(c) > 127 for c in ipa):
            key = f'"{ipa}"'
        else:
            key = ipa

        changed = []
        if r["cur_cf1"] != r["tgt_cf1"]:
            changed.append(f"cf1: {r['cur_cf1']} -> {r['tgt_cf1']}")
        if r["cur_cf2"] != r["tgt_cf2"]:
            changed.append(f"cf2: {r['cur_cf2']} -> {r['tgt_cf2']}")
        if r["cur_cf3"] != r["tgt_cf3"]:
            changed.append(f"cf3: {r['cur_cf3']} -> {r['tgt_cf3']}")

        if not changed:
            print(f"  {key}: (no change needed)")
            continue

        print(f"  {key}:  # {r['vc']} \"{r['word']}\"")
        print(f"    cf1: {r['tgt_cf1']}")
        print(f"    cf2: {r['tgt_cf2']}")
        print(f"    cf3: {r['tgt_cf3']}")
        # Mirror to parallel formants
        print(f"    pf1: {r['tgt_cf1']}")
        print(f"    pf2: {r['tgt_cf2']}")
        print(f"    pf3: {r['tgt_cf3']}")
        print(f"    # was: cf1={r['cur_cf1']} cf2={r['cur_cf2']} cf3={r['cur_cf3']}")
        print(f"    # delta: {', '.join(changed)}")
        print()


# ─────────────────────────────────────────────────────────────────────────────
# A/B Synthesis
# ─────────────────────────────────────────────────────────────────────────────

def synthesize_ab(rows: List[Dict], phonemes: Dict, out_dir: str,
                  sr: int = 16000, f0: float = 120.0, dur: float = 0.35):
    """Generate A/B WAV pairs for each vowel: current vs Hillenbrand-aligned."""
    try:
        import numpy as np
        # Add tools/ to path so we can import klatt_tune_sim
        tools_dir = str(Path(__file__).parent)
        if tools_dir not in sys.path:
            sys.path.insert(0, tools_dir)
        import klatt_tune_sim as kts
    except ImportError as exc:
        print(f"  Cannot synthesize: {exc}")
        return

    os.makedirs(out_dir, exist_ok=True)

    defaults = {
        "vibratoPitchOffset": 0.0, "vibratoSpeed": 0.0,
        "voiceTurbulenceAmplitude": 0.0, "glottalOpenQuotient": 0.0,
        "preFormantGain": 2.0, "outputGain": 1.5,
    }

    for r in rows:
        if r["missing"]:
            continue

        ipa = r["ipa"]
        vc = r["vc"]
        props = dict(phonemes[ipa])

        # Current values
        frame_cur, fx_cur = kts.build_frame_from_phoneme(props, f0=f0, defaults=defaults)
        wav_cur = kts.synthesize(frame_cur, fx_cur, dur, sr, "engine", 0.62, 1.2)
        cur_path = os.path.join(out_dir, f"current_{vc}_{ipa}.wav")
        kts.write_wav(cur_path, wav_cur, sr)

        # Hillenbrand-aligned values
        props_new = dict(props)
        props_new["cf1"] = r["tgt_cf1"]
        props_new["cf2"] = r["tgt_cf2"]
        props_new["cf3"] = r["tgt_cf3"]
        # Also update parallel formants
        props_new["pf1"] = r["tgt_cf1"]
        props_new["pf2"] = r["tgt_cf2"]
        props_new["pf3"] = r["tgt_cf3"]

        frame_new, fx_new = kts.build_frame_from_phoneme(props_new, f0=f0, defaults=defaults)
        wav_new = kts.synthesize(frame_new, fx_new, dur, sr, "engine", 0.62, 1.2)
        new_path = os.path.join(out_dir, f"hillenbrand_{vc}_{ipa}.wav")
        kts.write_wav(new_path, wav_new, sr)

        # Metrics comparison
        m_cur = kts.spectral_metrics(wav_cur, sr)
        m_new = kts.spectral_metrics(wav_new, sr)
        print(f"  {vc} ({ipa}): current centroid={m_cur['centroid_hz']:.0f} Hz  "
              f"-> hillenbrand centroid={m_new['centroid_hz']:.0f} Hz")

    print(f"\n  WAV pairs written to: {out_dir}/")


# ─────────────────────────────────────────────────────────────────────────────
# CLI
# ─────────────────────────────────────────────────────────────────────────────

def main():
    ap = argparse.ArgumentParser(
        description="Tune TGSpeechBox vowels against Hillenbrand 1995 dataset")
    ap.add_argument("--vowdata", default=None,
                    help="Path to vowdata.dat (default: tools/Hillenbrand/vowdata.dat)")
    ap.add_argument("--phonemes", default=None,
                    help="Path to phonemes.yaml (default: packs/phonemes.yaml)")
    ap.add_argument("--correction", type=float, default=1.0,
                    help="Synth correction factor for Hillenbrand values (default 1.0)")
    ap.add_argument("--synth", action="store_true",
                    help="Generate A/B comparison WAV pairs")
    ap.add_argument("--out-dir", default=None,
                    help="Output directory for WAVs (default: tools/Hillenbrand/comparison)")
    ap.add_argument("--sr", type=int, default=16000, help="Sample rate for synthesis")
    ap.add_argument("--f0", type=float, default=120.0, help="Base pitch Hz")
    ap.add_argument("--dur", type=float, default=0.35, help="Duration per vowel (s)")
    args = ap.parse_args()

    # Resolve default paths relative to repo root
    script_dir = Path(__file__).parent
    repo_root = script_dir.parent

    vowdata_path = args.vowdata or str(script_dir / "Hillenbrand" / "vowdata.dat")
    phonemes_path = args.phonemes or str(repo_root / "packs" / "phonemes.yaml")
    out_dir = args.out_dir or str(script_dir / "Hillenbrand" / "comparison")

    if not Path(vowdata_path).exists():
        print(f"ERROR: {vowdata_path} not found", file=sys.stderr)
        sys.exit(1)
    if not Path(phonemes_path).exists():
        print(f"ERROR: {phonemes_path} not found", file=sys.stderr)
        sys.exit(1)

    print(f"Hillenbrand data: {vowdata_path}")
    print(f"Phonemes YAML:    {phonemes_path}")
    print(f"Correction factor: {args.correction}")

    # Parse
    hill = parse_hillenbrand(vowdata_path, gender="m")
    phonemes = parse_base_phonemes(phonemes_path)

    print(f"\nHillenbrand men: {sum(v['n'] for v in hill.values())} tokens across {len(hill)} vowels")
    found = sum(1 for vc in HILL_TO_IPA if HILL_TO_IPA[vc] in phonemes)
    print(f"Matched to phonemes.yaml: {found}/{len(HILL_TO_IPA)}")

    # Compare
    rows = compare(hill, phonemes, args.correction)
    print_comparison(rows)
    print_suggested(rows)

    # Synthesize
    if args.synth:
        print("\nSynthesizing A/B comparison pairs...")
        synthesize_ab(rows, phonemes, out_dir, sr=args.sr, f0=args.f0, dur=args.dur)

    print("Done.")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""
apply_fricative_stage1.py — Apply Stage 1 bandwidth fixes to phonemes.yaml

Stage 1: Widen cb4/cb5/cb6 by articulatory class.
This is the safest, highest-impact change — it can only make things less harsh.

Usage:
    python3 apply_fricative_stage1.py phonemes.yaml
    # Creates phonemes_stage1.yaml with bandwidth changes applied

    python3 apply_fricative_stage1.py phonemes.yaml --stage 2
    # Also applies Stage 2 frequency changes (cf4/cf5/cf6)

    python3 apply_fricative_stage1.py phonemes.yaml --diff
    # Show what would change without writing
"""

import sys
import os

# Add parent dir for simple_yaml if needed
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from simple_yaml import load_yaml_file

# ============================================================
# Articulatory class definitions
# ============================================================

CLASSES = {
    "non_sibilant": {
        "phonemes": ["f", "v", "θ", "ð"],
        "label": "Non-sibilant (labiodental/interdental)",
        "stage1": {"cb4": 600, "cb5": 1200, "cb6": 2000},
        "stage2": {"cf4": 3500, "cf5": 4500, "cf6": 6000},
    },
    "alveolar_sibilant": {
        "phonemes": ["s", "z"],
        "label": "Alveolar sibilant",
        "stage1": {"cb4": 350, "cb5": 500, "cb6": 1500},
        "stage2": {"cf4": 3500, "cf5": 5500, "cf6": 7500},
    },
    "postalveolar_sibilant": {
        "phonemes": ["ʃ", "ʒ"],
        "label": "Postalveolar sibilant",
        "stage1": {"cb4": 400, "cb5": 700, "cb6": 1500},
        "stage2": {"cf4": 3300, "cf5": 4000, "cf6": 5500},
    },
    "alveolopalatal": {
        "phonemes": ["ɕ", "ʑ", "ç", "ʝ"],
        "label": "Alveolopalatal",
        "stage1": {"cb4": 380, "cb5": 600, "cb6": 1500},
        "stage2": {"cf4": 3400, "cf5": 4800, "cf6": 6500},
    },
    "retroflex": {
        "phonemes": ["ʂ", "ʐ"],
        "label": "Retroflex sibilant",
        "stage1": {"cb4": 450, "cb5": 800, "cb6": 1500},
        "stage2": {"cf4": 3200, "cf5": 3800, "cf6": 5000},
    },
    "velar_uvular": {
        "phonemes": ["x", "ɣ", "X", "ʁ", "ɧ"],
        "label": "Velar/uvular fricative",
        "stage1": {"cb4": 500, "cb5": 1000, "cb6": 1800},
        "stage2": {"cf4": 3000, "cf5": 3500, "cf6": 4500},
    },
    "stop_labial": {
        "phonemes": ["p", "b"],
        "label": "Labial stop",
        "stage1": {"cb4": 600, "cb5": 1000, "cb6": 1800},
        "stage2": {},  # keep default frequencies
    },
    "stop_alveolar": {
        "phonemes": ["t", "d"],
        "label": "Alveolar stop",
        "stage1": {"cb4": 350, "cb5": 500, "cb6": 1500},
        "stage2": {"cf4": 3500, "cf5": 5500, "cf6": 7500},
    },
    "stop_velar": {
        "phonemes": ["k", "g", "ɡ", "c", "ɟ"],
        "label": "Velar/palatal stop",
        "stage1": {"cb4": 450, "cb5": 800, "cb6": 1500},
        "stage2": {"cf4": 3000, "cf5": 3500, "cf6": 4500},
    },
    "affricate_alveolar": {
        "phonemes": ["t͡s", "d͡z"],
        "label": "Alveolar affricate",
        "stage1": {"cb4": 350, "cb5": 500, "cb6": 1500},
        "stage2": {"cf4": 3500, "cf5": 5500, "cf6": 7500},
    },
    "affricate_postalveolar": {
        "phonemes": ["t͡ʃ", "d͡ʒ"],
        "label": "Postalveolar affricate",
        "stage1": {"cb4": 400, "cb5": 700, "cb6": 1500},
        "stage2": {"cf4": 3300, "cf5": 4000, "cf6": 5500},
    },
    "affricate_alveolopalatal": {
        "phonemes": ["t͡ɕ", "d͡ʑ"],
        "label": "Alveolopalatal affricate",
        "stage1": {"cb4": 380, "cb5": 600, "cb6": 1500},
        "stage2": {"cf4": 3400, "cf5": 4800, "cf6": 6500},
    },
    "affricate_retroflex": {
        "phonemes": ["t͡ʂ", "d͡ʐ"],
        "label": "Retroflex affricate",
        "stage1": {"cb4": 450, "cb5": 800, "cb6": 1500},
        "stage2": {"cf4": 3200, "cf5": 3800, "cf6": 5000},
    },
}

# Also apply to parallel bandwidths (pb4/pb5/pb6) — same classes, same ratios
# but scaled relative to the existing pb values (which do have slight variation)
PARALLEL_BW_SCALE = True  # Whether to also adjust pb4/pb5/pb6


def build_phoneme_class_map():
    """Map each phoneme to its articulatory class."""
    pmap = {}
    for cls_key, cls_data in CLASSES.items():
        for p in cls_data["phonemes"]:
            pmap[p] = cls_key
    return pmap


def apply_changes(phonemes, stage=1, diff_only=False):
    """Apply bandwidth (stage 1) and optionally frequency (stage 2) changes."""
    pmap = build_phoneme_class_map()
    changes = []

    for pkey, pdata in phonemes.items():
        if not isinstance(pdata, dict):
            continue
        if pkey not in pmap:
            continue

        cls_key = pmap[pkey]
        cls = CLASSES[cls_key]

        # Stage 1: bandwidths
        for param, new_val in cls["stage1"].items():
            old_val = pdata.get(param)
            if old_val is not None:
                old_val = float(old_val)
                if abs(old_val - new_val) > 0.01:
                    changes.append({
                        "phoneme": pkey,
                        "class": cls["label"],
                        "param": param,
                        "old": old_val,
                        "new": new_val,
                        "stage": 1,
                    })
                    if not diff_only:
                        pdata[param] = new_val

        # Also scale parallel bandwidths proportionally
        if PARALLEL_BW_SCALE:
            for cparam, new_cascade_val in cls["stage1"].items():
                pparam = cparam.replace("cb", "pb")  # cb4 -> pb4
                old_cascade = 250 if cparam == "cb4" else (200 if cparam == "cb5" else 1000)
                scale = new_cascade_val / old_cascade
                old_pval = pdata.get(pparam)
                if old_pval is not None:
                    old_pval = float(old_pval)
                    new_pval = round(old_pval * scale)
                    if abs(old_pval - new_pval) > 0.01:
                        changes.append({
                            "phoneme": pkey,
                            "class": cls["label"],
                            "param": pparam,
                            "old": old_pval,
                            "new": new_pval,
                            "stage": 1,
                        })
                        if not diff_only:
                            pdata[pparam] = new_pval

        # Stage 2: frequencies
        if stage >= 2:
            for param, new_val in cls.get("stage2", {}).items():
                old_val = pdata.get(param)
                if old_val is not None:
                    old_val = float(old_val)
                    if abs(old_val - new_val) > 0.01:
                        changes.append({
                            "phoneme": pkey,
                            "class": cls["label"],
                            "param": param,
                            "old": old_val,
                            "new": new_val,
                            "stage": 2,
                        })
                        if not diff_only:
                            pdata[param] = new_val

    return changes


def print_diff(changes):
    """Print changes grouped by class."""
    by_class = {}
    for c in changes:
        key = c["class"]
        if key not in by_class:
            by_class[key] = []
        by_class[key].append(c)

    total = 0
    for cls_label, cls_changes in sorted(by_class.items()):
        print(f"\n  {cls_label}:")
        # Group by phoneme
        by_phone = {}
        for c in cls_changes:
            if c["phoneme"] not in by_phone:
                by_phone[c["phoneme"]] = []
            by_phone[c["phoneme"]].append(c)

        for phone, pchanges in sorted(by_phone.items()):
            parts = []
            for c in pchanges:
                stage_tag = f"[S{c['stage']}]" if c["stage"] > 1 else ""
                parts.append(f"{c['param']}: {c['old']:.0f} -> {c['new']:.0f}{stage_tag}")
            print(f"    {phone}: {', '.join(parts)}")
            total += len(pchanges)

    print(f"\n  Total: {total} parameter changes across {len(by_class)} classes")


def main():
    import argparse

    parser = argparse.ArgumentParser(description="Apply fricative tuning stages")
    parser.add_argument("input", help="Input phonemes.yaml")
    parser.add_argument("--stage", type=int, default=1, choices=[1, 2],
                        help="Stage to apply (1=bandwidths, 2=bandwidths+frequencies)")
    parser.add_argument("--diff", action="store_true",
                        help="Show changes without writing")
    parser.add_argument("--output", "-o", help="Output filename (default: phonemes_stageN.yaml)")
    args = parser.parse_args()

    data = load_yaml_file(args.input)
    phonemes = data.get("phonemes", {})

    print(f"Loaded {len(phonemes)} phonemes from {args.input}")

    changes = apply_changes(phonemes, stage=args.stage, diff_only=args.diff)

    if not changes:
        print("No changes needed!")
        return

    print(f"\nStage {args.stage} changes:")
    print_diff(changes)

    if args.diff:
        print("\n(Diff only — no file written)")
        return

    # Write changes by line-editing the original file
    out_path = args.output or args.input.replace(".yaml", f"_stage{args.stage}.yaml")

    # Build lookup: (phoneme_key, param) -> new_value
    change_map = {}
    for c in changes:
        change_map[(c["phoneme"], c["param"])] = c["new"]

    # Parse and rewrite line by line
    with open(args.input, "r", encoding="utf-8") as f:
        lines = f.readlines()

    current_phoneme = None
    output_lines = []
    in_phonemes = False

    for line in lines:
        stripped = line.rstrip()
        indent = len(line) - len(line.lstrip())

        # Detect phoneme key (2-space indent, IPA key + colon)
        if in_phonemes and indent == 2 and ":" in stripped:
            candidate = stripped.strip().rstrip(":")
            # Remove quotes if present (YAML quotes IPA characters)
            if candidate.startswith('"') and candidate.endswith('"'):
                candidate = candidate[1:-1]
            # Phoneme keys are at indent=2 under 'phonemes:'
            current_phoneme = candidate

        if stripped == "phonemes:":
            in_phonemes = True

        # Detect parameter line (4-space indent under a phoneme)
        if current_phoneme and indent == 4 and ":" in stripped:
            param_part = stripped.strip()
            colon_idx = param_part.index(":")
            param_name = param_part[:colon_idx].strip()
            lookup = (current_phoneme, param_name)

            if lookup in change_map:
                new_val = change_map[lookup]
                # Format: preserve indent, update value
                if isinstance(new_val, float) and new_val == int(new_val):
                    val_str = str(int(new_val))
                elif isinstance(new_val, int):
                    val_str = str(new_val)
                else:
                    val_str = str(new_val)
                output_lines.append(f"    {param_name}: {val_str}\n")
                continue

        output_lines.append(line)

    with open(out_path, "w", encoding="utf-8") as f:
        f.writelines(output_lines)

    print(f"\nWritten to {out_path}")


if __name__ == "__main__":
    main()

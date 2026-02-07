#!/usr/bin/env python3
"""
apply_sonorant_fix.py — Smooth upper formant bandwidths for sonorants.

After Stages 1-2 widened obstruent bandwidths, every obstruent→sonorant
transition is a bandwidth cliff (e.g. /b/ cb5=1000 → /r/ cb5=200).
This fix gives sonorants moderate bandwidths to smooth those transitions.

Usage:
    python3 apply_sonorant_fix.py phonemes_current.yaml
    python3 apply_sonorant_fix.py phonemes_current.yaml --diff
"""

import sys
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from simple_yaml import load_yaml_file

# ============================================================
# Sonorant class definitions
# ============================================================

SONORANT_CLASSES = {
    "nasal": {
        "phonemes": ["m", "n", "ŋ", "ɲ"],  # Core nasals (not nasal vowels)
        "label": "Nasal",
        "changes": {"cb4": 300, "cb5": 350, "cb6": 1300},
    },
    "nasal_palatal": {
        # The second ɲ variant (with PUA char) — grab by examining the file
        "phonemes": [],  # Will be filled dynamically
        "label": "Nasal (palatal variant)",
        "changes": {"cb4": 300, "cb5": 350, "cb6": 1300},
    },
    "liquid_lateral": {
        "phonemes": ["l", "ɫ", "ɭ", "ʎ"],
        "label": "Lateral liquid",
        "changes": {"cb4": 300, "cb5": 350, "cb6": 1300},
    },
    "liquid_rhotic": {
        "phonemes": ["ɹ", "ɻ"],
        "label": "Rhotic approximant",
        "changes": {"cb4": 300, "cb5": 350, "cb6": 1300},
    },
    "liquid_other": {
        "phonemes": ["β"],  # Bilabial approximant, classed as liquid
        "label": "Other liquid",
        "changes": {"cb4": 300, "cb5": 350, "cb6": 1300},
    },
    "trill_tap": {
        "phonemes": ["r", "ɾ", "ɽ"],
        "label": "Trill/tap",
        "changes": {"cb4": 320, "cb5": 400, "cb6": 1400},
    },
    "semivowel": {
        "phonemes": ["j", "w"],
        "label": "Semivowel",
        "changes": {"cb4": 280, "cb5": 300, "cb6": 1200},
    },
    "semivowel_weak": {
        # Weak/reduced semivowel variants
        "phonemes": ["ᴊ", "ᵂ", "ᵊ"],
        "label": "Weak semivowel",
        "changes": {"cb4": 280, "cb5": 300, "cb6": 1200},
    },
    "glottal_stop": {
        "phonemes": ["?", "ʔ", "t(3"],
        "label": "Glottal stop/creak",
        "changes": {"cb4": 350, "cb5": 400, "cb6": 1400},
    },
    "voiceless_w": {
        "phonemes": ["ʍ"],
        "label": "Voiceless labial-velar",
        "changes": {"cb4": 280, "cb5": 300, "cb6": 1200},
    },
}


def build_phoneme_map(phonemes):
    """Map phoneme keys to their sonorant class, handling PUA chars."""
    pmap = {}
    for cls_key, cls_data in SONORANT_CLASSES.items():
        for p in cls_data["phonemes"]:
            if p in phonemes:
                pmap[p] = cls_key

    # Catch any remaining sonorants at cb5=200 that we missed
    # (PUA characters, nasal vowels, etc.)
    missed = []
    for key, pdata in phonemes.items():
        if not isinstance(pdata, dict):
            continue
        if key in pmap:
            continue

        cb5 = float(pdata.get("cb5", 0))
        if abs(cb5 - 200) > 1:
            continue  # Already changed

        is_vowel = pdata.get("_isVowel", False)
        is_nasal = pdata.get("_isNasal", False)
        is_liquid = pdata.get("_isLiquid", False)
        is_semivowel = pdata.get("_isSemivowel", False)
        is_trill = pdata.get("_isTrill", False)
        is_tap = pdata.get("_isTap", False)

        # Skip pure vowels (even nasal vowels) — those are for the vowel stage
        if is_vowel:
            continue

        # Classify remaining sonorants
        if is_nasal:
            pmap[key] = "nasal"
        elif is_liquid:
            pmap[key] = "liquid_lateral"
        elif is_semivowel:
            pmap[key] = "semivowel_weak"
        elif is_trill or is_tap:
            pmap[key] = "trill_tap"

        # Check for unclassified non-vowels still at default
        if key not in pmap and not is_vowel:
            fric = float(pdata.get("fricationAmplitude", 0))
            voice = float(pdata.get("voiceAmplitude", 0))
            if fric > 0 or voice > 0:
                missed.append(key)

    return pmap, missed


def apply_changes(phonemes, diff_only=False):
    """Apply sonorant bandwidth smoothing."""
    pmap, missed = build_phoneme_map(phonemes)
    changes = []

    for pkey, cls_key in pmap.items():
        cls = SONORANT_CLASSES[cls_key]
        pdata = phonemes[pkey]

        for param, new_val in cls["changes"].items():
            old_val = float(pdata.get(param, 0))
            if abs(old_val - new_val) > 0.01:
                changes.append({
                    "phoneme": pkey,
                    "class": cls["label"],
                    "param": param,
                    "old": old_val,
                    "new": new_val,
                })
                if not diff_only:
                    pdata[param] = new_val

            # Also scale parallel bandwidth proportionally
            pparam = param.replace("cb", "pb")
            old_cascade_default = {"cb4": 250, "cb5": 200, "cb6": 1000}[param]
            scale = new_val / old_cascade_default
            old_pval = float(pdata.get(pparam, 0))
            new_pval = round(old_pval * scale)
            if abs(old_pval - new_pval) > 0.01:
                changes.append({
                    "phoneme": pkey,
                    "class": cls["label"],
                    "param": pparam,
                    "old": old_pval,
                    "new": new_pval,
                })
                if not diff_only:
                    pdata[pparam] = new_pval

    if missed:
        print(f"\n  WARNING: {len(missed)} unclassified phonemes still at cb5=200: {missed}")
        print("  These may need manual review.")

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
        by_phone = {}
        for c in cls_changes:
            if c["phoneme"] not in by_phone:
                by_phone[c["phoneme"]] = []
            by_phone[c["phoneme"]].append(c)

        for phone, pchanges in sorted(by_phone.items()):
            parts = []
            for c in pchanges:
                parts.append(f"{c['param']}: {c['old']:.0f} -> {c['new']:.0f}")
            print(f"    {phone}: {', '.join(parts)}")
            total += len(pchanges)

    print(f"\n  Total: {total} parameter changes across {len(by_class)} classes")


def write_output(input_path, output_path, changes):
    """Line-edit the YAML to apply changes."""
    change_map = {}
    for c in changes:
        change_map[(c["phoneme"], c["param"])] = c["new"]

    with open(input_path, "r", encoding="utf-8") as f:
        lines = f.readlines()

    current_phoneme = None
    output_lines = []
    in_phonemes = False

    for line in lines:
        stripped = line.rstrip()
        indent = len(line) - len(line.lstrip())

        if stripped == "phonemes:":
            in_phonemes = True

        if in_phonemes and indent == 2 and ":" in stripped:
            candidate = stripped.strip().rstrip(":")
            if candidate.startswith('"') and candidate.endswith('"'):
                candidate = candidate[1:-1]
            current_phoneme = candidate

        if current_phoneme and indent == 4 and ":" in stripped:
            param_part = stripped.strip()
            colon_idx = param_part.index(":")
            param_name = param_part[:colon_idx].strip()
            lookup = (current_phoneme, param_name)

            if lookup in change_map:
                new_val = change_map[lookup]
                if isinstance(new_val, float) and new_val == int(new_val):
                    val_str = str(int(new_val))
                elif isinstance(new_val, int):
                    val_str = str(new_val)
                else:
                    val_str = str(new_val)
                output_lines.append(f"    {param_name}: {val_str}\n")
                continue

        output_lines.append(line)

    with open(output_path, "w", encoding="utf-8") as f:
        f.writelines(output_lines)


def main():
    import argparse

    parser = argparse.ArgumentParser(description="Apply sonorant bandwidth smoothing")
    parser.add_argument("input", help="Input phonemes YAML (post stage-2)")
    parser.add_argument("--diff", action="store_true", help="Show changes without writing")
    parser.add_argument("--output", "-o", help="Output filename")
    args = parser.parse_args()

    data = load_yaml_file(args.input)
    phonemes = data.get("phonemes", {})
    print(f"Loaded {len(phonemes)} phonemes from {args.input}")

    changes = apply_changes(phonemes, diff_only=args.diff)

    if not changes:
        print("No changes needed!")
        return

    print_diff(changes)

    if args.diff:
        print("\n(Diff only — no file written)")
        return

    out_path = args.output or args.input.replace(".yaml", "_sonorants.yaml")
    write_output(args.input, out_path, changes)
    print(f"\nWritten to {out_path}")


if __name__ == "__main__":
    main()

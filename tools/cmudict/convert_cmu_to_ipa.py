#!/usr/bin/env python3
"""
convert_cmu_to_ipa.py  --  CMUdict ARPAbet to IPA converter for TGSpeechBox

Reads the CMUdict .dict file (ARPAbet with stress digits) and writes a
tab-separated file:   WORD <tab> IPA

Stress handling
---------------
ARPAbet marks stress on the vowel nucleus:  0 = no stress, 1 = primary, 2 = secondary.
IPA places the stress mark before the *syllable onset* (the consonant(s) preceding the
vowel).  This script walks the phone list and inserts U+02C8 (primary) or U+02CC
(secondary) before the earliest onset consonant of each stressed syllable.

Usage
-----
    python convert_cmu_to_ipa.py                        # defaults: cmudict.dict -> cmudict_ipa.tsv
    python convert_cmu_to_ipa.py -i other.dict -o out.tsv
    python convert_cmu_to_ipa.py --stats                # print mapping stats at the end
"""

from __future__ import annotations

import argparse
import io
import os
import re
import sys
from pathlib import Path

# Force UTF-8 on stdout/stderr so IPA characters print correctly on Windows
if sys.stdout.encoding != "utf-8":
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")
if sys.stderr.encoding != "utf-8":
    sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding="utf-8", errors="replace")

# ---------------------------------------------------------------------------
# ARPAbet -> IPA mappings targeting TGSpeechBox's phoneme inventory
# ---------------------------------------------------------------------------

# Vowels (base form, without stress digit)
# Stress digit is stripped before lookup; AH and ER are context-sensitive.
#
# These mappings target eSpeak's IPA conventions so that CMUdict output can
# pass through en-us.yaml normalization rules without double-corrections.
ARPABET_VOWELS: dict[str, str] = {
    "AA": "\u0251\u02d0",      # ɑː  (eSpeak uses ɑː for LOT/PALM/START)
    "AE": "\u00e6",            # æ
    "AH": "\u0259",            # ə   (default / unstressed; stressed → ʌ)
    "AO": "\u0254\u02d0",      # ɔː  (eSpeak uses ɔː for THOUGHT/FORCE)
    "AW": "a\u0361\u028a",     # a͡ʊ (tie bar protects from a→æ rule)
    "AY": "a\u0361\u026a",     # a͡ɪ (tie bar protects from a→æ rule)
    "EH": "\u025b",            # ɛ
    "ER": "\u025a",            # ɚ   (unstressed lettER; stressed → ɜː)
    "EY": "e\u026a",           # eɪ
    "IH": "\u026a",            # ɪ
    "IY": "i\u02d0",           # iː
    "OW": "o\u028a",           # oʊ
    "OY": "\u0254\u026a",      # ɔɪ
    "UH": "\u028a",            # ʊ
    "UW": "u\u02d0",           # uː
}

# AH with primary or secondary stress becomes ʌ instead of ə
AH_STRESSED = "\u028c"  # ʌ

# ER with primary or secondary stress becomes ɜː (NURSE) instead of ɚ (lettER)
ER_STRESSED = "\u025c\u02d0"  # ɜː

# Consonants -- mostly transparent
ARPABET_CONSONANTS: dict[str, str] = {
    "B":  "b",
    "CH": "t\u0283",     # tʃ
    "D":  "d",
    "DH": "\u00f0",      # ð
    "F":  "f",
    "G":  "\u0261",      # ɡ  (IPA U+0261, not ASCII g)
    "HH": "h",
    "JH": "d\u0292",     # dʒ
    "K":  "k",
    "L":  "l",
    "M":  "m",
    "N":  "n",
    "NG": "\u014b",      # ŋ
    "P":  "p",
    "R":  "\u0279",      # ɹ
    "S":  "s",
    "SH": "\u0283",      # ʃ
    "T":  "t",
    "TH": "\u03b8",      # θ
    "V":  "v",
    "W":  "w",
    "Y":  "j",
    "Z":  "z",
    "ZH": "\u0292",      # ʒ
}

# Merged lookup (vowel base forms + consonants)
ARPABET_TO_IPA: dict[str, str] = {**ARPABET_VOWELS, **ARPABET_CONSONANTS}

# Pre-compiled regex: a vowel phone is 2 uppercase letters followed by a digit
_VOWEL_RE = re.compile(r"^([A-Z]{1,2})([012])$")

# ---------------------------------------------------------------------------
# Legal English onset clusters (for stress-mark placement)
# ---------------------------------------------------------------------------
# Any single consonant is a valid onset; these are the multi-consonant clusters.
# Tuples of IPA phoneme strings, read left-to-right as they'd appear in speech.

_R = "\u0279"  # ɹ
_G = "\u0261"  # ɡ
_TH = "\u03b8" # θ
_SH = "\u0283" # ʃ

LEGAL_ONSET_CLUSTERS: set[tuple[str, ...]] = {
    # -- Two-consonant: stop + liquid/glide --
    ("p", "l"), ("p", _R), ("p", "j"), ("p", "w"),
    ("b", "l"), ("b", _R), ("b", "j"),
    ("t", _R),  ("t", "w"), ("t", "j"),
    ("d", _R),  ("d", "w"), ("d", "j"),
    ("k", "l"), ("k", _R), ("k", "w"), ("k", "j"),
    (_G, "l"),  (_G, _R),  (_G, "w"),  (_G, "j"),
    # -- Two-consonant: fricative + liquid/glide --
    ("f", "l"), ("f", _R), ("f", "j"),
    ("v", "j"),
    (_TH, _R),  (_TH, "w"), (_TH, "j"),
    (_SH, _R),
    ("s", "l"), ("s", "w"), ("s", "j"),
    ("h", "j"), ("h", "w"),
    # -- Two-consonant: s + obstruent --
    ("s", "p"), ("s", "t"), ("s", "k"), ("s", "f"),
    # -- Two-consonant: s + nasal --
    ("s", "m"), ("s", "n"),
    # -- Two-consonant: nasal/liquid + glide --
    ("m", "j"), ("n", "j"), ("l", "j"),
    # -- Three-consonant: s + stop + liquid/glide --
    ("s", "p", "l"), ("s", "p", _R), ("s", "p", "j"),
    ("s", "t", _R),  ("s", "t", "j"),
    ("s", "k", "l"), ("s", "k", _R), ("s", "k", "w"), ("s", "k", "j"),
}


def _is_legal_onset(cluster: tuple[str, ...]) -> bool:
    """Check whether *cluster* is a legal English syllable onset."""
    if len(cluster) <= 1:
        return True  # any single consonant is a valid onset
    return cluster in LEGAL_ONSET_CLUSTERS

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def is_vowel_phone(phone: str) -> bool:
    """Return True if *phone* (with stress digit) is a vowel."""
    return _VOWEL_RE.match(phone) is not None


def parse_phone(phone: str) -> tuple[str, int | None]:
    """Split an ARPAbet phone into (base, stress).

    Consonants return stress=None.  Vowels return 0, 1, or 2.
    """
    m = _VOWEL_RE.match(phone)
    if m:
        return m.group(1), int(m.group(2))
    return phone, None


def phone_to_ipa(base: str, stress: int | None) -> str:
    """Convert a single ARPAbet phone to IPA."""
    if base == "AH" and stress is not None and stress > 0:
        return AH_STRESSED   # ʌ (stressed STRUT)
    if base == "ER" and stress is not None and stress > 0:
        return ER_STRESSED   # ɜː (stressed NURSE)
    ipa = ARPABET_TO_IPA.get(base)
    if ipa is None:
        # Unknown phone -- pass through wrapped in angle brackets
        return f"<{base}>"
    return ipa


# ---------------------------------------------------------------------------
# Core conversion
# ---------------------------------------------------------------------------

def arpabet_to_ipa(phones: list[str]) -> str:
    """Convert a list of ARPAbet phones to an IPA string with stress marks.

    Strategy for stress placement:
    1.  Walk the phone list left-to-right, converting each phone to IPA.
    2.  Collect converted segments into a flat list of (ipa_text, stress) tuples.
    3.  Then walk the list *backwards* from each stressed vowel to find the
        syllable onset (consecutive consonants before the vowel) and insert
        the stress mark before the first consonant of that onset cluster.
        If the vowel is word-initial (no preceding consonant), the mark goes
        directly before the vowel.
    """
    # Phase 1: convert each phone
    segments: list[tuple[str, bool, int | None]] = []
    # Each entry: (ipa_string, is_vowel, stress_level)
    for phone in phones:
        base, stress = parse_phone(phone)
        ipa = phone_to_ipa(base, stress)
        segments.append((ipa, stress is not None, stress))

    # Phase 2: determine where to insert stress marks
    # We build a list of IPA chunks; stress marks get their own chunk.
    output_parts: list[str] = []
    # Track which indices need a stress mark inserted *before their onset*
    # We process segments left-to-right and insert marks retroactively.

    # Simple approach: build a list, then do a second pass.
    ipa_pieces: list[str] = [seg[0] for seg in segments]
    stress_levels: list[int | None] = [seg[2] for seg in segments]
    is_vowel_flags: list[bool] = [seg[1] for seg in segments]

    # For each stressed vowel, walk backwards to find the onset start,
    # constrained by English onset legality (maximal onset principle).
    insert_marks: dict[int, str] = {}  # index -> mark to insert *before* that index
    for i, (ipa, is_v, stress) in enumerate(segments):
        if not is_v or stress is None or stress == 0:
            continue
        mark = "\u02c8" if stress == 1 else "\u02cc"  # ˈ or ˌ
        # Walk backwards over consonants, building the onset cluster.
        # Only extend the onset if the resulting cluster is a legal English onset.
        onset_start = i
        cluster: list[str] = []
        j = i - 1
        while j >= 0 and not is_vowel_flags[j]:
            candidate = tuple([ipa_pieces[j]] + cluster)
            if _is_legal_onset(candidate):
                cluster = list(candidate)
                onset_start = j
                j -= 1
            else:
                break
        if onset_start in insert_marks:
            # Higher stress wins
            if mark == "\u02c8":
                insert_marks[onset_start] = mark
        else:
            insert_marks[onset_start] = mark

    # Phase 3: assemble output
    result: list[str] = []
    for i, piece in enumerate(ipa_pieces):
        if i in insert_marks:
            result.append(insert_marks[i])
        result.append(piece)

    return "".join(result)


# ---------------------------------------------------------------------------
# File I/O
# ---------------------------------------------------------------------------

def parse_cmudict_line(line: str) -> tuple[str, list[str]] | None:
    """Parse one line of cmudict.dict.

    Returns (word, [phones]) or None for comment/blank lines.
    Lines look like:
        about  AH0 B AW1 T
        a(2)   EY1
    """
    line = line.strip()
    if not line or line.startswith(";;;"):
        return None
    parts = line.split()
    if len(parts) < 2:
        return None
    word = parts[0]
    phones = parts[1:]
    # Some entries have metadata after a '#' marker (e.g. "# place, danish").
    # Truncate the phone list at the first '#'.
    if "#" in phones:
        phones = phones[:phones.index("#")]
    return word, phones


def convert_file(
    input_path: Path,
    output_path: Path,
    *,
    show_stats: bool = False,
) -> None:
    """Read a CMUdict file and write a TSV of word -> IPA."""
    total = 0
    written = 0
    unknown_phones: dict[str, int] = {}

    print(f"Reading: {input_path}")
    print(f"Writing: {output_path}")

    with open(input_path, encoding="latin-1") as fin, \
         open(output_path, "w", encoding="utf-8") as fout:

        for line in fin:
            total += 1
            parsed = parse_cmudict_line(line)
            if parsed is None:
                continue
            word, phones = parsed
            ipa = arpabet_to_ipa(phones)

            # Track unknown phones
            for ch in re.findall(r"<([A-Z]+)>", ipa):
                unknown_phones[ch] = unknown_phones.get(ch, 0) + 1

            fout.write(f"{word}\t{ipa}\n")
            written += 1

    print(f"Done. {written} entries written from {total} lines.")

    if show_stats or unknown_phones:
        if unknown_phones:
            print("Warning: unknown ARPAbet phones encountered:")
            for ph, count in sorted(unknown_phones.items()):
                print(f"  {ph}: {count} occurrences")
        else:
            print("All phones mapped successfully, no unknowns.")


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main() -> None:
    here = Path(__file__).resolve().parent

    parser = argparse.ArgumentParser(
        description="Convert CMUdict ARPAbet to IPA for TGSpeechBox"
    )
    parser.add_argument(
        "-i", "--input",
        type=Path,
        default=here / "cmudict.dict",
        help="Path to CMUdict .dict file (default: cmudict.dict in same directory)",
    )
    parser.add_argument(
        "-o", "--output",
        type=Path,
        default=here / "cmudict_ipa.tsv",
        help="Output TSV path (default: cmudict_ipa.tsv in same directory)",
    )
    parser.add_argument(
        "--stats",
        action="store_true",
        help="Print mapping statistics after conversion",
    )
    parser.add_argument(
        "--sample",
        type=int,
        metavar="N",
        help="Only convert first N entries (for quick testing)",
    )
    parser.add_argument(
        "--lookup",
        type=str,
        metavar="WORD",
        help="Look up a single word and print its IPA (no file output)",
    )
    args = parser.parse_args()

    if args.lookup:
        # Quick single-word lookup mode
        target = args.lookup.upper()
        with open(args.input, encoding="latin-1") as f:
            for line in f:
                parsed = parse_cmudict_line(line)
                if parsed is None:
                    continue
                word, phones = parsed
                # Strip variant suffix like (2) for matching
                base_word = re.sub(r"\(\d+\)$", "", word).upper()
                if base_word == target:
                    ipa = arpabet_to_ipa(phones)
                    print(f"{word}\t{' '.join(phones)}\t{ipa}")
        return

    if args.sample:
        # Sample mode: convert only first N entries
        print(f"Sample mode: converting first {args.sample} entries.")
        count = 0
        with open(args.input, encoding="latin-1") as fin, \
             open(args.output, "w", encoding="utf-8") as fout:
            for line in fin:
                parsed = parse_cmudict_line(line)
                if parsed is None:
                    continue
                word, phones = parsed
                ipa = arpabet_to_ipa(phones)
                fout.write(f"{word}\t{ipa}\n")
                print(f"  {word}\t{ipa}")
                count += 1
                if count >= args.sample:
                    break
        print(f"Done. {count} sample entries written to {args.output}")
        return

    convert_file(args.input, args.output, show_stats=args.stats)


if __name__ == "__main__":
    main()

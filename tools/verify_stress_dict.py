#!/usr/bin/env python3
"""
TGSpeechBox — eSpeak stress dictionary alignment verification.

Reads packs/dict/en-us-stress.tsv, runs each word through espeak-ng,
counts vowel nuclei in the IPA output (matching C++ findNuclei() exactly),
and produces two files:

  tools/stress_audit.tsv         — full audit log (PASS/FAIL per word)
  packs/dict/en-us-stress-verified.tsv — only PASS entries (clean dict)

Usage:
  python tools/verify_stress_dict.py [--jobs N]
"""

import argparse
import subprocess
import sys
from concurrent.futures import ProcessPoolExecutor, as_completed
from pathlib import Path

# ---------------------------------------------------------------------------
# Vowel set — must exactly match isIpaVowel() in text_parser.cpp:35-68
# ---------------------------------------------------------------------------
IPA_VOWELS = set(
    "aeiouy"
    "\u0251"  # ɑ  open back unrounded
    "\u00E6"  # æ  near-open front unrounded
    "\u025B"  # ɛ  open-mid front unrounded
    "\u026A"  # ɪ  near-close front unrounded
    "\u0254"  # ɔ  open-mid back rounded
    "\u0259"  # ə  schwa
    "\u028A"  # ʊ  near-close back rounded
    "\u028C"  # ʌ  open-mid back unrounded
    "\u0252"  # ɒ  open back rounded
    "\u025C"  # ɜ  open-mid central unrounded
    "\u0250"  # ɐ  near-open central
    "\u0264"  # ɤ  close-mid back unrounded
    "\u0275"  # ɵ  close-mid central rounded
    "\u0258"  # ɘ  close-mid central unrounded
    "\u025E"  # ɞ  open-mid central rounded
    "\u0276"  # ɶ  open front rounded
    "\u0268"  # ɨ  close central unrounded
    "\u0289"  # ʉ  close central rounded
    "\u026F"  # ɯ  close back unrounded
    "\u025D"  # ɝ  r-colored schwa
    "\u025A"  # ɚ  r-colored schwa (mid central)
    "\u00F8"  # ø  close-mid front rounded
    "\u1D7B"  # ᵻ  near-close central unrounded (eSpeak)
    "\u1D7F"  # ᵿ  near-close central rounded (eSpeak)
)

STRESS_MARKS = {"\u02C8", "\u02CC"}  # ˈ ˌ
TIE_BAR = "\u0361"       # ◌͡
SYLLABIC_MARK = "\u0329"  # ◌̩
LENGTH_MARK = "\u02D0"    # ː


def strip_stress(ipa: str) -> str:
    """Remove ˈ and ˌ from IPA string."""
    return "".join(c for c in ipa if c not in STRESS_MARKS)


def count_nuclei(ipa: str) -> int:
    """Count vowel nuclei — matches C++ findNuclei() exactly."""
    nuclei = 0
    in_vowel = False
    i = 0
    while i < len(ipa):
        c = ipa[i]

        # Tie bar after vowel: binds next char into same nucleus
        if c == TIE_BAR and in_vowel:
            if i + 1 < len(ipa):
                i += 1  # skip next char (bound into nucleus)
            i += 1
            continue

        # Syllabic consonant: non-vowel + U+0329 = nucleus
        if c not in IPA_VOWELS and not in_vowel:
            if i + 1 < len(ipa) and ipa[i + 1] == SYLLABIC_MARK:
                nuclei += 1
                i += 2  # skip consonant + syllabic mark
                in_vowel = False
                continue

        if c in IPA_VOWELS:
            if not in_vowel:
                nuclei += 1
                in_vowel = True
        elif c == LENGTH_MARK and in_vowel:
            pass  # extends nucleus
        else:
            in_vowel = False

        i += 1

    return nuclei


def espeak_ipa(word: str) -> str:
    """Get IPA from espeak-ng for a single word."""
    for cmd in ["espeak-ng", "espeak"]:
        try:
            out = subprocess.check_output(
                [cmd, "-q", "--ipa", "-v", "en-us", word],
                stderr=subprocess.DEVNULL,
                timeout=10,
            ).decode("utf-8", errors="replace")
            # eSpeak may return multiple lines; take first non-empty
            for line in out.strip().splitlines():
                line = line.strip()
                if line:
                    return line
            return ""
        except FileNotFoundError:
            continue
        except (subprocess.TimeoutExpired, subprocess.CalledProcessError):
            return ""
    return ""


def process_word(entry: tuple) -> tuple:
    """Process a single (word, pattern_str) pair. Returns (word, pattern_str, expected, got, ipa)."""
    word, pattern_str = entry
    pattern = pattern_str.split()
    expected = len(pattern)

    ipa = espeak_ipa(word)
    stripped = strip_stress(ipa)
    got = count_nuclei(stripped)

    return (word, pattern_str, expected, got, ipa)


def main():
    parser = argparse.ArgumentParser(description="Verify stress dictionary against eSpeak")
    parser.add_argument("--jobs", type=int, default=8,
                        help="Number of parallel workers (default: 8)")
    args = parser.parse_args()

    repo = Path(__file__).resolve().parent.parent
    dict_path = repo / "packs" / "dict" / "en-us-stress.tsv"
    audit_path = repo / "tools" / "stress_audit.tsv"
    verified_path = repo / "packs" / "dict" / "en-us-stress-verified.tsv"

    if not dict_path.exists():
        print(f"Error: {dict_path} not found", file=sys.stderr)
        sys.exit(1)

    # Load dictionary
    entries = []
    with open(dict_path, encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\r\n")
            if not line:
                continue
            tab = line.find("\t")
            if tab < 0:
                continue
            word = line[:tab]
            pattern_str = line[tab + 1:]
            if not word or not pattern_str:
                continue
            entries.append((word, pattern_str))

    total = len(entries)
    print(f"Loaded {total} entries from {dict_path.name}")
    print(f"Running eSpeak verification with {args.jobs} workers...")

    results = []
    done = 0

    with ProcessPoolExecutor(max_workers=args.jobs) as pool:
        futures = {pool.submit(process_word, e): e for e in entries}
        for future in as_completed(futures):
            results.append(future.result())
            done += 1
            if done % 5000 == 0 or done == total:
                print(f"  {done}/{total} ({100 * done // total}%)")

    # Sort by original order (word)
    results.sort(key=lambda r: r[0].lower())

    pass_count = 0
    fail_count = 0

    with open(audit_path, "w", encoding="utf-8") as audit, \
         open(verified_path, "w", encoding="utf-8") as verified:

        for word, pattern_str, expected, got, ipa in results:
            status = "PASS" if expected == got else "FAIL"
            audit.write(f"{word}\t{expected}\t{got}\t{status}\t{ipa}\n")

            if status == "PASS":
                pass_count += 1
                verified.write(f"{word}\t{pattern_str}\n")
            else:
                fail_count += 1

    print(f"\nResults:")
    print(f"  PASS: {pass_count} ({100 * pass_count // total}%)")
    print(f"  FAIL: {fail_count} ({100 * fail_count // total}%)")
    print(f"\nAudit log:     {audit_path}")
    print(f"Verified dict: {verified_path}")


if __name__ == "__main__":
    main()

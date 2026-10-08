# TGSpeechBox Dictionary Editing Guide

This document covers the dictionary system — how pronunciation overrides, stress dictionaries, compound splitting, character dictionaries, and user dictionaries work across all platforms.

For phoneme tuning, language pack settings, normalization rules, and voice profiles, see **[Tuning.md](Tuning.md)**. Dictionaries operate above the phoneme/pack level: they intercept text *before* it reaches eSpeak or the IPA pipeline and redirect pronunciation at the word level.

## Overview

TGSpeechBox supports five dictionary types, each stored as a TSV file in `packs/dict/`:

| Type | Filename | Purpose |
|------|----------|---------|
| **Pronunciation** | `{lang}-dict.tsv` | Word-level pronunciation overrides (respelling or IPA) |
| **User** | `{lang}-user.tsv` | User-created pronunciation overrides (same format as pronunciation) |
| **Character** | `{lang}-letters.tsv` | Single-character spoken descriptions for spelling mode |
| **Stress** | `{lang}-stress.tsv` | Lexical stress patterns for vowel prominence |
| **Compound** | `{lang}-compounds.tsv` | Compound word splitting for correct stress |

## Pronunciation Dictionary

The main dictionary for fixing mispronounced words. Each entry can use **respelling** (let eSpeak re-phonemize a simpler spelling) or **IPA injection** (bypass eSpeak entirely with hand-crafted phonemes).

### File format

Five tab-separated columns:

```
# from_text	to_text	from_ipa	to_ipa	category
knievel	kneevel		k ᵊ n ˈiː v ə l	proper
schitt's	shitts			proper
secondary	secondairy			adjective
```

- **from_text** — the word to match (case-insensitive by default)
- **to_text** — respelling that eSpeak will pronounce correctly
- **from_ipa** — (optional) expected IPA from eSpeak, for validation
- **to_ipa** — (optional) IPA override sent directly to the speech pipeline, bypassing eSpeak entirely. Space-delimited phoneme keys matching `packs/phonemes.yaml`
- **category** — (optional) organizational tag: `proper`, `noun`, `verb`, `adjective`, `fictional`, `technology`, `loanword`, `place`, `astrology`, `portmanteau`, etc.

### Respelling vs IPA injection

**Respelling** (to_text only): The simplest approach. Write a phonetic spelling that eSpeak already handles correctly. Example: `libra → leebra` fixes eSpeak saying "LYE-bra".

**IPA injection** (to_ipa): More powerful — the IPA string goes directly into the speech pipeline without eSpeak re-phonemizing it. Use space-delimited phoneme keys that match entries in `packs/phonemes.yaml`. Example: `knievel → k ᵊ n ˈiː v ə l`.

When both to_text and to_ipa are set, to_ipa takes priority for synthesis. to_text is still used for display/preview purposes.

### IPA key format

IPA keys in to_ipa must be space-delimited and match phoneme keys in `packs/phonemes.yaml`. Spaces are converted to U+001F (unit separator) internally as phoneme key boundaries. Examples:

- `k ᵊ n ˈiː v ə l` — each phoneme separated by a space
- `h w ˈɑː w eɪ`, stress marks stay attached to the following vowel
- Language-specific keys like `a_es` (Spanish /a/) or `e_hu` (Hungarian /eː/) are valid

### How phoneme key lookup works

The IPA tokenizer is greedy — it tries to match the longest known phoneme key first, then falls back to shorter sequences. When a space-delimited key from to_ipa is found in `phonemes.yaml`, it becomes a discrete phoneme token with all the formant, duration, and amplitude parameters defined for that key.

When a key is *not* found in the phoneme table, the tokenizer treats the characters as a continuation of the IPA stream and attempts to match them as standard IPA symbols. This means you can freely mix pack-specific phoneme keys with standard IPA in the same to_ipa field. For example:

- `k ᵊ n ˈiː v ə l` — `k`, `ᵊ`, `n`, `ˈiː`, `v`, `ə`, `l` are all standard IPA, matched individually
- `b ɐ n ˈæ n a_es` — `a_es` is a language-specific key from `phonemes.yaml`; the rest are standard IPA

This makes the system flexible — you can inject any phoneme type (stops, fricatives, vowels, affricates, nasals) by referencing its key. If a phoneme has been customized in the pack with specific formant values, burst parameters, or duration scaling, the injected version inherits all of that. You're not limited to what eSpeak would produce — the full phoneme inventory of the loaded language pack is available for injection.

## User Dictionary

The user dictionary (`{lang}-user.tsv`) has the same format as the pronunciation dictionary but is reserved for user-created entries. It is loaded *after* the main dictionary, so user entries override main entries on conflict.

### Platform behavior

| Platform | User entry storage | User TSV role |
|----------|-------------------|---------------|
| **Android** | SharedPreferences (JSON) | Blank — entries applied in-memory via JSON |
| **iOS** | UserDefaults (JSON) | Blank — entries applied in-memory via JSON |
| **NVDA** | `{lang}-user.tsv` in addon packs | Primary storage — edit directly or use Import |
| **SAPI** | `{lang}-user.tsv` in install packs | Primary storage |
| **Phoneme Editor** | `{lang}-user.tsv` in packs | Direct editing via "User" dict type |

### Export / Import

- **Android & iOS**: "Export changed" in the dictionary editor's menu saves user entries as `{lang}-user.tsv` (full 5-column TSV). Import reads TSV and applies entries as JSON overrides.
- **NVDA**: "Export User Dictionary..." and "Import User Dictionary..." buttons in the TGSpeechBox settings panel. Uses standard Windows file dialogs.
- **Cross-platform**: A user dictionary exported from Android can be imported on NVDA and vice versa — the TSV format is the same everywhere.

### Surviving addon updates

NVDA addon updates replace the entire addon directory, which deletes any in-place modifications to `{lang}-user.tsv`. To preserve your work:

1. Before updating: use "Export User Dictionary..." to save a copy
2. Update the addon
3. Use "Import User Dictionary..." to restore your entries

The "Save Voice Profile Sliders to YAML As..." and "Import Voice Profile Sliders..." buttons serve the same purpose for voice profile customizations in `phonemes.yaml`.

## Stress Dictionary

Lexical stress patterns for words where eSpeak's default stress is wrong or missing. Used by the text parser to correct vowel prominence.

### File format

Two tab-separated columns:

```
photograph	1 0 0
photography	0 1 0 0
```

- **word** — lowercase lookup key
- **stress pattern** — space-separated digits: `0` = unstressed, `1` = primary stress, `2` = secondary stress. One digit per vowel nucleus.

The en-us stress dictionary (`en-us-stress.tsv`) contains ~109,000 entries derived from the CMU Pronouncing Dictionary (Copyright (C) 1993-2015 Carnegie Mellon University, BSD 2-clause license; the full text is in `packs/dict/LICENSE-CMUdict.txt`, which ships with the packs).

## Compound Dictionary

Compound word splitting for languages where eSpeak merges compound words and gets the stress wrong.

### File format

Two tab-separated columns:

```
notebook	note book
headphones	head phones
```

- **word** — lowercase compound word
- **parts** — space-separated component words

The text parser splits compounds before eSpeak phonemization, then merges the IPA back together with correct stress boundaries. The en compounds dictionary (`en-compounds.tsv`) contains ~3,700 entries.

## Character / Letter Dictionary

Maps individual characters to spoken descriptions or replacement words. Used when screen readers read text character-by-character (spelling mode, cursor navigation, etc.). The character dictionary gives you control over how each character is announced — useful for symbols, accented characters, or language-specific letter names.

### File format

Two tab-separated columns in `{lang}-letters.tsv`:

```
@	at sign
#	hash
€	euro
ñ	enye
ó	o acentuada
```

- **key** — a single character (or short string) to match
- **description** — the spoken replacement. This is what the synth will say when the character is read individually.

### Use cases

- **Symbol names**: Map `@` to "at sign", `#` to "hash", `€` to "euro" so they're spoken clearly in spelling mode
- **Accented character names**: In Spanish, map `ó` to "o acentuada" so users hear the accent when navigating letter-by-letter
- **Language-specific letter names**: Override default English letter names with language-appropriate names (e.g., Hungarian `gy` → "gyé")
- **Emoji/special characters**: Provide descriptions for characters that eSpeak might skip or mispronounce

### Platform support

The character dictionary type appears in the dictionary editor on all platforms (Android, iOS, Phoneme Editor). It supports the same add/edit/preview/export workflow as pronunciation entries — you can preview how a character will sound before saving.

The frontend loads character dictionaries with the same override-dir-first pattern as other dict types, and falls back to the base language tag (e.g., `es-letters.tsv` for `es-mx`).

## Adding entries

### On mobile (Android / iOS)

1. Open the Dictionary editor (tab in the app)
2. Select "Pronunciation" or "Pronunciation (user)" from the type dropdown
3. Tap the + button to add a new entry
4. Fill in "From text" (the word to fix) and either "To text" (respelling) or "To IPA" (direct phoneme override)
5. Use "Fill IPA from eSpeak" to see what eSpeak currently produces
6. Use "Insert phoneme" to browse and preview available phonemes before inserting
7. Use "Preview" to hear the result before saving

### On NVDA

User entries can be added via the Phoneme Editor's "User" dictionary type, or by directly editing `{lang}-user.tsv` in the addon's `packs/dict/` directory.

### On the Phoneme Editor (Win32)

1. Open the Dictionary editor (Editor menu → Dictionary Editor)
2. Select "User" from the type dropdown
3. Add, edit, or remove entries
4. Save — changes write directly to `{lang}-user.tsv`

## Community dictionaries

Community members can maintain their own dictionary TSV files (e.g., [fastfinge/tgict](https://github.com/fastfinge/tgict)). Useful entries are periodically merged into the main dictionary.

Post-3.0, we plan to support subscription-based dictionary loading — specify a URL to a TSV and refresh on demand. For now, community contributions are merged manually.

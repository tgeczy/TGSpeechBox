# NV Speech Player – Language Pack & Phoneme Tuning Guide

This document covers language pack configuration, phoneme tuning, voice profiles, normalization rules, and frontend processing passes.

## phonemes.yaml
`packs/phonemes.yaml` defines how each phoneme maps to Klatt-style frame parameters. Keys are IPA symbols or internal symbols (we use some private keys like `ᴇ`, `ᴒ`, etc. for language-specific tuning).

Example (shortened):
```yaml
phonemes:
  "0":
    _isVowel: true
    _isVoiced: true
    voiceAmplitude: 0.9
    cf1: 500
    cf2: 1400
    cf3: 2300
    cb1: 110
    cb2: 45
    cb3: 82.5
```

## Voice profiles (optional)
Voice profiles let you change the *character* of the voice (female, child, deep, etc.) without keeping separate phoneme tables. The base `phonemes:` map stays the same; a profile is just an overlay that scales or overrides some parameters.

**Where they live:** voice profiles are defined in `packs/phonemes.yaml`, not in the per-language YAML files.

**Basic shape:**
```yaml
phonemes:
  "a":
    _isVowel: true
    cf1: 700
    cf2: 1200

voiceProfiles:
  female:
    classScales:
      vowel:
        cf_mul: [1.12, 1.16, 1.18, 1.08, 1.04, 1.02]
        pf_mul: [1.12, 1.16, 1.18, 1.08, 1.04, 1.02]
        voicePitch_mul: 1.4        # raise pitch ~40%
        endVoicePitch_mul: 1.4
        glottalOpenQuotient_mul: 1.08  # slightly breathier
      consonant:
        voicePitch_mul: 1.4
        endVoicePitch_mul: 1.4
      unvoicedFricative:
        fricationAmplitude_mul: 0.95
        aspirationAmplitude_mul: 0.95
    phonemeOverrides:
      "i":
        cf2: 2500  # absolute value
        pf2: 2500
```

### Voice profile integration

The NVDA driver uses these parameters in voice profiles defined in `phonemes.yaml`. Under the `voicingTone` key, profiles can specify any of these parameters to create distinct voice characters:

```yaml
voiceProfiles:
  Beth:
    voicingTone:
      voicedTiltDbPerOct: -6.0
      highShelfGainDb: 2.0
      highShelfFcHz: 2800.0
      noiseGlottalModDepth: 0.3
```

### Class names you can use
These match the flags already present in phoneme entries (like `_isVowel`, `_isVoiced`, etc.):
- `vowel`
- `consonant` (fallback for any non-vowel)
- `voicedConsonant`
- `voicedFricative`
- `unvoicedFricative`
- `nasal`
- `liquid`
- `stop`
- `affricate`
- `semivowel`

### Supported scale fields
Class scales are *multipliers*:

**Formant multipliers (arrays or scalar shorthand):**
- `cf_mul`, `pf_mul` (formant frequency multipliers for F1..F6)
- `cb_mul`, `pb_mul` (bandwidth multipliers for B1..B6)

**Pitch multipliers:**
- `voicePitch_mul` (fundamental frequency)
- `endVoicePitch_mul` (end pitch for contours)

**Vibrato multipliers:**
- `vibratoPitchOffset_mul` (vibrato depth)
- `vibratoSpeed_mul` (vibrato rate)

**Voice quality multipliers:**
- `voiceTurbulenceAmplitude_mul` (breathiness)
- `glottalOpenQuotient_mul` (glottal open quotient)

**Amplitude multipliers:**
- `voiceAmplitude_mul`
- `aspirationAmplitude_mul`
- `fricationAmplitude_mul`
- `preFormantGain_mul`
- `outputGain_mul`

**Scalar shorthand:** for `cf_mul` / `pf_mul` / `cb_mul` / `pb_mul`, you can provide a single number and it will be replicated across all 6 formants. Example: `cf_mul: 1.12` means `cf_mul: [1.12, 1.12, 1.12, 1.12, 1.12, 1.12]`.

### Per-phoneme overrides (`phonemeOverrides`)

Use `phonemeOverrides` when your profile is *mostly* right, but **one specific phoneme** still sounds off.

**What it does**
- Values in `phonemeOverrides` are **absolute assignments** for that phoneme (not multipliers).
- Overrides are applied **after** `classScales`.
- If an override sets a field, it **wins for that field only**. Other fields still come from the base phoneme + any class scaling.

**Effective order (per field)**
1. Start with the phoneme's base value from `phonemes:`
2. Apply any matching `classScales` multipliers
3. Apply `phonemeOverrides` (only for the fields you specify)

**Tip**  
Override the **smallest number of fields** you can (often just `cf2`, or `cf1 + cf2` on one vowel).  
If you're overriding lots of phonemes, it usually means your `classScales` need tuning.

**Example**
```yaml
voiceProfiles:
  female:
    classScales:
      vowel:
        cf_mul: [1.12, 1.16, 1.18, 1.08, 1.04, 1.02]
    phonemeOverrides:
      "i":
        cf2: 2550
      "s":
        fricationAmplitude: 40
```

### How to enable a profile
Preferred: select a profile at runtime via the frontend API:
```c
// Enable
nvspFrontend_setVoiceProfile(handle, "female");

// Disable (back to base phonemes)
nvspFrontend_setVoiceProfile(handle, "");

// Read current profile ("" if none)
const char* profile = nvspFrontend_getVoiceProfile(handle);
```

### Debugging: pack load warnings
If a profile isn't taking effect (typo, YAML error, etc.), read the pack warnings:
```c
const char* warnings = nvspFrontend_getPackWarnings(handle);
if (warnings && warnings[0] != '\0') {
    // show or log warnings
}
```

## Language YAML example (en.yaml)
Example (shortened):
```yaml
settings:
  stopClosureMode: always
  postStopAspirationEnabled: true

normalization:
  classes:
    BATH_FOLLOW: ["s", "f", "θ"]
  replacements:
    - from: "a"
      to: "æ"
    - from: "æ"
      to: "ɑː"
      when:
        beforeClass: BATH_FOLLOW
```

## Language pack settings reference
Below is a reference for language pack `settings` values used by the frontend.

### Stress and timing
- `primaryStressDiv` (number, default `1.4`): Slows down the entire syllable carrying primary stress. Higher = more slowdown. **Bypassed when `prominence.enabled` is true** — the prominence pass handles stress realization on vowels instead.
- `secondaryStressDiv` (number, default `1.1`): Slows down secondary stress syllables. **Bypassed when `prominence.enabled` is true.**

### Stop closure insertion
These settings control the short "silence gap" inserted before stops/affricates (helps clarity, especially for consonant clusters).
- `stopClosureMode` (string, default `"vowel-and-cluster"`): One of:
  - `"always"`: insert a closure gap before every stop/affricate (clearest, can sound clicky)
  - `"after-vowel"`: only after vowels
  - `"vowel-and-cluster"`: after vowels, and also in some clusters (balanced)
  - `"none"`: never insert closure gaps
- `stopClosureClusterGapsEnabled` (bool, default `true`): Enables the cluster part of `"vowel-and-cluster"`.
- `stopClosureAfterNasalsEnabled` (bool, default `false`): If true, allows closure gaps before stops even after nasals. Helpful when nasal+stop clusters swallow the stop at higher rates.

### Stop closure gap timing (ms at speed=1.0)
- `stopClosureVowelGapMs` (number, default `41.0`)
- `stopClosureVowelFadeMs` (number, default `10.0`)
  - Duration/fade of closure gaps inserted after vowels.
- `stopClosureClusterGapMs` (number, default `22.0`)
- `stopClosureClusterFadeMs` (number, default `4.0`)
  - Duration/fade of closure gaps inserted in clusters.
- `stopClosureWordBoundaryClusterGapMs` (number, default `0.0`)
- `stopClosureWordBoundaryClusterFadeMs` (number, default `0.0`)
  - If set (>0), overrides the cluster gap timing only when the stop/affricate is at a word boundary (word start).

### Segment boundary timing (between chunks)
These settings help when callers stitch speech from multiple chunks (common in NVDA UI speech: label / role / value).
- `segmentBoundaryGapMs` (number, default `0.0`)
- `segmentBoundaryFadeMs` (number, default `0.0`)
  - If non-zero, inserts a short silence frame between consecutive frontend queue calls on the same handle.
- `segmentBoundarySkipVowelToVowel` (bool, default `true`)
  - If true, skips the segment-boundary silence when a chunk ends with a vowel/semivowel and the next chunk begins with a vowel/semivowel (to avoid audible gaps across diphthongs).
- `segmentBoundarySkipVowelToLiquid` (bool, default `false`)
  - If true, also skips the segment-boundary silence when a chunk ends with a vowel/semivowel and the next chunk begins with a liquid-like consonant (liquids/taps/trills). This can help reduce audible seams in vowel+R transitions across chunks if it's noticeable in your language.

### Automatic diphthong handling
These settings optionally add tie bars for vowel+vowel sequences that should behave like a diphthong.
- `autoTieDiphthongs` (bool, default `false`): If true, the frontend can mark eligible vowel+vowel pairs as tied (diphthongs) even when the IPA lacks a tie bar.
- `autoDiphthongOffglideToSemivowel` (bool, default `false`): If true (and `autoTieDiphthongs` is enabled), convert the diphthong offglide to a semivowel (`i/ɪ → j`, `u/ʊ → w`) when those phonemes exist.

### Semivowel offglide shortening
If your pack represents diphthongs using a vowel+semivowel sequence (for example `eɪ → ej`), you may hear a tiny "syllable break" when that semivowel is followed by a vowel or a liquid-like consonant inside the same word (e.g. "player", "later").

- `semivowelOffglideScale` (number, default `1.0`)
  - If set to anything other than `1.0`, the engine multiplies the duration and fade of semivowel tokens in the pattern `vowel + semivowel + (vowel | liquid | tap | trill)`.
  - Values below `1.0` make those semivowels shorter and more "glide-like" without affecting word-final diphthongs like "day".

### Liquid dynamics (optional)

These settings add internal movement to liquids and glides, so they don't sound "stuck" on one static shape (especially noticeable on **/l/**, **/r/**, and **/w/**).

You can write them as a nested block (preferred):

```yaml
settings:
  liquidDynamics:
    enabled: true

    # /l/ clarity
    lateralOnglide:
      f1Delta: -50
      f2Delta: 200
      durationPct: 0.30

    # American-style rhotic quality
    rhoticF3Dip:
      enabled: true
      f3Minimum: 1600
      dipDurationPct: 0.50

    # /w/ should start labial and move toward the vowel
    labialGlideTransition:
      enabled: true
      startF1: 300
      startF2: 700
      transitionPct: 0.60
```

Flat-key equivalents are also supported:

- `liquidDynamicsEnabled`
- `liquidDynamicsLateralOnglideF1Delta`
- `liquidDynamicsLateralOnglideF2Delta`
- `liquidDynamicsLateralOnglideDurationPct`
- `liquidDynamicsRhoticF3DipEnabled`
- `liquidDynamicsRhoticF3Minimum`
- `liquidDynamicsRhoticF3DipDurationPct`
- `liquidDynamicsLabialGlideTransitionEnabled`
- `liquidDynamicsLabialGlideStartF1`
- `liquidDynamicsLabialGlideStartF2`
- `liquidDynamicsLabialGlideTransitionPct`

Implementation note: the frontend splits the token into a short "onglide" segment + a steady segment and uses a small internal crossfade.

### Intra-word stressed vowel hiatus break (spelling aid)
These settings can insert a tiny silence between two adjacent vowels when the *second* vowel is explicitly stressed. This mostly exists to help spelled-out acronyms (initialisms) where two letter names meet with no consonant in between.
- `stressedVowelHiatusGapMs` (number, default `0.0`): Gap duration in milliseconds at speed=1.0. The engine divides it by current speed.
- `stressedVowelHiatusFadeMs` (number, default `0.0`): Fade duration (crossfade) for that gap frame.

### Spelling diphthong handling
This is a more speech-like alternative to inserting a pause. It changes how some letter-name diphthongs are rendered when the word looks like a spelled-out acronym.
- `spellingDiphthongMode` (string, default "none"): One of:
  - `none`: do nothing
  - `monophthong`: in acronym-like words, treat the English letter name **A** (/eɪ/, often pack-normalized to /ej/) as a long monophthong by dropping the offglide and marking the nucleus lengthened. This reduces the unwanted "y"-glide in cases like "NVDA".

### Post-stop aspiration insertion (English-style)
- `postStopAspirationEnabled` (bool, default `false`): Inserts a short aspiration phoneme after unvoiced stops in specific contexts.
- `postStopAspirationPhoneme` (string/IPA key, default `"h"`): Which phoneme key to insert for aspiration.

### Allophone rules (YAML-driven rule engine)

This pass replaces the older hardcoded `positionalAllophones` system with a generic, data-driven rule engine. Language pack authors can define allophone rules entirely in YAML without C++ changes.

Rules support five action types: **replace** (swap phoneme), **scale** (multiply duration/fields), **shift** (Hz deltas or blend-toward-target), **insert-before**, and **insert-after**. Replace and insert rules are first-match-wins (exclusive); scale and shift rules are cumulative (all matching rules apply).

```yaml
settings:
  allophoneRules:
    enabled: true
    rules:
      # Intervocalic flapping: /t,d/ → [ɾ] before unstressed vowel
      - name: american_flapping
        phonemes: [t, d]
        position: intervocalic
        stress: next-unstressed
        action: replace
        replaceTo: "ɾ"
        replaceDurationMs: 14.0
        replaceRemovesClosure: true
        replaceRemovesAspiration: true

      # /l/ darkness: post-vocalic /l/ blends F2 toward 900 Hz
      - name: dark_l_post_vocalic
        flags: [liquid]
        position: post-vocalic
        action: shift
        fieldShifts:
          - field: cf2
            targetHz: 900
            blend: 0.8
          - field: pf2
            targetHz: 900
            blend: 0.8

      # Unreleased word-final voiceless stops
      - name: unreleased_word_final
        flags: [stop]
        notFlags: [voiced]
        position: word-final
        action: scale
        durationScale: 0.85
        fieldScales:
          fricationAmplitude: 0.4
          aspirationAmplitude: 0.3

      # Glottal reinforcement: insert [ʔ] before word-final voiceless stops
      - name: glottal_reinforcement
        flags: [stop]
        notFlags: [voiced]
        position: word-final
        action: insert-before
        insertPhoneme: "ʔ"
        insertDurationMs: 18.0
```

#### Rule match conditions

| Field | Type | Description |
|-------|------|-------------|
| `phonemes` | list | IPA keys that match (e.g. `[t, d]`). Empty = match any. |
| `flags` | list | Required phoneme flags (ALL must match): `stop`, `vowel`, `voiced`, `nasal`, `liquid`, `semivowel`, `affricate`, `tap`, `trill` |
| `notFlags` | list | Excluded phoneme flags (rule won't match if ANY of these are set) |
| `tokenType` | string | Token type filter: `"phoneme"` (default), `"aspiration"`, `"closure"` |
| `position` | string | Positional filter: `"word-initial"`, `"word-final"`, `"intervocalic"`, `"post-vocalic"`, `"pre-vocalic"`, `"syllabic"` |
| `stress` | string | Stress filter: `"stressed"`, `"unstressed"`, `"next-unstressed"`, `"prev-stressed"` |
| `after` | list | Previous phoneme must be one of these IPA keys |
| `before` | list | Next phoneme must be one of these IPA keys |
| `afterFlags` | list | Previous phoneme must have ALL listed flags |
| `notAfterFlags` | list | Exclude if previous phoneme has ANY listed flag |
| `beforeFlags` | list | Next phoneme must have ALL listed flags |
| `notBeforeFlags` | list | Exclude if next phoneme has ANY listed flag |

#### Action types

| Action | Description |
|--------|-------------|
| `replace` | Swap phoneme def, optionally remove or scale adjacent closure/aspiration tokens. Fields: `replaceTo`, `replaceDurationMs`, `replaceRemovesClosure`, `replaceRemovesAspiration`, `replaceClosureScale`, `replaceAspirationScale` |
| `scale` | Multiply duration, fade, and/or specific fields. Fields: `durationScale`, `fadeScale`, `fieldScales` (map of field name to multiplier) |
| `shift` | Apply Hz deltas or blend-toward-target. Fields: `fieldShifts` (list of `{field, deltaHz, targetHz, blend}`) |
| `insert-before` | Insert a new token before this one. Fields: `insertPhoneme`, `insertDurationMs`, `insertFadeMs`, `insertContexts` |
| `insert-after` | Insert a new token after this one. Fields: `insertPhoneme`, `insertDurationMs`, `insertFadeMs`, `insertContexts` |

#### Detailed action parameters

**replace** additional fields:
- `replaceClosureScale` (number, default `0.0`): If > 0, scale the adjacent closure token's duration instead of removing it. Useful when you want to keep a shortened closure (e.g. `0.5` halves the closure).
- `replaceAspirationScale` (number, default `0.0`): If > 0, scale the adjacent aspiration token's duration and inject breathiness into the main token instead of removing aspiration entirely.

**scale** additional fields:
- `fadeScale` (number, default `1.0`): Multiplier for the token's crossfade time. Values < 1.0 make transitions crisper; > 1.0 makes them smoother.

**insert-before / insert-after** additional fields:
- `insertFadeMs` (number, default `3.0`): Crossfade duration for the inserted token (ms at speed=1.0).
- `insertContexts` (list of strings): If non-empty, the insertion only happens when at least one context matches. Valid contexts:
  - `"V_#"` — previous phoneme is a vowel AND current token is word-final
  - `"#_#"` — current token is word-final

#### Position and stress details

- **`syllabic`**: Matches when the phoneme has no adjacent vowel on either side (i.e. it IS the syllable nucleus). Useful for syllabic consonants like dark /l/ in "bottle".
- **`prev-stressed`**: Matches when the preceding phoneme carries stress (stress > 0).

#### Class-based neighbor matching

The `afterFlags`/`beforeFlags`/`notAfterFlags`/`notBeforeFlags` fields let you match based on the *class* of adjacent phonemes rather than specific IPA keys. This is more maintainable than listing every possible phoneme.

```yaml
# Example: only darken /l/ when preceded by a back vowel
- name: dark_l_after_back_vowel
  phonemes: [l]
  afterFlags: [vowel]        # prev must be a vowel
  position: post-vocalic
  action: shift
  fieldShifts:
    - field: cf2
      targetHz: 900
      blend: 0.8
```

The flag names used in these fields are the same as `flags`/`notFlags`: `stop`, `vowel`, `voiced`, `nasal`, `liquid`, `semivowel`, `affricate`, `tap`, `trill`.

#### Important notes

- **Neighbor matching** uses `prevPhoneme`/`nextPhoneme` which skip closure, aspiration, and gap tokens — so intervocalic checks work correctly even when stops have inserted closure/aspiration tokens between them.
- **`isWordFinalPhoneme`** looks past trailing aspiration/closure tokens, so a stop followed by its aspiration token is still considered word-final.
- **eSpeak stress marks** land on syllable-initial consonants, not vowels. The `next-unstressed` check verifies both that the current token has `stress <= 0` AND that the next vowel is unstressed, preventing false matches on stressed consonants.
- **`intervocalic` excludes word-initial**: A stop at the start of a word is never treated as intervocalic, even if the previous word ended with a vowel. This prevents flapping from incorrectly applying to word-initial stops like the /d/ in "do".
- **Backward compatibility**: the old `positionalAllophones: enabled: true` syntax is still parsed and sets `allophoneRulesEnabled = true` (no rules are loaded from the old format).

### Length mark handling (ː)
- `lengthenedScale` (number, default `1.05`): Duration multiplier when a phoneme is lengthened with ː.
- `lengthenedScaleHu` (number, default `1.3`): Hungarian-specific length scaling (used by legacy behavior).
- `applyLengthenedScaleToVowelsOnly` (bool, default `true`): If true, ː only lengthens vowels.
- `lengthenedVowelFinalCodaScale` (number, default `1.0`): Additional multiplier for lengthened vowels (ː) when they occur in a word-final closed syllable (vowel + final consonant cluster), e.g. "rules" /ruːlz/. This is intentionally conservative: it does not apply when there are later vowels in the same word (e.g. "ruler" /ruːlə/).

### Length contrast and gemination (optional)

If a language has phonemic length (short vs long vowels) or geminate consonants, this pass helps keep those cues stable even at higher speech rates.

Nested block form (preferred):

```yaml
settings:
  lengthContrast:
    enabled: true

    # Vowel constraints (ms at speed=1)
    shortVowelCeiling: 80
    longVowelFloor: 120

    # Gemination cues
    geminateClosureScale: 1.8
    geminateReleaseScale: 0.9
    preGeminateVowelScale: 0.85
```

Flat-key equivalents are also supported:

- `lengthContrastEnabled`
- `lengthContrastShortVowelCeilingMs`
- `lengthContrastLongVowelFloorMs`
- `lengthContrastGeminateClosureScale`
- `lengthContrastGeminateReleaseScale`
- `lengthContrastPreGeminateVowelScale`

How it behaves:

- Short vowels are capped, long vowels are floored.
- For doubled consonants, the engine mainly lengthens the **closure gap** (the inserted pre-stop gap between the consonants), which is where gemination tends to "live" perceptually.
- The numbers are treated as **ms at speed=1**, and the effective limits are divided by the current speed so things don't explode at 3× speed.

### Language-specific duration tweaks
These are "compat switches" for behavior that existed in the legacy Python pipeline.
- `huShortAVowelEnabled` (bool, default `true`)
- `huShortAVowelKey` (string/IPA key, default `"ᴒ"`)
- `huShortAVowelScale` (number, default `0.85`): Scales Hungarian short "a" timing (using the phoneme key you map for it).
- `englishLongUShortenEnabled` (bool, default `true`)
- `englishLongUKey` (string/IPA key, default `"u"`)
- `englishLongUWordFinalScale` (number, default `0.80`): Shortens English long /uː/ in word-final position.

### Default frame values (applied unless phoneme sets them)
- `defaultPreFormantGain` (number, default `1.0`)
- `defaultOutputGain` (number, default `1.5`)
- `defaultVibratoPitchOffset` (number, default `0.0`)
- `defaultVibratoSpeed` (number, default `0.0`)
- `defaultVoiceTurbulenceAmplitude` (number, default `0.0`)
- `defaultGlottalOpenQuotient` (number, default `0.0`)

### Phoneme timing defaults (reference)
These values control the base duration of each phoneme type (ms at speed=1.0). They are currently hardcoded in the engine and not configurable via YAML, but are listed here for reference:

| Phoneme type | Default (ms) | Notes |
|--------------|-------------|-------|
| Vowel | 60 | `defaultVowelDurationMs` |
| Voiceless fricative | 45 | `voicelessFricativeDurationMs` — /s/, /f/, /ʃ/, /x/ |
| Trill | 40 | `trillFallbackDurationMs` (if `trillModulationMs` unset) |
| Vowel before nasal | 40 | `vowelBeforeNasalDurationMs` — unstressed V before N |
| Tied vowel (diphthong start) | 40 | `tiedVowelDurationMs` |
| Voiced consonant | 30 | `voicedConsonantDurationMs` — default for all voiced non-vowels |
| Vowel before liquid | 30 | `vowelBeforeLiquidDurationMs` — unstressed V before L/R |
| Affricate | 24 | `affricateDurationMs` — /t͡ʃ/, /d͡ʒ/ |
| Post-stop aspiration | 20 | `postStopAspirationDurationMs` |
| Tied-from vowel | 20 | `tiedFromVowelDurationMs` (diphthong end) |
| Tap | 14 | `tapDurationMs` — /ɾ/ |
| Default crossfade | 10 | `defaultFadeMs` |
| Stop burst | 6 | `stopDurationMs` — /p/, /t/, /k/, /b/, /d/, /g/ |

### Normalization cleanup
- `stripAllophoneDigits` (bool, default `true`): Removes eSpeak allophone digits from IPA streams (disable for tonal digit languages if needed).
- `stripHyphen` (bool, default `true`): Removes hyphens in IPA streams.

### Pitch model selection

The frontend supports three pitch models, selected via `legacyPitchMode`:

- `legacyPitchMode: "espeak_style"` (default) — Table-based intonation model (same contours as `ipa.py`).
- `legacyPitchMode: "legacy"` — Older time-based pitch curves ported from the ee80f4d-era `ipa.py`.
- `legacyPitchMode: "fujisaki_style"` — Fujisaki pitch model with exponential declination and accent peaks. This provides Eloquence-like intonation with smooth phrase-level pitch fall, stressed syllable peaks, and clause-final pitch shaping.

Additional pitch settings:

- `legacyPitchInflectionScale` (number, default `0.58`): Multiplier for the caller-provided `inflection` (0..1) before applying pitch math. Historical NVSpeechPlayer used lower inflection values; this scaling prevents overly "excited" speech with modern configs.

#### Fujisaki pitch model settings

When `legacyPitchMode: "fujisaki_style"` is enabled, these settings control the intonation:

**Amplitude settings:**
- `fujisakiPhraseAmp` (number, default `0.24`): Phrase command amplitude in log-F0 domain. Controls the overall phrase arc.
- `fujisakiPrimaryAccentAmp` (number, default `0.24`): Primary stress accent amplitude. Creates pitch peaks on stressed syllables.
- `fujisakiSecondaryAccentAmp` (number, default `0.12`): Secondary stress accent amplitude.
- `fujisakiAccentMode` (string, default `"all"`): Which syllables get accents: `"all"`, `"first_only"`, or `"off"`.

**Timing settings (DSP filter lengths):**
- `fujisakiPhraseLen` (number, default `0`): Phrase filter length in samples. 0 = DSP default (~4250 @ 22050 Hz = 193ms).
- `fujisakiAccentLen` (number, default `0`): Accent filter attack time in samples. 0 = DSP default (~1024 @ 22050 Hz = 46ms).
- `fujisakiAccentDur` (number, default `0`): Accent pulse duration in samples. 0 = DSP default (~7500 @ 22050 Hz = 340ms).

**Declination settings (exponential pitch fall across utterance):**
- `fujisakiDeclinationRate` (number, default `0.0003`): Exponential decay steepness. Controls how fast pitch falls across the utterance. Higher = steeper fall. The exponential formula `basePitch * exp(-rate * timeMs)` naturally asymptotes — fast initial fall that gradually flattens — so long sentences decline smoothly without ever hitting a hard floor.
  - `0.0002` = gentle slope, stays lively longer
  - `0.0003` = natural conversational declination (default)
  - `0.0005` = steep fall for dramatic effect

**Deprecated declination settings** (kept for YAML backward compatibility, no longer used):
- `fujisakiDeclinationScale`, `fujisakiDeclinationMax`, `fujisakiDeclinationPostFloor`, `fujisakiPhraseDecay` — these were used by earlier linear and multi-phrase implementations. Existing YAML files referencing them will parse without warnings but the values have no effect.

**Clause-type prosody:**
The Fujisaki model automatically adjusts prosody based on punctuation. Each clause type gets distinct behavior through phrase amplitude scaling, declination rate, and direct pitch shaping on the final vowel:
- `.` (period): Full declarative fall; final vowel ends 15% lower for a definitive finish
- `?` (question): Higher base pitch (+18%), reduced declination, final vowel rises 25% with a strong accent hump
- `!` (exclamation): High start (+15%), steep declination (2.5x), punchy accents, final vowel snaps down hard
- `,` (comma): Slight raise (+4%), gentle declination, no final shaping (level = continuation)

This clause-final pitch shaping is essential for short utterances (single words, spelled letters) where exponential declination barely has time to create within-word pitch movement.

Example configuration for Eloquence-like prosody:
```yaml
settings:
  legacyPitchMode: "fujisaki_style"
  fujisakiPhraseAmp: 0.24
  fujisakiPrimaryAccentAmp: 0.24
  fujisakiSecondaryAccentAmp: 0.12
  fujisakiDeclinationRate: 0.0003
```

*Note: The Fujisaki pitch model implementation was developed with assistance from Rommix, whose extensive testing and feedback on timing parameters helped shape the final behavior.*

### Tonal language support
- `tonal` (bool, default `false`): Enables tone parsing / tone contours.
- `toneDigitsEnabled` (bool, default `true`): Allows digits 1–5 as tone markers.
- `toneContoursMode` (string, default `"absolute"`): How tone contour points are interpreted:
  - `"absolute"`: points are absolute 0–100 pitch-percent values
  - `"relative"`: points are offsets from the syllable baseline
- `toneContoursAbsolute` (bool, default `true`): Low-level switch used by the code; `toneContoursMode` is the recommended YAML knob.

### Other pack sections (not in settings)

## Normalization

The `normalization` section controls how raw IPA from eSpeak is transformed before phoneme lookup. This is where you handle dialect differences, allophonic variation, and phoneme mappings.

```yaml
normalization:
  aliases:
    "ɾ": "ɹ"  # map tap to approximant
  
  classes:
    VOWELS: ["a", "e", "i", "o", "u", "ə", "ɪ", "ʊ", "ɛ", "ɔ", "æ"]
    CONSONANTS: ["b", "d", "f", "g", "k", "l", "m", "n", "p", "s", "t", "v", "z", "ɹ"]
  
  preReplacements:
    - from: "oːɹ"
      to:   "oɹ"
      when:
        beforeClass: CONSONANTS
  
  replacements:
    - from: "ɔ"
      to:   "ᴐ"
```

### preReplacements vs replacements: Order Matters!

**This is critical for language pack authors to understand.**

The normalization pipeline runs in this order:
1. **`preReplacements`** — runs on raw IPA, before any other transforms
2. Internal processing (tie-bar handling, phoneme-specific transforms like ᵊ insertion)
3. **`replacements`** — runs after internal processing

**Why does this matter?** Some internal transforms insert characters (like the schwa-glide `ᵊ`) or modify tie bars (`͡`). If your rule depends on matching a specific pattern like `ɔːɹ`, it **must** run in `preReplacements` — otherwise internal transforms may have already changed the text to `ɔːᵊɹ` and your pattern won't match.

**Rule of thumb:**
- Use `preReplacements` for rules that must see the **original eSpeak IPA** before any modifications
- Use `replacements` for rules that should run **after** internal phoneme transforms

**Example:** FORCE vowel shortening in American English

The goal: shorten the vowel in "short", "sport", "report" (where ɹ is followed by a consonant) but keep it long in "door", "for", "score" (where ɹ is word-final).

```yaml
preReplacements:
  # Must be in preReplacements! By the time replacements runs,
  # internal transforms may have inserted ᵊ, changing ɔːɹ → ɔːᵊɹ
  - from: "ɔːɹ"
    to:   "ɔɹ"
    when:
      beforeClass: CONSONANTS
```

### Replacement conditions (`when`)

Each replacement can have an optional `when` block with these conditions:

| Condition | Description |
|-----------|-------------|
| `atWordStart: true` | Match only at the beginning of a word |
| `atWordEnd: true` | Match only at the end of a word |
| `beforeClass: NAME` | Match only if the **next** character is in the named class |
| `afterClass: NAME` | Match only if the **previous** character is in the named class |
| `notBeforeClass: NAME` | Match only if the **next** character is **NOT** in the named class (or at end of string) |
| `notAfterClass: NAME` | Match only if the **previous** character is **NOT** in the named class (or at start of string) |

**Positive vs Negative conditions:**
- `beforeClass` / `afterClass` — rule applies **only if** the adjacent character is in the class
- `notBeforeClass` / `notAfterClass` — rule applies **only if** the adjacent character is **not** in the class (or there is no adjacent character)

**Example: Lengthening open syllables only**

```yaml
classes:
  CONSONANTS: ["b", "d", "f", "g", "k", "l", "m", "n", "p", "s", "t", "ɹ"]

replacements:
  # Add length mark to FORCE vowel, but NOT when followed by a consonant
  - from: "ɔːɹ"
    to:   "ɔːːɹ"
    when:
      notBeforeClass: CONSONANTS  # matches "door" (word-final) but not "short" (followed by t)
```

**Note:** Stress markers (`ˈ`, `ˌ`) are treated as transparent for all class checks. This lets rules match segment clusters like `rˈa` the same way they match `ra`.

### skipReplacements

Child language packs inherit all `replacements` and `preReplacements` from their parent. Sometimes a child pack needs to suppress a specific inherited rule rather than override it. `skipReplacements` is a list of `{from, to}` pairs that are removed from the merged rule set after inheritance is resolved.

```yaml
normalization:
  skipReplacements:
    - from: s
      to: s_es        # remove this specific rule from the parent
    - from: x
      to: x_es        # remove this one too
    - from: ɣ         # no 'to' — removes ALL rules with this 'from'
```

**When to use it:**

A parent pack (e.g. `es.yaml`) defines shared rules that remap base phonemes to language-tagged variants (`s → s_es`, `x → x_es`). A child dialect (e.g. `es-mx.yaml`) may have its own allophone for `/s/` — `s_mx` — and needs the parent's `s → s_es` rule gone before it adds its own `s → s_mx` rule.

Without `skipReplacements`, you'd have to duplicate the entire parent rule set and edit it, which defeats inheritance.

**Matching:**

- If `to` is specified: removes rules where both `from` and the first `to` candidate match.
- If `to` is omitted: removes all inherited rules with that `from`, regardless of target.

**Order of operations:**

`skipReplacements` is applied after the full inheritance chain is merged, so it sees the complete inherited rule list before any of the child's own rules are applied. The child's own `replacements` are added afterward and are never affected.

**Real example — es-mx.yaml:**

```yaml
normalization:
  skipReplacements:
    - from: x
      to: x_es        # Mexican /x/ is softer — base x already sounds right
    - from: s
      to: s_es        # Mexican /s/ is laminal, not apical — s_mx handles it

  replacements:
    - from: s
      to: s_mx        # laminal Mexican sibilant
```

### transforms
Rules that match phonemes by properties (`isVowel`, `isStop`, etc.) and then modify fields:
- `set`: set field values
- `scale`: multiply field values
- `add`: add offsets to fields

### intonation
Clause-type pitch shapes keyed by punctuation (example keys: `.`, `?`, `!`, `:`, `;`, `,`)

Defaults for `.`, `,`, `?`, `!` are seeded by `applyLanguageDefaults()` unless overridden by packs.

Each clause supports percent-based parameters used by `calculatePitches()` to apply pitch paths across prehead/head/nucleus/tail:
- `preHeadStart`, `preHeadEnd`: Pitch percentages for the prehead region (before the first stressed syllable).
- `headStart`, `headEnd`: Pitch percentages for the head region (from first stress up to the nucleus).
- `headSteps`: Ordered percentages that select stepped head contours on stressed syllables (example: `[100, 85, 70, 55, 40, 25, 10, 0]`).
- `headExtendFrom`: Index in `headSteps` to clamp/extend when the head is longer than the steps list.
- `headStressEndDelta`: Delta (in percent units) applied from a stressed syllable's start to its end.
- `headUnstressedRunStartDelta`, `headUnstressedRunEndDelta`: Deltas applied over unstressed runs following a stress.
- `nucleus0Start`, `nucleus0End`: Pitch percentages for the nucleus when the clause has no tail (single-word or tail-less cases).
- `nucleusStart`, `nucleusEnd`: Pitch percentages for the nucleus when a tail exists.
- `tailStart`, `tailEnd`: Pitch percentages for the tail region (after the nucleus).

### toneContours
Tone contour definitions (tone string → list of pitch-percent points)

## Frontend rule passes settings

Recent versions of the frontend can optionally run **modular "passes"** over the token stream (after IPA parsing, and optionally after timing / pitch). These passes are meant to encode phonetic/prosodic "rules" (in the spirit of SSRS/Delta/Eloquence) without turning the core IPA engine into a single giant function.

All settings below live under `settings:` in a language YAML (for example `packs/lang/en.yaml`). Passes are **disabled by default** unless `enabled: true` is set for that pass.

### Coarticulation

The coarticulation pass makes consonants "aim" toward a vowel-dependent locus instead of snapping to a single fixed consonant target. In practice this reduces edgy segment boundaries (especially in stop clusters) while keeping consonants intelligible at high speech rates.

```yaml
settings:
  # Master switch for this pass.
  coarticulationEnabled: true

  # Overall amount of locus pull (0.0–1.0). Higher = stronger vowel influence on consonants.
  coarticulationStrength: 0.25

  # Optional scaling for word-initial consonants (often useful to keep word onsets crisp).
  coarticulationWordInitialFadeScale: 1.0

  # Cross-syllable reduction: when a consonant and its adjacent vowel are in
  # different syllables, multiply the effective coarticulation strength by this.
  # Values < 1.0 make cross-syllable transitions crisper (less vowel pull on the
  # consonant), preserving syllable identity. Default 1.0 (no reduction).
  coarticulationCrossSyllableScale: 0.70
```

#### Graduated coarticulation

When `coarticulationGraduated` is enabled, the vowel influence is **scaled by vowel proximity** rather than being treated as an on/off switch. Adjacent vowels get the strongest pull; the effect tapers off as you move through consonant clusters.

```yaml
settings:
  coarticulationGraduated: true
  coarticulationAdjacencyMaxConsonants: 2   # 0=only immediate neighbors, 1=allow C_V, 2=allow CC_V
```

#### Locus targets

These define the vowel-independent "anchor" targets the pass pulls toward for common places of articulation. Think of them as the baseline F2 goals for labial / alveolar / velar stops and related consonants.

```yaml
settings:
  coarticulationLabialF2Locus: 800
  coarticulationAlveolarF2Locus: 1800
  coarticulationVelarF2Locus: 1200
```

#### Context-dependent velar F2 locus (optional)

The velar place of articulation is unusual — the tongue body contact point shifts depending on the neighboring vowel. Before front vowels ("geese", "key"), velars have a high F2 locus. Before back vowels ("go", "cool"), velars have a low F2 locus. Without this split, the phoneme's static `cf2` value dominates and velars sound nearly identical to alveolars before back vowels.

```yaml
settings:
  # 0 = disabled (use phoneme cf2 as-is). When set, overrides the
  # srcF2 fed to the MITalk coarticulation equation.
  coarticulationVelarF2LocusFront: 1800   # before front vowels (vowel F2 > 1600)
  coarticulationVelarF2LocusBack: 1100    # before back vowels  (vowel F2 <= 1600)
```

This gives "go" /ɡoʊ/ a gentle F2 glide (1100→900) while "geese" /ɡiːs/ stays crisp (1800→2200). The threshold between "front" and "back" is currently hardcoded at 1600 Hz (vowel F2).

#### Velar pinch (optional)

Velars often sound more natural when F2 and F3 move closer ("pinch") near high-front vowel contexts. This option applies a controlled pinch when the adjacent vowel's F2 suggests a fronting context.

```yaml
settings:
  coarticulationVelarPinchEnabled: true

  # Only apply pinch when the neighboring vowel's F2 is at/above this threshold.
  coarticulationVelarPinchThreshold: 1800

  # Scales F2 during pinch (values < 1.0 pull it down toward the pinch band).
  coarticulationVelarPinchF2Scale: 0.9

  # Target F3 to blend toward during pinch.
  coarticulationVelarPinchF3: 2400
```

#### Per-formant scaling

Control how much each formant is affected by coarticulation:

```yaml
settings:
  coarticulationF1Scale: 0.6   # F1 gets 60% of coarticulation effect
  coarticulationF2Scale: 1.0   # F2 gets full effect (most important for place)
  coarticulationF3Scale: 0.5   # F3 gets 50% of effect
```

#### Mitalk-K locus weighting

The `coarticulationMitalkK` parameter controls how strongly consonant locus targets pull the formants vs. how strongly vowel targets pull:

```yaml
settings:
  coarticulationMitalkK: 0.42  # 0.0 = all vowel, 1.0 = all consonant locus
```

Tuning notes:
- If consonants start feeling "too vowel-y", lower `coarticulationStrength`.
- If clusters get mushy at speed, keep `coarticulationGraduated: true` and reduce `coarticulationAdjacencyMaxConsonants` to `1`.
- If word onsets lose bite, lower `coarticulationWordInitialFadeScale`.
- For language-specific coarticulation effects (e.g. rhotic F3 lowering, labial F2 pulling), use the **special coarticulation** pass instead of modifying the core locus pass.

#### Aspiration coarticulation

Post-stop aspiration tokens (the /h/-like burst after voiceless stops like /p/, /t/, /k/) inherit formants from the generic /h/ phoneme via `_copyAdjacent`. But at the boundary between the stop burst and the following vowel, the aspiration's formants should transition smoothly along the stop→vowel locus trajectory rather than jumping to generic /h/ values.

The aspiration coarticulation settings control where the aspiration's formants sit on the stop→vowel trajectory:

```yaml
settings:
  # 0.0 = stop's locus target, 1.0 = vowel's formant target.
  # The DSP ramps from blendStart to blendEnd across the aspiration token.
  coarticulationAspirationBlendStart: 0.3   # start 30% toward the vowel
  coarticulationAspirationBlendEnd: 0.7     # end 70% toward the vowel
```

This creates a smooth spectral glide through the aspiration: formants start near the stop's place of articulation and arrive close to the vowel target by the time voicing begins. Without this, aspiration can sound disconnected — a generic "huh" between stop and vowel rather than a natural release.

The aspiration blend is scaled by the main `coarticulationStrength` so it tracks with the overall coarticulation intensity. It only applies to tokens marked as `postStopAspiration` (inserted by the post-stop aspiration pass), not to phonemic /h/.

### Special coarticulation

The special coarticulation pass applies **language-specific formant Hz deltas** to vowels adjacent to trigger consonants. Unlike the core coarticulation pass (which does MITalk-style locus interpolation), this pass lets you write explicit rules like "lower F3 by 200 Hz on vowels next to /ɹ/" directly in YAML.

```yaml
settings:
  specialCoarticulation:
    enabled: true
    maxDeltaHz: 400       # safety clamp on accumulated deltas

    rules:
      - name: "rhotic F3 lowering"
        triggers: ["ɹ", "r"]
        vowelFilter: all        # "all", "front", "back", or a specific IPA key
        formant: f3
        deltaHz: -200
        side: both              # "left", "right", or "both"
        cumulative: true        # if both sides match, apply twice
        unstressedScale: 0.7    # reduce effect on unstressed vowels
        phraseFinalStressedScale: 1.2   # boost at phrase-final stress

      - name: "labial F2 pull"
        triggers: ["w"]
        vowelFilter: front
        formant: f2
        deltaHz: -120
        side: right             # only when /w/ precedes the vowel
```

Flat-key equivalents:

- `specialCoarticulationEnabled`
- `specialCoarticMaxDeltaHz`

(Rules must be defined via the nested `rules:` sequence — there are no flat-key equivalents for individual rules.)

#### Rule fields

| Field | Type | Description |
|-------|------|-------------|
| `name` | string | Human-readable label (for debugging) |
| `triggers` | list | IPA keys that activate the rule (e.g. `["ɹ", "r"]`) |
| `vowelFilter` | string | Which vowels to affect: `"all"`, `"front"` (F2 > 1600), `"back"` (F2 < 1400), or a specific IPA key |
| `formant` | string | Which formant to shift: `"f2"` or `"f3"` |
| `deltaHz` | number | Hz offset to apply (negative = lower) |
| `side` | string | Where to look for triggers: `"left"`, `"right"`, or `"both"` |
| `cumulative` | bool | If true and triggers match on both sides, apply the delta twice |
| `unstressedScale` | number | Multiply delta by this for unstressed vowels (default `1.0`) |
| `phraseFinalStressedScale` | number | Multiply delta by this for phrase-final stressed vowels (default `1.0`) |

Tuning notes:
- Deltas are accumulated across all matching rules and clamped to ±`maxDeltaHz`.
- The pass also adjusts `endCf2`/`endCf3` if the locus pass set end targets, so transitions stay consistent.
- Use `cumulative: true` for rhotics where vowels between two /ɹ/ should get a double dose of F3 lowering.

### Cluster timing

The cluster timing pass shortens consonants that occur in clusters (adjacent to other consonants) and adjusts word-medial/word-final obstruent durations. This prevents consonant clusters from feeling "stacked" at normal speech rates.

```yaml
settings:
  clusterTiming:
    enabled: true

    # Two-consonant cluster scales (by type pair).
    fricBeforeStopScale: 0.65       # e.g. /st/, /sp/
    stopBeforeFricScale: 0.70       # e.g. /ts/, /pf/
    fricBeforeFricScale: 0.75       # e.g. /sf/
    stopBeforeStopScale: 0.60       # e.g. /kt/, /pt/

    # Triple cluster: middle consonant gets extra shortening.
    tripleClusterMiddleScale: 0.55  # e.g. the /t/ in /str/

    # Affricates in clusters get an additional multiplier.
    affricateInClusterScale: 0.75

    # Non-cluster position adjustments.
    wordMedialConsonantScale: 0.85  # word-internal consonants not in a cluster
    wordFinalObstruentScale: 0.90   # word-final stops/fricatives/affricates
```

Flat-key equivalents are also supported:

- `clusterTimingEnabled`
- `clusterTimingFricBeforeStopScale`
- `clusterTimingStopBeforeFricScale`
- `clusterTimingFricBeforeFricScale`
- `clusterTimingStopBeforeStopScale`
- `clusterTimingTripleClusterMiddleScale`
- `clusterTimingAffricateInClusterScale`
- `clusterTimingWordMedialConsonantScale`
- `clusterTimingWordFinalObstruentScale`

How it behaves:

- Consonants are classified as stop (including affricates), fricative, or other.
- In a two-consonant cluster, both members can be scaled: the first by "X before Y" and the second by "Y after X" (but stops after fricatives are not double-scaled — the fricative absorbs the shortening).
- Triple clusters (three consecutive consonants with no syllable/word boundary between them) apply `tripleClusterMiddleScale` to the middle consonant instead of the two-consonant rules.
- Syllable and word boundaries break triple cluster detection — e.g. in "abstract" /b.str/, the /s/ starts a new onset and doesn't get the triple shortening.
- All scale factors are clamped to ≤ 1.0 (never lengthen), and the result has a 2ms floor.

Tuning notes:
- If clusters sound "crushed" at high rates, raise the scale factors closer to 1.0.
- If isolated word-final stops/fricatives sound too long, lower `wordFinalObstruentScale`.
- The affricate multiplier stacks with the cluster type scale — so an affricate in a stop+stop cluster gets `stopBeforeStopScale × affricateInClusterScale`.

### Cluster blend

The cluster blend pass adds **formant coarticulation between adjacent consonants** in clusters. Where cluster *timing* shortens consonants to prevent stacking, cluster *blend* modifies C2's start formants to anticipate C1's place of articulation — creating a smooth spectral transition instead of an abrupt frequency jump at the C1→C2 boundary.

The pass reads each consonant's formant values from `phonemes.yaml` (consonants DO have formant targets — /s/: cf2=1390, /t/: cf2=1700, /k/: cf2=1800, /n/: cf2=1550) and blends C2's onset toward C1's values.

```yaml
settings:
  clusterBlend:
    enabled: true
    strength: 0.35              # Master blend fraction (0.0–1.0)

    # Per-manner-pair scale factors (multiplied by strength).
    # Higher = more spectral blending between these consonant types.
    nasalToStopScale:   1.30    # /ŋk/, /mp/, /nt/, /nd/
    fricToStopScale:    0.85    # /st/, /sk/, /sp/
    stopToFricScale:    0.70    # /ts/, /ks/
    nasalToFricScale:   1.00    # /nf/, /ns/, /mf/
    liquidToStopScale:  0.85    # /lt/, /rk/, /lp/
    liquidToFricScale:  0.75    # /ls/, /rf/
    fricToFricScale:    0.60    # /sʃ/ (rare, morpheme boundaries)
    stopToStopScale:    0.55    # /kt/, /pt/, /gd/
    defaultPairScale:   0.50    # Anything not listed above

    # Context modifiers
    homorganicScale:    0.30    # Same place of articulation → less shift needed
    wordBoundaryScale:  0.50    # C2 starts a new word → weaker blend

    # Per-formant scaling
    f1Scale: 0.50               # F1 blend is gentler (jaw height, not place)

    # Forward drift: fill endCf on ANY token still missing it.
    # After C→C blending, consonants next to vowels still have flat formants
    # during their hold phase. Forward drift looks at the next real phoneme
    # and sets endCf to drift this fraction of the distance toward it.
    # F1 drift is scaled by f1Scale to prevent a "tight jaw" sound.
    forwardDriftStrength: 0.30  # 0.0 = disabled, 0.3 = 30% drift toward next phoneme
```

Flat-key equivalents are also supported:

- `clusterBlendEnabled`
- `clusterBlendStrength`
- `clusterBlendNasalToStopScale`
- `clusterBlendFricToStopScale`
- `clusterBlendStopToFricScale`
- `clusterBlendNasalToFricScale`
- `clusterBlendLiquidToStopScale`
- `clusterBlendLiquidToFricScale`
- `clusterBlendFricToFricScale`
- `clusterBlendStopToStopScale`
- `clusterBlendDefaultPairScale`
- `clusterBlendHomorganicScale`
- `clusterBlendWordBoundaryScale`
- `clusterBlendF1Scale`
- `clusterBlendForwardDriftStrength`

How it behaves:

- For each C1→C2 consonant pair (skipping micro-gaps and post-stop aspiration tokens between them), the pass classifies both by manner (stop, fricative, nasal, liquid) and place (labial, alveolar, palatal, velar).
- The effective blend strength = `strength × pairScale × contextModifiers`, clamped to [0, 1].
- C2's start formants (F1, F2, F3) are interpolated toward C1's values: `startF2 = c2f2 + strength × (c1f2 − c2f2)`. The original C2 values are stored as `endCf2`/`endCf3` so the DSP ramps back to the canonical target during the phoneme.
- F1 uses a reduced blend (`strength × f1Scale`) since F1 is mainly jaw height, not place.
- A minimum delta threshold of 15 Hz prevents unnecessary micro-adjustments.
- Homorganic pairs (same place) get reduced blending — they already share similar formant targets.
- Word boundaries reduce blend strength since C2 belongs to a new articulatory plan.

#### Forward drift

The C→C blending above only handles consonant clusters. But consonants adjacent to vowels also sit spectrally frozen during their hold phase — the vowels glide (thanks to coarticulation setting endCf), but consonants between them are dead walls. The ear hears glide-freeze-glide-freeze.

`forwardDriftStrength` fixes this. After the C→C loop, a second loop runs over every token that still has no endCf set. For each, it looks ahead to the next real phoneme (skipping micro-gaps and aspiration) and sets endCf to drift `forwardDriftStrength` of the distance toward the next phoneme's formants. F1 drift is multiplied by `f1Scale` to prevent jaw-height changes from making the voice sound "tight-jawed."

For example, with `forwardDriftStrength: 0.30` and `f1Scale: 0.50`:
- /b/ (cf2=900) before /ə/ (cf2=1400): endCf2 = 900 + 0.30 × (1400 − 900) = 1050. The /b/ drifts 150 Hz toward the schwa during its hold phase.
- F1 drift = 0.30 × 0.50 = 0.15 — much gentler, keeping jaw movement subtle.
- /ə/ before /n/: if coarticulation already set endCf on the vowel, forward drift skips it (only fills gaps).

This is NOT coarticulation (which computes WHERE formants should be using locus math) — it's a simple "don't freeze" mechanism that ensures formants are always moving toward the next target.

Tuning notes:
- This pass complements cluster timing (duration) and boundary smoothing (crossfade). All three work together: timing shortens, smoothing crossfades, blend shapes the spectral trajectory.
- If clusters sound "blurred" or consonants lose identity, lower `strength` or the per-pair scales.
- Nasal→stop pairs (e.g. /ŋk/, /nt/) benefit from higher blend because the nasal's formant structure naturally anticipates the stop's place.
- Stop→stop pairs use lower scales because each stop has its own burst character that shouldn't be smeared.
- If forward drift makes the voice sound "tight-jawed," lower `forwardDriftStrength` or reduce `f1Scale`. The tight-jaw effect comes from F1 being pulled toward low consonant values.
- Forward drift is subtle — it fills the hold phase, not the crossfade. The ear perceives it as "nothing is ever still" rather than a dramatic formant sweep.

### Prominence

The prominence pass has two stages: **scoring** (phonological classification) and **realization** (acoustic output). Scoring assigns each vowel a 0.0–1.0 prominence value from stress marks, vowel length, and word position. Realization uses that score to apply duration multipliers, amplitude shaping, and pitch scaling.

When prominence is enabled, it **replaces** the old `primaryStressDiv`/`secondaryStressDiv` system for stress realization. The old stressDiv path is bypassed in `calculateTimes()`, and the prominence pass applies direct duration multipliers to vowels instead. This avoids double-lengthening and gives finer control (vowel-only, not whole-syllable).

```yaml
settings:
  prominence:
    enabled: true

    # ── SCORING (phonological classification, 0.0–1.0) ──
    # Primary stress always scores 1.0. These settings control other sources.

    secondaryStressLevel: 0.6       # ˌ → prominence 0.6 (score, not a multiplier)

    # Vowel length mark (Token.lengthened > 0)
    longVowelWeight: 0.5            # ː on unstressed vowel → 0.5
    longVowelMode: "unstressed-only" # "unstressed-only", "always", "never"

    # Word position (additive adjustments on the score, then clamped to [0,1])
    wordInitialBoost: 0.0           # added to first syllable's prominence
    wordFinalReduction: 0.0         # subtracted from last syllable's prominence

    # ── REALIZATION (what prominence DOES) ──

    # Duration: direct multipliers (like stressDiv but vowel-only).
    # These replace the old primaryStressDiv/secondaryStressDiv system.
    primaryStressWeight: 1.4        # prominence >= 0.9: durationMs *= 1.4
    secondaryStressWeight: 1.1      # prominence >= 0.4: durationMs *= 1.1

    # Duration safety nets
    durationProminentFloorMs: 0     # 0 = disabled. If > 0, prominent vowels
                                    # (score >= 0.4) never shrink below this.
    durationReducedCeiling: 1.0     # non-prominent vowels (score < 0.3) scaled
                                    # toward this (1.0 = no reduction, 0.7 = 70%)

    # Amplitude: prominence → voiceAmplitude adjustment (dB)
    amplitudeBoostDb: 0.0           # max boost for prominence=1.0
    amplitudeReductionDb: 0.0       # max reduction for prominence=0.0
                                    # (positive number = amount of reduction in dB)

    # Pitch: prominence → Fujisaki accent amplitude scaling
    pitchFromProminence: false       # false = pitch_fujisaki uses stress as before
                                     # true  = pitch_fujisaki scales accents by prominence
```

Flat-key equivalents are also supported:

- `prominenceEnabled`
- `prominenceSecondaryStressLevel`
- `prominencePrimaryStressWeight`
- `prominenceSecondaryStressWeight`
- `prominenceLongVowelWeight`
- `prominenceLongVowelMode`
- `prominenceWordInitialBoost`
- `prominenceWordFinalReduction`
- `prominenceDurationProminentFloorMs`
- `prominenceDurationReducedCeiling`
- `prominenceAmplitudeBoostDb`
- `prominenceAmplitudeReductionDb`
- `prominencePitchFromProminence`

#### Design principles

1. **Score and realization are separate.** Scoring is phonological classification (primary=1.0, secondary=`secondaryStressLevel`, unstressed=0.0). Realization settings (`primaryStressWeight`, `secondaryStressWeight`) are direct duration multipliers applied in a later pass. This means the weight knob always does something audible — `1.4` = 40% longer, `0.8` = 20% shorter.

2. **Prominence is max-combined, not additive.** If a vowel has primary stress (1.0) AND is long (0.5), prominence = max(1.0, 0.5) = 1.0, not 1.5. Word-position adjustments are additive on top of the max (then clamped to 0.0–1.0).

3. **Replaces stressDiv when enabled.** When `prominence.enabled` is true, the old `primaryStressDiv`/`secondaryStressDiv` system is bypassed in `calculateTimes()`. All stress-based duration shaping is handled by the prominence pass (vowel-only, not whole-syllable). When `prominence.enabled` is false (default), the old stressDiv system runs exactly as before — zero change.

4. **Auto-migration.** If a pack enables prominence but doesn't set custom weight values, the engine auto-migrates from `primaryStressDiv`/`secondaryStressDiv` into the equivalent prominence weights. This prevents silent regressions when switching an existing pack to prominence mode.

5. **Diphthong-safe.** Offglide tokens (`tiedFrom`) inherit their nucleus's prominence score for amplitude treatment, but the duration floor is skipped — their short duration IS the glide.

#### How prominence interacts with stress divisors

When `prominence.enabled` is **false**: `primaryStressDiv` and `secondaryStressDiv` run as before, slowing entire syllables. Prominence has no effect.

When `prominence.enabled` is **true**: the stressDiv path is bypassed (`curSpeed = baseSpeed` for all syllables regardless of stress). Instead, the prominence pass applies `primaryStressWeight` / `secondaryStressWeight` as direct duration multipliers on vowels only. This is more precise — consonants keep their natural cluster-timing durations, and the weight knob has a direct, predictable effect (`1.4` = 40% longer stressed vowels).

#### Stress inheritance from eSpeak

eSpeak puts stress marks on syllable-initial consonants, not vowels. The prominence pass walks backward from each vowel to the syllable start to check for inherited stress, without crossing word boundaries or other vowels.

#### Duration realization details

Duration multipliers are applied based on prominence thresholds:

| Prominence | Category | Duration effect |
|------------|----------|-----------------|
| >= 0.9 | Primary stress | `durationMs *= primaryStressWeight` |
| >= 0.4 | Secondary stress | `durationMs *= secondaryStressWeight` |
| < 0.3 | Unstressed | blended toward `durationReducedCeiling` |
| 0.3–0.9 gap | — | no double-application (clean threshold bands) |

The thresholds are designed so that: primary stress always scores 1.0 (even with -0.1 word-final reduction = 0.9), secondary scores ~0.6 (>= 0.4), and unstressed scores ~0.0 (< 0.3).

#### Amplitude realization details

- Prominent vowels (score >= 0.5): boost scales linearly from 0dB at 0.5 to `amplitudeBoostDb * primaryStressWeight * factor` at 1.0. The weight scaling means turning up the weight knob also makes stressed vowels louder.
- Non-prominent vowels (score < 0.3): reduction scales linearly from 0dB at 0.3 to full `amplitudeReductionDb` at 0.0. Reduction is NOT scaled by the weight — unstressed vowels get reduced regardless.
- Vowels between 0.3 and 0.5 are untouched (dead zone to avoid flutter).

#### Pitch realization details

When `pitchFromProminence` is true, the Fujisaki accent commands use prominence scores instead of raw stress marks. Any vowel with prominence > 0.05 gets an accent command with amplitude = `primaryAccentAmp * accentBoost * prominence`. This lets long vowels and word-initial vowels receive pitch peaks even without explicit stress marks.

The original stress-based accent path is preserved exactly when `pitchFromProminence` is false.

#### Syllable-position duration shaping (Pass 2b)

After prominence's main duration realization (stressed/unstressed vowel scaling), an optional second stage shapes durations based on syllable position. This creates natural rhythm: onset consonants get slightly more time (they initiate the gesture), coda consonants get less (they trail off), and unstressed open syllables compress their nucleus.

```yaml
settings:
  syllableDuration:
    enabled: true
    onsetScale: 1.10                     # onset consonants +10%
    codaScale: 0.85                      # coda consonants -15%
    unstressedOpenNucleusScale: 0.90     # unstressed open-syllable vowels -10%
```

Flat-key equivalents:

- `syllableDurationEnabled`
- `syllableDurationOnsetScale`
- `syllableDurationCodaScale`
- `syllableDurationUnstressedOpenNucleusScale`

How it behaves:

- Walks each word using the existing `syllableIndex` assignments. Monosyllabic words are skipped (no positional contrast).
- **Word-final syllables are skipped** — they're already shaped by phrase-final lengthening and `wordFinalObstruentScale`. Compressing them further makes final syllables disappear.
- For each non-final syllable, tokens are classified as onset (consonant before the nucleus vowel), nucleus (first vowel), or coda (consonant after the nucleus).
- Onset consonants get `onsetScale` (default 1.10 = 10% longer).
- Coda consonants get `codaScale` (default 0.85 = 15% shorter).
- The nucleus vowel gets `unstressedOpenNucleusScale` only if the syllable is unstressed AND open (no coda consonant) AND the vowel is not a diphthong offglide.
- Gap tokens (preStopGap, clusterGap, vowelHiatusGap, postStopAspiration, voicedClosure) are skipped.
- A 2ms floor and `fadeMs = min(fadeMs, durationMs)` clamp prevent artifacts.

**Stacking with prominence reduction:** An unstressed open-syllable vowel gets both `durationReducedCeiling` (from Pass 2) and `unstressedOpenNucleusScale` (from Pass 2b) multiplicatively. With the en-us defaults (0.92 × 0.90 = 0.83), this is intentional — these are the lightest syllables in natural speech (like /bə/ in "banana"). If vowels vanish, raise either value.

#### Pipeline position

Prominence (including syllable-position shaping) runs PostTiming, after `cluster_timing` but before `prosody`. This means cluster-adjusted consonant durations are already set before prominence looks at vowels, and phrase-final lengthening (in the prosody pass) stacks on top of prominence duration adjustments.

Tuning notes:
- **English defaults**: `primaryStressWeight: 1.4`, `secondaryStressWeight: 1.1`, `amplitudeBoostDb: 1.5`, `amplitudeReductionDb: 2.0`, `durationReducedCeiling: 0.85`. This gives stressed vowels 40% more duration + 1.5dB boost, unstressed vowels 15% shorter + 2dB quieter.
- `durationProminentFloorMs: 45` prevents stressed vowels from collapsing at high rates.
- **Hungarian**: `primaryStressWeight: 1.05` (barely any duration stress), `amplitudeBoostDb: 1.8` (stress realized through amplitude), `durationReducedCeiling: 1.0` (no unstressed reduction — syllable-timed).
- For Hungarian, `longVowelWeight: 0.5`, `longVowelMode: "unstressed-only"` — vowel length carries prosodic weight independently of stress.
- For English, `longVowelMode: "never"` is appropriate since stress is lexical, not length-based.
- The weight knob has a direct, predictable effect: `2.0` = stressed vowels twice as long, `0.5` = half as long, `1.0` = no duration stress (amplitude/pitch only).

### Syllable-aware processing

The engine assigns a `syllableIndex` to every token, enabling passes to distinguish within-syllable transitions (one articulatory gesture, should be smooth) from cross-syllable transitions (separate gestures, should be crisper). Two mechanisms work together:

1. **Syllable marking pass** (`syllable_marking`, PreTiming) — walks the token stream and converts `syllableStart` booleans into sequential `syllableIndex` integers per word. Tokens without a phoneme definition or silence tokens get index `-1`.

2. **Onset maximization** (text parser) — for words in the stress dictionary whose nucleus count matches, the text parser inserts IPA `.` syllable boundary markers at linguistically correct positions using a legal onset table loaded from the language pack. The IPA engine parses `.` and sets `syllableStart` on the next token, suppressing the heuristic consonant→vowel boundary detection for that word.

The heuristic (consonant→vowel transition = syllable start) is the fallback for words not in the stress dictionary or languages without an onset table.

#### Syllable structure configuration

The legal onset table is defined in the language pack under `syllableStructure:`:

```yaml
settings:
  syllableStructure:
    legalOnsets:
      # CC: stop + liquid/glide
      - pl
      - pɹ
      - bl
      - bɹ
      - tɹ
      - kl
      - kɹ
      - fl
      - fɹ
      - sl
      - sm
      - sn
      - sp
      - st
      - sk
      - sw
      - ʃɹ
      # CCC: s + stop + liquid/glide
      - spl
      - spɹ
      - stɹ
      - skɹ
      - skw
```

Each entry is an IPA string representing a consonant cluster that can legally begin a syllable. The onset maximization algorithm finds the longest legal onset suffix in a consonant cluster between two vowel nuclei and places the `.` boundary before it.

**Example:** "abstract" /æbstɹækt/ — the cluster between the two vowels is /bstɹ/. The longest legal onset suffix is /stɹ/ (in the table), so the boundary becomes /æb.stɹækt/. Without onset maximization, the heuristic would produce /æbstɹ.ækt/ (boundary at the consonant→vowel transition).

Languages without a `syllableStructure:` block get an empty onset table, and the text parser skips onset maximization entirely — the heuristic fallback handles everything.

#### How other passes use syllableIndex

- **Boundary smoothing:** Within-syllable transitions get gentler transition scales and longer fades (configurable via `withinSyllableScale` and `withinSyllableFadeScale`).
- **Coarticulation:** Cross-syllable consonant→vowel pairs get reduced locus pull (configurable via `coarticulationCrossSyllableScale`).
- **Cluster timing:** Triple-cluster detection respects syllable boundaries — consonants in different syllables are not treated as a single cluster.

### Boundary smoothing

#### How boundary smoothing differs from coarticulation

Coarticulation and boundary smoothing work together but answer different questions:

**Coarticulation answers: WHERE should the formants be?** It computes the actual Hz targets. When you say /ba/, the /a/ vowel doesn't start at pure /a/ formants — it starts shifted toward the labial locus because your lips are still closing from the /b/. Coarticulation calculates that shifted starting point (using MITalk locus math: `start = vowel + k * (consonantLocus - vowel)`), sets that as the frame's cf1/cf2/cf3, and puts the pure vowel target in endCf1/endCf2/endCf3. It defines the endpoints of the journey — where you start and where you're going.

**Boundary smoothing answers: HOW FAST should the formants get there?** It doesn't touch Hz values at all. It sets fade durations (how many ms the crossfade between adjacent frames takes) and transScale values (what fraction of that fade time each formant group uses to reach its target). It defines the speed and shape of the journey.

Think of it as **GPS vs driving style**. Coarticulation is the GPS — it says "you're at point A, your destination is point B." Boundary smoothing is how you drive — do you floor it and arrive in 30% of the time, or do you cruise and use the full duration? Both passes are place-aware: coarticulation picks different locus targets for labials vs velars vs palatals, and boundary smoothing picks different transition speeds to match the articulator physics.

#### What it does

Boundary smoothing increases the crossfade (fade) time at "harsh" segment boundaries (vowel→stop, stop→vowel, vowel→fricative, etc.) to reduce micro-clicks and make syllables flow together. At the same time, it sets per-formant transition scales (`transF1Scale`, `transF2Scale`, `transF3Scale`) to values **below 1.0** so that formant frequencies arrive at their target early — then hold steady while the amplitude crossfade finishes naturally. This "formants lead, amplitude follows" approach prevents the "wrong formants at full amplitude" discontinuity that can occur with longer fades.

#### Place-aware transition speeds

Transition scales are selected based on the consonant's **place of articulation**, because different articulators have different physical mass and speed:

| Place | Articulator | F1 Scale | F2 Scale | F3 Scale | Rationale |
|-------|-------------|----------|----------|----------|-----------|
| Labial | Lips (light, fast) | 0.25 | 0.60 | 0.55 | Lips are independent of tongue — F2/F3 drift rather than snap |
| Alveolar | Tongue tip (light, precise) | 0.30 | 0.40 | 0.35 | Tip flicks fast — formants arrive quickly |
| Palatal | Tongue blade (medium) | 0.30 | 0.55 | **0.70** | F3 must linger in the low postalveolar region — F3 IS the /ʃ/ vs /s/ distinction |
| Velar | Tongue body (heavy) | 0.30 | **0.65** | 0.60 | Tongue body is the heaviest articulator — F2 transition IS the velar cue |
| Unknown | (fallback) | 0.30 | 0.50 | 0.50 | Global defaults when place can't be determined |

For C→V transitions, the consonant's place determines the speeds. For V→C transitions, the same consonant-based speeds are used but with a **40% departure slowdown** — the vowel should hold its identity longer before the consonant's influence takes over. For example, a velar V→C transition: `0.65 * 1.4 = 0.91` — nearly full fade duration, because the vowel needs to hold while the heavy tongue body begins its slow velar movement.

#### Direction and context awareness

Beyond place, the pass adjusts for transition direction and utterance position:

- **V→C departure slowdown (1.4x):** When leaving a vowel, formant transitions are 40% slower. The ear expects the vowel to hold its quality before consonant influence takes over.
- **Pre-silence vowel protection:** Utterance-final vowels (last sound before silence) skip transScale entirely. The vowel holds steady and just fades in amplitude. Without this, formants bend into silence and sound metallic (e.g. "three" where /iː/ would rush toward nowhere).
- **Utterance-final consonant gentling (1.5x):** Word-final consonants before silence get 50% gentler scales — there's no following target to rush toward.
- **V→V (hiatus):** Vowel-to-vowel transitions skip transScale entirely. Both sides are targets; no rushing.

The pre-silence lookahead skips micro-gaps (preStopGap, clusterGap, vowelHiatusGap) to find the real next token — a closure gap before a word-final stop does NOT count as "silence" for this purpose.

#### Safety guards

Two safety guards prevent the fade stretch from causing artifacts:
- **Aspiration bypass:** Tokens whose onset is aspiration-dominant (`aspirationAmplitude > 0.08`, `voiceAmplitude < 0.1` — e.g. /h/, voiceless fricatives) are skipped entirely. Aspiration needs a crisp onset; a gradual fade-in sounds mushy.
- **Voicing-flip guard:** When voicing flips between two non-vowel, non-stop consonants (e.g. /z/→/s/), the fade is not stretched. A longer crossfade would make voicing and aspiration overlap, producing buzz. Stops, vowels, and word boundaries are exempt from this guard.

#### Configuration

```yaml
settings:
  boundarySmoothing:
    enabled: true

    # Global fallback transition scales (used when place is Unknown).
    f1Scale: 0.3
    f2Scale: 0.5
    f3Scale: 0.5

    # Per-place-of-articulation transition speeds.
    # These override the global scales when the consonant's place is known.
    labialF1Scale: 0.25
    labialF2Scale: 0.60
    labialF3Scale: 0.55
    alveolarF1Scale: 0.30
    alveolarF2Scale: 0.40
    alveolarF3Scale: 0.35
    palatalF1Scale: 0.30
    palatalF2Scale: 0.55
    palatalF3Scale: 0.70
    velarF1Scale: 0.30
    velarF2Scale: 0.65
    velarF3Scale: 0.60

    # Plosive/nasal-specific transition behavior.
    plosiveSpansPhone: true       # formants ramp across entire plosive burst
    nasalF1Instant: true          # F1 jumps nearly instantly at nasal boundaries
    nasalF2F3SpansPhone: true     # F2/F3 ramp across entire nasal

    # Per-boundary-type fade times (ms).
    # Fast speech (speed > 1.0) shortens proportionally; slow speech
    # keeps them at the configured values (never stretched beyond these).
    vowelToStopFadeMs: 22.0
    stopToVowelFadeMs: 20.0
    vowelToFricFadeMs: 18.0
    fricToVowelFadeMs: 18.0
    vowelToNasalFadeMs: 16.0
    nasalToVowelFadeMs: 16.0
    vowelToLiquidFadeMs: 14.0
    liquidToVowelFadeMs: 14.0
    nasalToStopFadeMs: 12.0
    liquidToStopFadeMs: 12.0
    fricToStopFadeMs: 10.0
    stopToFricFadeMs: 14.0
    vowelToVowelFadeMs: 18.0

    # Syllable-aware gentling.
    # Within-syllable transitions are one articulatory gesture — they should
    # be smoother than cross-syllable transitions.
    withinSyllableScale: 1.5       # transScale multiplier for within-syllable pairs
    withinSyllableFadeScale: 1.3   # fade multiplier for within-syllable pairs
```

**Within-syllable gentling:** When two adjacent tokens share the same `syllableIndex`, their transition is within a single articulatory gesture. `withinSyllableScale` multiplies the per-place transScale values (clamped to 1.0 — transitions get slower, not faster), and `withinSyllableFadeScale` multiplies the fade duration. The defaults (1.5 / 1.3) give within-syllable transitions noticeably gentler smoothing without turning cross-syllable boundaries to mush.

All settings have flat-key equivalents. You can use either the nested `boundarySmoothing:` block or flat camelCase keys — the engine reads both (nested block takes priority if both are present).

Flat-key naming convention: `boundarySmoothing` + field name in PascalCase. Examples:

- `boundarySmoothingEnabled`, `boundarySmoothingF1Scale`, `boundarySmoothingF2Scale`, `boundarySmoothingF3Scale`
- `boundarySmoothingPlosiveSpansPhone`, `boundarySmoothingNasalF1Instant`, `boundarySmoothingNasalF2F3SpansPhone`
- `boundarySmoothingVowelToStopFadeMs`, `boundarySmoothingStopToVowelFadeMs`, ... (all 13 fade times)
- `boundarySmoothingLabialF1Scale`, `boundarySmoothingLabialF2Scale`, ... (all 12 place scales)
- `boundarySmoothingWithinSyllableScale`, `boundarySmoothingWithinSyllableFadeScale`

#### Transition coverage

The pass handles a comprehensive set of boundary types:

**Vowel transitions:** vowel→stop, stop→vowel, vowel→fricative, fricative→vowel, vowel→nasal, nasal→vowel, vowel→liquid, liquid→vowel, vowel→vowel (hiatus, but not tied diphthongs).

**Consonant cluster transitions:** nasal→stop, liquid→stop, fricative→stop, stop→fricative, nasal→fricative, fricative→nasal, stop→nasal, nasal→liquid, liquid→fricative.

**Fallback transitions:** Any consonant→consonant pair not covered above gets a generic 10ms target. Additionally, sonorant→unclassified-consonant and unclassified-consonant→sonorant transitions (e.g. /n/→/h/, where /h/ uses `aspirationAmplitude` not `fricationAmplitude` and thus `tokIsFricative()` returns false) get vowel-to-fricative-equivalent targets.

#### Fade cap and floor

The fade is capped at 75% of the token's duration to preserve some steady-state. A 6ms floor prevents the cap from creating near-discontinuities on very short sentence-final phones.

#### Tuning notes

**Per-place tuning by ear:**
- If velars still sound too abrupt (sharp /g/ in "dialog"): increase `velarF2Scale` toward 0.75
- If palatals lose their /ʃ/ quality ("bridge" sounds too /s/-like): increase `palatalF3Scale` toward 0.80
- If labials sound too "drifty": decrease `labialF2Scale` toward 0.50
- If alveolars sound too snappy: increase `alveolarF2Scale` toward 0.50

**General:**
- `nasalF1Instant: true` is important for natural nasal transitions — F1 jumps sharply at the velum opening/closing, overriding the place-based scale.
- Aspiration-dominant tokens (/h/, voiceless fricatives) are automatically skipped — they keep their natural crisp onset.
- If consonant-to-consonant transitions sound harsh, the generic fallback (10ms) may need tuning for your language.
- The pre-silence protection skips the nasal F1 override too — utterance-final nasalized vowels hold steady rather than snapping. This is correct behavior but worth monitoring if your language has prominent final nasal vowels (e.g. French).

### Trajectory limiting

Trajectory limiting caps how fast key formants are allowed to move across token boundaries (best applied to **F2/F3** first). If a transition would require an extremely fast jump, the frontend spreads the change by increasing the boundary crossfade (up to `windowMs`).

```yaml
settings:
  trajectoryLimit:
    enabled: true
    applyTo: [cf1, cf2, cf3, pf2, pf3]

    # Maximum allowed change rate in Hz per ms (at speed=1.0)
    maxHzPerMs:
      cf1: 15
      cf2: 18
      cf3: 22
      pf2: 18
      pf3: 22

    # Upper cap on how much we are allowed to "spread" a change (ms at speed=1.0)
    windowMs: 25

    # If false, do not apply the limiter across word boundaries.
    applyAcrossWordBoundary: false

    # Liquids (/ɹ/, /l/) get gentler rate limits — their large formant
    # excursions are perceptually expected.  1.5 = allow 50% faster Hz/ms.
    liquidRateScale: 1.5
```

Flat-key equivalents (for use without the nested block):

- `trajectoryLimitEnabled`, `trajectoryLimitWindowMs`, `trajectoryLimitApplyAcrossWordBoundary`, `trajectoryLimitLiquidRateScale`
- `trajectoryLimitApplyTo` — comma-separated field names: `"cf1, cf2, cf3, pf2, pf3"` (brackets optional)
- `trajectoryLimitMaxHzPerMsCf2`, `trajectoryLimitMaxHzPerMsCf3`, `trajectoryLimitMaxHzPerMsPf2`, `trajectoryLimitMaxHzPerMsPf3`

Tuning notes:
- Smaller `maxHzPerMs` = smoother transitions (but too small can blur consonant identity).
- Larger `windowMs` = the limiter has more room to soften big jumps (try 30–40ms if you want fewer "corners").
- If you want the effect only inside words, keep `applyAcrossWordBoundary: false`.
- The pass is transScale-aware: it reads `transF1Scale`/`transF2Scale`/`transF3Scale` from boundary smoothing and computes the actual Hz/ms rate through the effective (compressed or expanded) fade, not the raw fade time.
- Nasals and semivowels are always exempt (sharp transitions are their perceptual identity). Liquids are rate-limited but at `liquidRateScale`× the normal limit.

### Phrase-final lengthening

Duration matters as much as pitch for prosody. Phrase-final lengthening scales the last (and optionally the penultimate) syllable/token durations near clause boundaries.

```yaml
settings:
  phraseFinalLengthening:
    enabled: true

    finalSyllableScale: 1.35
    penultimateSyllableScale: 1.12

    # Optional clause-type tuning (multipliers applied on top).
    statementScale: 1.00
    questionScale: 0.92

    # If true, apply mostly to the "nucleus" (vowel-like) portion.
    nucleusOnlyMode: true

    # Coda bias (full-syllable mode only, nucleusOnlyMode: false).
    # Stretches coda consonants more than the nucleus vowel, preserving
    # vowel clarity while giving word-final morphological segments
    # (/s/, /d/, /z/) the lengthening they need.
    # Both default to 0.0 (disabled — uses finalSyllableScale for all).
    nucleusScale: 1.0    # vowel stays near natural length
    codaScale: 1.10      # generic coda fallback (nasals, liquids)

    # Class-aware coda scaling (falls back to codaScale when 0.0).
    # Stops/affricates benefit from extra duration — the burst needs
    # room for perceptual clarity.  Fricatives already carry strong
    # noise energy; stretching them just makes them hissy.
    codaStopScale: 1.30       # stops + affricates
    codaFricativeScale: 1.0   # fricatives — no stretch
    codaNasalScale: 1.25      # nasals — gentle sustain

    # Diphthong nuclei are already long (two vowel targets);
    # giving them the same lengthening as monophthongs makes them
    # sound drawled.  Set a reduced scale for diphthong nuclei.
    # 0.0 = fall back to nucleusScale (treat same as monophthong).
    nucleusDiphthongScale: 1.08
```

Flat-key equivalents: `phraseFinalLengtheningCodaStopScale`, `phraseFinalLengtheningCodaFricativeScale`, `phraseFinalLengtheningCodaNasalScale`, `phraseFinalLengtheningNucleusDiphthongScale`.

The engine detects diphthongs via the `tiedTo` flag on the nucleus vowel (ties it to its offglide). Monophthongs get `nucleusScale`, diphthongs get `nucleusDiphthongScale`.

### Microprosody

Microprosody adds small F0 perturbations around consonant→vowel boundaries and models several natural pitch/duration effects. Six independently-gated phases:

1. **Onset F0** (backward-looking): voiceless C raises vowel start pitch; voiced obstruent lowers it.
2. **Endpoint F0** (forward-looking): voiceless C after vowel raises end pitch; voiced obstruent lowers it.
3. **Intrinsic vowel F0**: high vowels (low F1) run slightly higher F0; low vowels (high F1) run lower.
4. **Pre-voiceless shortening**: vowels before voiceless consonants are shortened (strongest duration cue in English).
5. **Voiceless coda lengthening**: voiceless consonants after voiced segments get lengthened. This is the complement to Phase 4 — when the vowel shrinks before a voiceless coda, the consonant grows to preserve syllable weight (Cho & Ladefoged 1999).
6. **Total perturbation cap**: clamps combined pitch delta so no single vowel shifts more than `maxTotalDeltaHz`.

```yaml
  # All fields are flat keys (no nested block).
  microprosodyEnabled: true
  microprosodyMinVowelMs: 25

  # Phase 1: onset (backward-looking)
  microprosodyVoicelessF0RaiseEnabled: true
  microprosodyVoicelessF0RaiseHz: 15
  microprosodyVoicelessF0RaiseEndHz: 8    # optional: separate value for vowel end (0 = same as RaiseHz)
  microprosodyVoicedF0LowerEnabled: true
  microprosodyVoicedF0LowerHz: 8
  microprosodyVoicedFricativeLowerScale: 0.6   # fricatives lower less than stops

  # Phase 2: endpoint (forward-looking)
  microprosodyFollowingF0Enabled: true
  microprosodyFollowingVoicelessRaiseHz: 10
  microprosodyFollowingVoicedLowerHz: 5

  # Phase 3: intrinsic vowel F0 (F1-based height classification)
  microprosodyIntrinsicF0Enabled: true
  microprosodyIntrinsicF0HighThreshold: 400    # F1 below = high vowel
  microprosodyIntrinsicF0LowThreshold: 600     # F1 above = low vowel
  microprosodyIntrinsicF0HighRaiseHz: 6
  microprosodyIntrinsicF0LowDropHz: 4

  # Phase 4: pre-voiceless shortening
  microprosodyPreVoicelessShortenEnabled: true
  microprosodyPreVoicelessShortenScale: 0.85
  microprosodyPreVoicelessMinMs: 25

  # Phase 5: voiceless coda lengthening
  microprosodyVoicelessCodaLengthenEnabled: true
  microprosodyVoicelessCodaLengthenScale: 1.20   # 20% longer

  # Phase 6: total perturbation cap (0 = no cap)
  microprosodyMaxTotalDeltaHz: 0
```

### Rate compensation

Rate compensation enforces configurable perceptual duration floors at high speech rates. At normal speed (1x) the pass does nothing. At high speed it prevents segments from being crushed below their audibility threshold.

Every phoneme class has its own minimum duration floor. Word-final segments get extra padding because English carries morphological information at word boundaries (plural /s/, past tense /d/, possessive /z/). An optional cluster proportion guard prevents single-segment "bulge" when one consonant in a cluster hits its floor.

```yaml
settings:
  rateCompensation:
    enabled: true
    minimumDurations:
      vowelMs: 25          # formant identity needs ~25ms
      fricativeMs: 18       # spectral identity needs noise cycles
      stopMs: 4             # already tiny — protect what's left
      nasalMs: 18           # place perception (/ɲ/ vs /n/ vs /ŋ/)
      liquidMs: 15           # /ɹ/ and /l/ need F3 transition time
      affricateMs: 12        # shorter than fricative, longer than stop
      semivowelMs: 10        # glide needs trajectory
      tapMs: 4               # quick by nature
      trillMs: 12            # needs modulation cycles
      voicedConsonantMs: 10  # catch-all voiced C

    wordFinalBonusMs: 5      # extra floor at word end (morphology)
    floorSpeedScale: 0.0     # 0.0 = rigid floors (recommended)

    clusterProportionGuard: true
    clusterMaxRatioShift: 0.4

    schwaReduction:
      enabled: false         # rate-dependent schwa shortening
      threshold: 2.5
      scale: 0.8
```

All fields also have flat-key equivalents (e.g. `rateCompVowelFloorMs: 25`). Old `rateReductionEnabled` keys are mapped to the new system automatically for backward compatibility.

A separate universal `nasalMinDurationMs` field (default 18.0) in pack.h enforces a nasal floor in `calculateTimes` for all languages regardless of whether rate compensation is enabled. Rate compensation's `nasalMs` floor is a second layer on top.

### Word-final schwa reduction

Independent of speech rate, word-final schwas can optionally be shortened. This is useful for languages like Danish, German, French, and Portuguese where unstressed final syllables are naturally reduced.

```yaml
settings:
  wordFinalSchwaReductionEnabled: true
  wordFinalSchwaScale: 0.6           # 0.0–1.0, lower = shorter
  wordFinalSchwaMinDurationMs: 8.0   # floor to avoid total silence
```

Unlike rate-dependent schwa shortening (which activates at high speeds via rate compensation), this applies at all speech rates. It runs as Phase 0 of the rate compensation pass, gated by its own bool — it executes even when `rateCompEnabled` is false.

### Anticipatory nasalization

Anticipatory nasalization partially nasalizes a vowel shortly before a nasal consonant. This is optional and language-dependent.

```yaml
settings:
  nasalization:
    anticipatory:
      enabled: false
      ms: 30
      amplitude: 0.35
      blend: 0.50
```

### Liquid dynamics

Liquid/glide tokens can have internal movement rather than a static steady-state target.

```yaml
settings:
  liquidDynamics:
    enabled: true

    lateralOnglide:
      f1Delta: -50
      f2Delta: 200
      durationPct: 0.30

    rhoticF3Dip:
      enabled: true
      f3Minimum: 1600
      dipDurationPct: 0.50

    labialGlideTransition:
      enabled: true
      startF1: 300
      startF2: 700
      transitionPct: 0.60
```

### Length contrast and gemination

For languages with phonemic length, it helps to enforce floors/ceilings and make geminates behave like closures rather than just "slightly longer consonants".

```yaml
settings:
  lengthContrast:
    enabled: true

    # Vowel duration constraints (ms, at speed=1.0).
    shortVowelCeilingMs: 80
    longVowelFloorMs: 120

    # Geminate shaping.
    geminateClosureScale: 1.80
    geminateReleaseScale: 0.90
    preGeminateVowelScale: 0.85
```

### Diphthong collapse

Diphthongs like /eɪ/, /aɪ/, /aʊ/, /oʊ/, /ɔɪ/ are stored as two separate vowel tokens in the IPA stream. Without this pass, the two tokens produce a "two-beat" artifact — two steady-state plateaus connected by a boundary-smoothing crossfade. Real diphthongs are one continuous formant sweep.

The diphthong collapse pass merges **tied** vowel pairs (where token A has `tiedTo` and token B has `tiedFrom`) into a single token. The merged token carries onset formants from A and offset formants from B. During frame emission, the merged token produces multiple micro-frames with cosine-smoothed formant waypoints, yielding a natural glide.

Tied pairs can come from two sources:
- **Manual tie-bar injection** — allophone rules that insert a Unicode tie bar (U+0361) between vowel pairs (e.g. `eɪ → e͡ɪ`).
- **`autoTieDiphthongs: true`** — the IPA engine automatically ties adjacent same-syllable vowel pairs. Only vowel+vowel sequences are tied (not semivowel+vowel, so /wɪ/ in "quick" stays separate).

```yaml
settings:
  autoTieDiphthongs: true       # auto-tie adjacent vowels in same syllable

  diphthongCollapse:
    enabled: true
    durationFloorMs: 60         # minimum ms for merged token (prevents crush at high rate)
    microFrameIntervalMs: 6     # target interval between waypoints (lower = smoother)
    onsetHoldExponent: 1.4      # >1.0 lingers at onset before sweeping (pow(frac, exp))
    amplitudeDipFactor: 0.03    # midpoint amplitude dip (sin curve, 0 = none)
```

**Settings reference:**

| Setting | Default | What it does |
|---------|---------|--------------|
| `enabled` | `true` | Master switch for the pass |
| `durationFloorMs` | `50.0` | Minimum duration for the merged diphthong token. Prevents the glide from being crushed at high speech rates. This floor is enforced inside the collapse pass itself (not in rate compensation). |
| `microFrameIntervalMs` | `6.0` | Target interval between micro-frame waypoints. Lower values produce more frames and smoother glides but cost more CPU. The frame count is clamped to 3–10. |
| `onsetHoldExponent` | `1.4` | Exponent applied to the interpolation fraction before cosine smoothing: `pow(frac, exp)`. Values > 1.0 make the glide linger at the onset target before sweeping toward the offset. Audible range is roughly 0.1–2.0. |
| `amplitudeDipFactor` | `0.03` | Controls a small amplitude dip at the midpoint of the glide (via `sin(pi * frac)`). Mimics the natural slight weakening at the transition between vowel qualities. Set to 0 to disable. |

**Pipeline interactions:**

- **Rate compensation** runs *before* diphthong collapse, so its per-class floors don't apply to merged tokens. The collapse pass enforces its own `durationFloorMs` after merging.
- **Microprosody** (Phase 4: pre-voiceless shortening) skips diphthong glide tokens entirely. Without this, words like "eight" and "state" would have their /eɪ/ crushed before /t/. The merged token's `isDiphthongGlide` flag is checked to bypass shortening.
- **Boundary smoothing** runs *after* diphthong collapse and sees the merged token as a single segment, so consonant-to-diphthong and diphthong-to-consonant transitions use normal fade logic.

### Allophone rules

YAML-driven allophone rule engine. Replaces the older `positionalAllophones` system. See the [allophone rules reference](#allophone-rules-yaml-driven-rule-engine) earlier in this document for the full rule format.

```yaml
settings:
  allophoneRules:
    enabled: true
    rules:
      - name: american_flapping
        phonemes: [t, d]
        position: intervocalic
        stress: next-unstressed
        action: replace
        replaceTo: "ɾ"
        replaceDurationMs: 14.0
        replaceRemovesClosure: true
        replaceRemovesAspiration: true

      - name: dark_l_post_vocalic
        flags: [liquid]
        position: post-vocalic
        action: shift
        fieldShifts:
          - field: cf2
            targetHz: 900
            blend: 0.8
          - field: pf2
            targetHz: 900
            blend: 0.8

      - name: unreleased_word_final
        flags: [stop]
        notFlags: [voiced]
        position: word-final
        action: scale
        durationScale: 0.85
        fieldScales:
          fricationAmplitude: 0.4
          aspirationAmplitude: 0.3
```

### Clause-final fade
- `clauseFinalFadeMs` (number, default `0.0`): Appends a short fade-to-silence at the end of every utterance (all clause types, not just single words). This softens the hard cut at the end of sentences, particularly useful for languages/voices where stop aspiration at sentence boundaries sounds abrupt. Set to `25` for a subtle tail, or `0` (default) for the original crisp cutoff.

### Single-word tuning

When speaking isolated words or letters (e.g. spelling mode, single-word responses), the default prosody can sound abrupt or produce an odd rising "comma contour." Single-word tuning addresses this with special handling:

```yaml
settings:
  singleWordTuningEnabled: true

  # Extra hold to add to the final voiced vowel/liquid/nasal (ms at speed=1.0).
  singleWordFinalHoldMs: 45

  # Scale factor for liquid hold (R, L). Liquids can sound "pirate-y" when held
  # too long. Use 0.3–0.5 for US English to reduce the exaggerated R sound.
  singleWordFinalLiquidHoldScale: 1.0

  # Fade to silence to append after single-word utterances (ms at speed=1.0).
  singleWordFinalFadeMs: 18

  # If the caller uses clauseType ',' for continuation, use a statement contour
  # for isolated words/letters to avoid the odd "comma rise".
  singleWordClauseTypeOverride: "."
  singleWordClauseTypeOverrideCommaOnly: true
```

| Parameter | Default | Description |
|-----------|---------|-------------|
| `singleWordTuningEnabled` | `true` | Enables single-word specific prosody adjustments |
| `singleWordFinalHoldMs` | `45` | Extra hold added to the final voiced vowel/liquid/nasal (ms at speed=1.0) |
| `singleWordFinalLiquidHoldScale` | `1.0` | Scale factor for liquid (R/L) hold. Use 0.3–0.5 for US English to avoid "pirate R" |
| `singleWordFinalFadeMs` | `18` | Fade-to-silence appended after single-word utterances (ms at speed=1.0) |
| `singleWordClauseTypeOverride` | `"."` | Override clause type for isolated words/letters |
| `singleWordClauseTypeOverrideCommaOnly` | `true` | Only apply the override when caller uses clauseType `','` (avoids the odd "comma rise" on continuations) |

## How to add or tune phonemes (data.py reference + YAML packs)
`data.py` is a dictionary: keys are IPA symbols (like `a`, `ɚ`, `t͡ʃ`, `ᴒ`, etc.) and values are parameter sets that describe how the formant synthesizer should shape that sound. While the runtime path is now YAML-based, the same concepts apply. Be sure to match any tuning with the equivalent entries in `packs/phonemes.yaml`, and map usage in `packs/lang/*.yaml` so your changes are actually used.

### Adding a new phoneme (recommended workflow)
1. **Pick a key**
   - Use a real IPA symbol if possible (`ɲ`, `ʎ`, `ɨ`, …).
   - If you need a language-specific variant, use a private/internal key (we use things like `ᴒ`, `ᴇ`, `ᴀ`, `ᴐ`).
2. **Clone the closest existing phoneme**
   - Copy an existing entry and adjust it.
   - This is important: the engine expects most fields to exist. A "minimal" entry can crash.
3. **Tune it**
   - Start by adjusting formant center frequencies (`cf1`, `cf2`, `cf3`).
   - Then adjust bandwidths (`cb1`, `cb2`, `cb3`) if it sounds "boxy/ringy".
   - Only then adjust frication/aspiration settings.
4. **Wire it up in language-specific YAML**
   - Make sure `normalizeIPA()` (legacy path) or the YAML normalization rules output your new key for the right language/case.
   - If you don't map it, the phoneme will never be used.

### Parameter reference (what the fields mean)
#### Phoneme type flags (metadata)
These fields are used by timing rules and by a few special cases:
- `_isVowel`: This is a vowel (timed longer, can be lengthened with `ː`).
- `_isVoiced`: Voiced (uses `voiceAmplitude`).
- `_isStop`: Stop consonant (very short; may get a silence gap).
- `_isNasal`: Nasal consonant or nasal vowel coupling.
- `_isLiquid`: l/r-like sounds (often get longer fades).
- `_isSemivowel`: Glides like j/w (often short and transitional).
- `_isTap`, `_isTrill`: Very short rhotic types.
- `_isAfricate`: Affricate (timed like a stop+fricative).
- `_copyAdjacent`: Copy unset formant parameters from the nearest adjacent phoneme. Used for `h` and inserted post-stop aspirations so they take on the formant shape of the neighboring vowel, making them sound more natural in context.

#### Core formant synthesizer knobs
Think of a vowel as resonances (formants). The important ones are F1–F3.

**Formant center frequencies** define where the resonances are (in Hz-ish units):
- `cf1`, `cf2`, `cf3`, `cf4`, `cf5`, `cf6` — "Cascade" formant frequencies. F1–F3 matter most for vowel identity.
- `pf1`, `pf2`, `pf3`, `pf4`, `pf5`, `pf6` — "Parallel" formant frequencies. Usually matched to the `cf*` values.

Quick intuition:
- Higher `cf1` → more open mouth (e.g. "ah")
- Higher `cf2` → more front / brighter (e.g. "ee")
- Lower `cf2` → more back / rounder ("oo")
- Lower `cf3` → more "r-colored" (rhotic vowels)

**Bandwidths** define how wide each resonance is:
- `cb1..cb6` and `pb1..pb6`

Quick intuition:
- Narrow bandwidth (small numbers) → very "ringy / boxy / hollow"
- Wider bandwidth → smoother / less resonant / less "plastic box"

If something sounds "boxy", widening `cb2`/`cb3` (and matching `pb2`/`pb3`) is often the first fix.

#### Amplitude / mixing controls
- `voiceAmplitude`: Strength of voicing. Lower it slightly if vowels feel "over-held" or harsh.
- `fricationAmplitude`: Noise level for fricatives (`s`, `ʃ`, `f`, `x`, etc.). If "s" is too hissy, reduce this.
- `aspirationAmplitude`: Breath noise used for aspirated/"h-like" behavior. Usually 0 for vowels.
- `parallelBypass`: Mix control between cascade and parallel paths. Most phonemes keep this at 0.0 unless you know you need it.
- `pa1..pa6`: Per-formant amplitude in the parallel path. Most entries keep these at 0.0. If a diphthong glide is too weak, a tiny `pa2`/`pa3` boost can help.

#### Nasal coupling (optional)
Some entries include:
- `cfN0`, `cfNP`, `cbN0`, `cbNP`, `caNP`

These relate to nasal resonance and coupling. We currently treat nasality conservatively; if you don't know what to do, clone from an existing nasal vowel/consonant entry.

#### Micro-event frame emission fields

These phoneme-level fields control the fine structure of stop bursts, fricative envelopes, and voiced closures. They live in `packs/phonemes.yaml` alongside the formant data. All default to 0.0 (engine uses built-in defaults when unset).

**Stop burst shaping:**
- `burstDurationMs` (number): Length of the burst noise in milliseconds. Longer bursts give velars their characteristic "compact" quality. Typical values: alveolar 5-7 ms, velar 10-12 ms.
- `burstDecayRate` (number, 0.0-1.0): How quickly burst energy decays. 0.0 = flat (no decay), 1.0 = instant cutoff. Velars use ~0.4 (slow decay), alveolars ~0.5.
- `burstSpectralTilt` (number): Tilts the burst spectrum. Negative values boost high-frequency energy (brighter burst). Used on velars (-0.15) for the characteristic "compact spectrum" sound. This works by modifying parallel amplitudes pa3-pa6 directly in the burst micro-frame.

**Voiced closure (voice bar):**
- `voiceBarAmplitude` (number, 0.0-1.0): Voicing level during stop closure. Higher values give a stronger murmur before the burst. Typical: 0.3 for voiced stops. Set to 0 for voiceless stops.
- `voiceBarF1` (number, Hz): F1 frequency during closure. Low values (120-180 Hz) give the characteristic "muffled" voice bar sound.

**Fricative envelope:**
- `fricAttackMs` (number): Onset ramp time for frication noise. Controls how quickly friction builds. Typical: 3 ms. Skipped in post-stop clusters (/ks/, /ts/) where the stop burst leads directly into frication.
- `fricDecayMs` (number): Offset ramp time for frication noise. Controls how gradually friction fades. Typical: 4 ms. Skipped for affricates where frication sustains through the release.

**Release and timing:**
- `releaseSpreadMs` (number): Duration of aspiration ramp-in after stop release (for tokens marked as post-stop aspiration). Typical: 4 ms.
- `durationScale` (number, default 1.0): Per-phoneme duration multiplier applied to the class-default duration for stops, affricates, and their closures. Values > 1.0 make the phoneme longer. Example: velars /k/,/ɡ/ use 1.3 (larger oral cavity needs more closure time), labials /p/,/b/ use 1.1, alveolars default to 1.0.

**Example:**
```yaml
phonemes:
  k:
    _isStop: true
    burstDurationMs: 11
    burstDecayRate: 0.4
    burstSpectralTilt: -0.15
    durationScale: 1.3
    voiceBarAmplitude: 0     # voiceless — no voice bar
    voiceBarF1: 150
  g:
    _isStop: true
    _isVoiced: true
    burstDurationMs: 11
    burstDecayRate: 0.4
    burstSpectralTilt: -0.15
    durationScale: 1.3
    voiceBarAmplitude: 0.3   # voiced — audible murmur
    voiceBarF1: 150
```

### Practical tuning tips (fast wins)
"This vowel sounds too much like another vowel"
- Adjust `cf1` and `cf2` first.
- Example: Hungarian short `a` vs long `á`: make short `a` lower `cf1` and lower `cf2` compared to `á`.

"This vowel is boxy / hollow / plastic"
- Widen `cb2`/`cb3` (and `pb2`/`pb3`) a bit.

"This sound is too sharp/hissy"
- Lower `fricationAmplitude`.

"This rhotic vowel (`ɚ`/`ɝ`) is too thick"
- Raise `cf3` slightly (less r-color) or widen `cb3`.

## Adding or tuning phonemes (recommended workflow)
1. Add or adjust a phoneme in `packs/phonemes.yaml`
   - Clone the closest phoneme and tweak it.
   - Keep type flags consistent (`_isVowel`, `_isStop`, `_isVoiced`, etc.) because timing rules rely on them.
2. Map it into use via language normalization
   - Add replacements/aliases in `packs/lang/<lang>.yaml` so the phoneme appears in the output stream.
3. Test
   - Compare a few representative words/phrases in NVDA across languages/dialects.

## Adding a new language
1. Create `packs/lang/<lang>.yaml`
2. Add any IPA normalizations needed for that language
3. Add or tune phonemes in `packs/phonemes.yaml` (or add internal phoneme keys)
4. If the language has special prosody needs:
   - start by adjusting settings (stress scaling, closure behavior)
   - only add new engine behavior if the data model truly can't express it

## Tonal languages (Chinese, Vietnamese, etc.)
Tonal languages are supported by treating tone as an optional overlay on top of the existing pitch model:
- enable `settings.tonal: true`
- define tone contours (digits or tone letters) in the language pack

This lets new tonal behavior be added mostly as YAML, while still allowing deeper future work (tone sandhi) if needed.

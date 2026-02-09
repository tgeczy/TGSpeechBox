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
- `primaryStressDiv` (number, default `1.4`): Slows down the syllable carrying primary stress. Higher = more slowdown.
- `secondaryStressDiv` (number, default `1.1`): Slows down secondary stress syllables.

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

### Positional allophones (optional)

This pass applies small, position-based tweaks without needing new phonemes in your pack.

Nested block form (preferred):

```yaml
settings:
  positionalAllophones:
    enabled: true

    # Applies to the inserted post-stop aspiration phoneme (see `postStopAspirationEnabled`)
    stopAspiration:
      wordInitialStressed: 0.8
      wordInitial: 0.5
      intervocalic: 0.2
      wordFinal: 0.1

    # /l/ -> more [ɫ]-like as it moves later in the syllable/word
    lateralDarkness:
      preVocalic: 0.2
      postVocalic: 0.8
      syllabic: 0.9

    # Simple target used for the F2 pull (Hz)
    lateralDarkF2Target: 900

    # Insert /ʔ/ before word-final voiceless stops in selected contexts
    glottalReinforcement:
      enabled: false
      contexts: ["V_#"]   # "vowel before word-final stop"

    # Inserted /ʔ/ duration in ms (at speed=1)
    glottalReinforcementDurationMs: 18
```

Flat-key equivalents are also supported:

- `positionalAllophonesEnabled`
- `positionalAllophonesStopAspirationWordInitialStressed`
- `positionalAllophonesStopAspirationWordInitial`
- `positionalAllophonesStopAspirationIntervocalic`
- `positionalAllophonesStopAspirationWordFinal`
- `positionalAllophonesLateralDarknessPreVocalic`
- `positionalAllophonesLateralDarknessPostVocalic`
- `positionalAllophonesLateralDarknessSyllabic`
- `positionalAllophonesLateralDarkF2TargetHz`
- `positionalAllophonesGlottalReinforcementEnabled`
- `positionalAllophonesGlottalReinforcementDurationMs`

Notes:
- Aspiration scaling only has an effect if `postStopAspirationEnabled` is already inserting aspiration tokens.
- Glottal reinforcement requires the phoneme **`ʔ`** to exist in `phonemes.yaml`.

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

### Normalization cleanup
- `stripAllophoneDigits` (bool, default `true`): Removes eSpeak allophone digits from IPA streams (disable for tonal digit languages if needed).
- `stripHyphen` (bool, default `true`): Removes hyphens in IPA streams.

### Pitch model selection

The frontend supports three pitch models, selected via `legacyPitchMode`:

- `legacyPitchMode: "espeak_style"` (default) — Table-based intonation model (same contours as `ipa.py`).
- `legacyPitchMode: "legacy"` — Older time-based pitch curves ported from the ee80f4d-era `ipa.py`.
- `legacyPitchMode: "fujisaki_style"` — Fujisaki pitch model with natural declination and accent peaks. This provides Eloquence-like intonation with phrase-level pitch fall and stressed syllable peaks.

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

**Declination settings (linear pitch fall across utterance):**
- `fujisakiDeclinationScale` (number, default `25.0`): How fast pitch falls. Lower = gentler slope.
- `fujisakiDeclinationMax` (number, default `1.25`): Maximum declination ratio (floor). 1.25 means pitch can't drop below ~80% of base. Lower values = shallower floor.
- `fujisakiDeclinationPostFloor` (number, default `0.15`): Continued slope after hitting floor (0-1). 0 = flat after floor, 0.15 = continue at 15% rate.

**Clause-type prosody:**
The Fujisaki model automatically adjusts prosody based on punctuation:
- `.` (period): Full declarative fall (default behavior)
- `?` (question): Higher pitch, less declination, strong final rise
- `!` (exclamation): Punchy accents, dramatic fall
- `,` (comma): Less declination, continuation feel

Example configuration for Eloquence-like prosody:
```yaml
settings:
  legacyPitchMode: "fujisaki_style"
  fujisakiPhraseAmp: 0.24
  fujisakiPrimaryAccentAmp: 0.24
  fujisakiSecondaryAccentAmp: 0.12
  fujisakiDeclinationScale: 25.0
  fujisakiDeclinationMax: 1.25
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
  coarticulationVelarF2Locus: 2200
```

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

### Boundary smoothing

Boundary smoothing increases crossfade time only at "harsh" boundaries (for example vowel→stop, stop→vowel, vowel→fricative). It's a simple way to reduce micro-clicks and make syllables feel less separated without changing phoneme targets.

The pass uses internally tuned fade values for each boundary type (vowel→stop 22ms, stop→vowel 20ms, vowel→fricative 18ms, etc.) and exposes per-formant transition scaling and plosive/nasal behavior controls.

```yaml
settings:
  boundarySmoothing:
    enabled: true

    # Per-formant transition scaling (multipliers on base fade values).
    # F1 should transition faster (place perception), F2/F3 slower.
    f1Scale: 0.6    # F1 fades are 60% of base
    f2Scale: 1.0    # F2 at base
    f3Scale: 1.2    # F3 slightly slower (smoother quality)

    # Plosive/nasal-specific transition behavior.
    plosiveSpansPhone: true       # formants ramp across entire plosive burst
    nasalF1Instant: true          # F1 jumps nearly instantly at nasal boundaries
    nasalF2F3SpansPhone: true     # F2/F3 ramp across entire nasal
```

Flat-key equivalents are also supported:

- `boundarySmoothingEnabled`
- `boundarySmoothingF1Scale`
- `boundarySmoothingF2Scale`
- `boundarySmoothingF3Scale`
- `boundarySmoothingPlosiveSpansPhone`
- `boundarySmoothingNasalF1Instant`
- `boundarySmoothingNasalF2F3SpansPhone`

Tuning notes:
- If speech starts sounding "mushy", lower `f3Scale` first (or try `f2Scale: 0.8`).
- `nasalF1Instant: true` is important for natural nasal transitions — F1 jumps sharply at the velum opening/closing.
- The per-formant scales multiply the pass's internal fade values, so `f1Scale: 0.6` means F1 transitions happen in 60% of the time that F2 uses.

### Trajectory limiting

Trajectory limiting caps how fast key formants are allowed to move across token boundaries (best applied to **F2/F3** first). If a transition would require an extremely fast jump, the frontend spreads the change by increasing the boundary crossfade (up to `windowMs`).

```yaml
settings:
  trajectoryLimit:
    enabled: true
    applyTo: [cf2, cf3]     # start here (F2/F3)

    # Maximum allowed change rate in Hz per ms (at speed=1.0)
    maxHzPerMs:
      cf2: 18
      cf3: 22

    # Upper cap on how much we are allowed to "spread" a change (ms at speed=1.0)
    windowMs: 25

    # If false, do not apply the limiter across word boundaries.
    applyAcrossWordBoundary: false
```

Tuning notes:
- Smaller `maxHzPerMs` = smoother transitions (but too small can blur consonant identity).
- Larger `windowMs` = the limiter has more room to soften big jumps (try 30–40ms if you want fewer "corners").
- If you want the effect only inside words, keep `applyAcrossWordBoundary: false`.

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
```

### Microprosody

Microprosody adds small F0 perturbations around consonant→vowel boundaries (e.g. voiceless consonants slightly raise the following vowel onset). This is subtle but can add a more "human" feel.

```yaml
settings:
  microprosody:
    enabled: true

    voicelessF0Raise:
      enabled: true
      deltaHz: 12
      endDeltaHz: 0

    voicedF0Lower:
      enabled: true
      deltaHz: 6

    # Avoid overdoing this on very short vowels.
    minVowelMs: 25
```

### Rate-dependent reduction

At high speech rates, some distinctions are better represented as reduction rather than "perfect" articulation. Start conservative.

```yaml
settings:
  rateReduction:
    enabled: true

    # Schwas become shorter/weaker beyond this speed multiplier.
    schwaReductionThreshold: 2.5
    schwaMinDurationMs: 15
    schwaScale: 0.80
```

### Word-final schwa reduction

Independent of speech rate, word-final schwas can optionally be shortened. This is useful for languages like Danish, German, French, and Portuguese where unstressed final syllables are naturally reduced.

```yaml
settings:
  wordFinalSchwaReductionEnabled: true
  wordFinalSchwaScale: 0.6           # 0.0–1.0, lower = shorter
  wordFinalSchwaMinDurationMs: 8.0   # floor to avoid total silence
```

Unlike rate-dependent schwa reduction (which activates at high speeds), this applies at all speech rates.

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

### Positional allophones

Systematic, position-based allophones (aspiration scaling, /l/ darkness, optional glottal reinforcement of final stops).

```yaml
settings:
  positionalAllophones:
    enabled: false

    stopAspiration:
      wordInitialStressed: 0.80
      wordInitial: 0.50
      intervocalic: 0.20
      wordFinal: 0.10

    lateralDarkness:
      preVocalic: 0.20
      postVocalic: 0.80
      syllabic: 0.90

    # Optional /l/ darkness target (F2 pull).
    lateralDarkF2Target: 900

    glottalReinforcement:
      enabled: true
      contexts: ["#_#", "V_#"]
      durationMs: 18
```

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

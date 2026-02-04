# NV Speech Player

A Klatt-based speech synthesis engine written in C++.

Author: Original project by NV Access Limited. This repository is maintained as a community fork by Tamas Geczy.

### Acknowledgments
Special thanks to:
- **Rommix** for extensive testing, feedback, and collaboration on the Fujisaki pitch model implementation and DSP timing parameters.

## Project status (fork + naming)
NV Access is not actively maintaining the original NV Speech Player project.

This repository is an independently maintained fork. It is **not affiliated with, endorsed by, or supported by NV Access**.

### About the name
The “NV Speech Player” name is kept here for historical continuity and to help existing users find the project they already know. It should not be interpreted as NV Access involvement.

### NVDA / trademarks
This project includes an NVDA add-on and necessarily refers to NVDA for compatibility and documentation. NVDA and related names/marks belong to their respective owners. This fork does not claim any official relationship.

### Note on eSpeak-ng
The eSpeak-ng project also contains a separate copy of SpeechPlayer code as an alternative Klatt-style implementation. This fork is independent of that copy.
## Overview
NV Speech Player is a free and open-source prototype speech synthesizer that can be used by NVDA. It generates speech using Klatt synthesis, making it somewhat similar to speech synthesizers such as Dectalk and Eloquence.

Historically, NV Speech Player relied on Python (`ipa.py`) to:
- normalize phonemes (mostly from eSpeak IPA),
- apply language/dialect rules,
- generate timed frame tracks and intonation,
- and feed frames into the C++ DSP engine.

This repo has now transitioned to a new **frontend + YAML packs** model that replaces the Python IPA pipeline for runtime use.

## License and copyright
NV Speech Player is Copyright (c) 2014 NV Speech Player contributors.

NV Speech Player is covered by the GNU General Public License (Version 2).

You are free to share or change this software in any way you like as long as it is accompanied by the license and you make all source code available to anyone who wants it. This applies to both original and modified copies of this software, plus any derivative works.

For further details, you can view the license online at:
http://www.gnu.org/licenses/old-licenses/gpl-2.0.html

## Background
The 70s and 80s saw much research in speech synthesis. One of the most prominent synthesis models that appeared was a formant-frequency synthesis known as Klatt synthesis. Some well-known Klatt synthesizers are Dectalk and Eloquence. They are well suited for use by the blind because they are responsive, predictable, and small in memory footprint.

Research later moved toward other synthesis approaches because they can sound closer to a human voice, but they often trade away responsiveness and predictability.

NV Speech Player exists as a modern prototype of a Klatt synthesizer:
- to explore the “classic” fast-and-stable sound profile,
- and to keep conversation and experimentation alive around this synthesis method.

## Repository layout
At a high level, the project is split into two layers:

1. **DSP engine (C++)**: `speechPlayer.dll`
2. **Frontend (C++)**: `nvspFrontend.dll` + YAML packs in `./packs`

The NVDA add-on loads both DLLs and uses eSpeak for text → IPA conversion.

### packs directory (important)
Language packs live in this repository at:

`./packs`

This folder is meant to be included in the NVDA add-on distribution as well.

Current structure:
- `packs/phonemes.yaml`
- `packs/lang/default.yaml`
- `packs/lang/en.yaml`, `packs/lang/en-us.yaml`, etc.

## DSP pipeline internals (speechPlayer.cpp + speechWaveGenerator.cpp)
At the highest level, `speechPlayer.cpp` wires together the frame queue and the DSP generator:
- `speechPlayer_initialize()` builds a `FrameManager` plus a `SpeechWaveGenerator` and connects them so the generator can pull the current frame data as it produces audio samples.
- `speechPlayer_queueFrame()` pushes time-aligned frame data into the `FrameManager`, including minimum frame duration, fade time, and a user index for tracking.
- `speechPlayer_synthesize()` asks the wave generator for the next block of samples, which is where all the DSP happens.
- `speechPlayer_getLastIndex()` lets the caller know which queued frame index was last consumed by the renderer.

The DSP pipeline lives in `speechWaveGenerator.cpp` and is executed once per output sample:
1. **Frame selection and interpolation:** `FrameManager::getCurrentFrame()` returns the current frame, or interpolates between frames using the configured fade time. This is how crossfades, pitch glides, and silence frames work.
2. **Source generation (voicing + aspiration):**
   - `VoiceGenerator` turns `voicePitch` into a periodic waveform, applies vibrato, and mixes in turbulence.
   - `aspirationAmplitude` adds breath noise.
3. **Cascade formant path:** The voiced source is shaped by a cascade of resonators (`cf1..cf6` with `cb1..cb6`), with optional nasal coupling (`cfN0/cfNP`, `cbN0/cbNP`, `caNP`).
4. **Parallel frication path:** A separate noise source (`fricationAmplitude`) is passed through parallel resonators (`pf1..pf6`, `pb1..pb6`, `pa1..pa6`). `parallelBypass` mixes raw noise against the resonated output.
5. **Mix and scale:** Cascade + parallel outputs are mixed, scaled by `preFormantGain` and `outputGain`, and clipped to 16-bit range before being returned to the caller.

This structure keeps the time-domain synthesis logic entirely in C++: callers provide timed frame tracks, while the engine interpolates and renders them into audio.

## New Speech Player exports (voicing tone control)

The `speechPlayer.dll` now exports additional functions for real-time control of the voice source characteristics. These allow callers to adjust the "voicing tone" — the spectral shape and character of the glottal pulse — without modifying individual frames.

### New API functions

- `speechPlayer_setVoicingTone(handle, VoicingTone*)` — Apply a new voicing tone configuration.
- `speechPlayer_getVoicingTone(handle, VoicingTone*)` — Retrieve the current voicing tone settings.
- `speechPlayer_hasVoicingToneSupport(handle)` — Check if the DLL supports voicing tone control (for backward compatibility).

### VoicingTone struct parameters

The `VoicingTone` struct contains parameters that shape the voiced sound source. As of DSP version 6, there are **12 parameters** plus a version detection header.

#### Version detection (v3 struct)

The v3 struct includes a header for backward compatibility:

| Field | Type | Description |
|-------|------|-------------|
| `magic` | uint32 | Magic number `0x32544F56` ("VOT2" in little-endian) |
| `structSize` | uint32 | Size of the struct in bytes |
| `structVersion` | uint32 | Struct version (currently 3) |
| `dspVersion` | uint32 | DSP version (currently 6) |

When the DLL receives a `VoicingTone` struct, it checks the magic number:
- **If magic matches**: Uses the full v3 struct with all 12 parameters
- **If magic doesn't match**: Assumes legacy v1 layout (7 doubles) and applies defaults for new parameters

This allows older drivers to continue working with newer DLLs, and vice versa.

#### Core parameters (original 7)

| Parameter | Type | Default | Range | Description |
|-----------|------|---------|-------|-------------|
| `voicingPeakPos` | double | 0.91 | 0.85–0.95 | Glottal pulse peak position. Lower values create a more pressed/tense voice quality; higher values sound more breathy/relaxed. |
| `voicedPreEmphA` | double | 0.92 | 0.0–0.97 | Pre-emphasis filter coefficient. Higher values boost high frequencies before formant filtering. |
| `voicedPreEmphMix` | double | 0.35 | 0.0–1.0 | Mix between original and pre-emphasized signal. 0.0 = no pre-emphasis, 1.0 = full pre-emphasis. |
| `highShelfGainDb` | double | 4.0 | -12 to +12 | High-shelf EQ gain in dB. Positive values brighten the output; negative values darken it. |
| `highShelfFcHz` | double | 2000.0 | 500–8000 | High-shelf corner frequency in Hz. Controls where the shelf boost/cut begins. |
| `highShelfQ` | double | 0.7 | 0.3–2.0 | High-shelf Q factor. Higher values create a more resonant shelf transition. |
| `voicedTiltDbPerOct` | double | 0.0 | -24 to +24 | Spectral tilt in dB/octave. Negative values create a brighter sound (less natural roll-off); positive values create a darker, more muffled sound. |

#### V2 parameters (DSP version 4+)

| Parameter | Type | Default | Range | Description |
|-----------|------|---------|-------|-------------|
| `noiseGlottalModDepth` | double | 0.0 | 0.0–1.0 | Modulates noise sources (aspiration, frication) by the glottal cycle. Higher values create more "buzzy" noise that pulses with voicing. |
| `pitchSyncF1DeltaHz` | double | 0.0 | -60 to +60 | Pitch-synchronous F1 modulation. During the glottal open phase, F1 is shifted by this amount. Can add naturalness or remove subtle clicks. |
| `pitchSyncB1DeltaHz` | double | 0.0 | -50 to +50 | Pitch-synchronous B1 (F1 bandwidth) modulation. During the glottal open phase, B1 is widened by this amount. Works with `pitchSyncF1DeltaHz`. |

#### V3 parameters (DSP version 5+)

| Parameter | Type | Default | Range | Description |
|-----------|------|---------|-------|-------------|
| `speedQuotient` | double | 2.0 | 0.5–4.0 | Glottal pulse asymmetry (ratio of opening to closing time). Lower values (0.5–1.5) create softer, more female-like voices. Higher values (2.5–4.0) create sharper, more male/pressed voices. Default 2.0 matches original behavior. |
| `aspirationTiltDbPerOct` | double | 0.0 | -12 to +12 | Spectral tilt for aspiration/breath noise in dB/octave. Negative values make breath brighter; positive values make it darker/more muffled. Independent of `voicedTiltDbPerOct`. |

### FrameEx struct (DSP version 5+)

DSP version 5 introduced an optional per-frame extension struct for voice quality parameters that vary during speech (e.g., Danish stød). DSP version 6 adds formant end targets and Fujisaki pitch model parameters. This keeps the original 47-parameter frame ABI stable.

#### New API function

- `speechPlayer_queueFrameEx(handle, frame*, frameEx*, frameExSize, minDuration, fadeDuration, userIndex, purgeQueue)` — Queue a frame with optional extended parameters. The `frameExSize` parameter enables forward compatibility. If `frameEx` is NULL, behaves identically to `speechPlayer_queueFrame()`.

#### FrameEx parameters

##### Voice quality (DSP v5+)

| Parameter | Type | Default | Range | Description |
|-----------|------|---------|-------|-------------|
| `creakiness` | double | 0.0 | 0.0–1.0 | Laryngealization / creaky voice. Used for Danish stød and similar phonation types. Adds pitch irregularity, amplitude reduction, and tighter glottal closure. |
| `breathiness` | double | 0.0 | 0.0–1.0 | Additional voiced breathiness. Increases turbulence during the open glottal phase independently of `voiceTurbulenceAmplitude`. |
| `jitter` | double | 0.0 | 0.0–1.0 | Pitch perturbation (cycle-to-cycle F0 variation). Adds random pitch wobble for more natural or pathological voice quality. |
| `shimmer` | double | 0.0 | 0.0–1.0 | Amplitude perturbation (cycle-to-cycle intensity variation). Adds random amplitude wobble. |
| `sharpness` | double | 0.0 | 0.0–15.0 | Glottal closure sharpness multiplier. When non-zero, multiplies the sample-rate-appropriate base sharpness (e.g., 10.0 at 44100 Hz, 3.0 at 16000 Hz). Values 0.5–2.0 are typical slider range. 0 = use default. Higher values create crisper, more "Eloquence-like" attacks. |

##### Formant end targets (DSP v6+)

These enable DECTalk-style within-frame formant ramping for smoother CV transitions:

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `endCf1` | double | NAN | End target for cascade F1 (Hz). NAN = no ramping. |
| `endCf2` | double | NAN | End target for cascade F2 (Hz). NAN = no ramping. |
| `endCf3` | double | NAN | End target for cascade F3 (Hz). NAN = no ramping. |
| `endPf1` | double | NAN | End target for parallel F1 (Hz). NAN = no ramping. |
| `endPf2` | double | NAN | End target for parallel F2 (Hz). NAN = no ramping. |
| `endPf3` | double | NAN | End target for parallel F3 (Hz). NAN = no ramping. |

##### Fujisaki pitch model (DSP v6+)

The Fujisaki pitch model provides natural-sounding intonation with phrase-level declination and accent peaks. This is used when `legacyPitchMode: "fujisaki_style"` is set in the language pack.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `fujisakiEnabled` | double | 0.0 | 1.0 to enable Fujisaki processing, 0.0 to disable. |
| `fujisakiReset` | double | 0.0 | 1.0 to reset model state (at clause start). |
| `fujisakiPhraseAmp` | double | 0.0 | Phrase command amplitude (log-F0 domain). Triggers phrase arc on rising edge. |
| `fujisakiPhraseLen` | double | 0.0 | Phrase filter length in samples. 0 = use DSP default (~4250 @ 22050 Hz). |
| `fujisakiAccentAmp` | double | 0.0 | Accent command amplitude (log-F0 domain). Triggers accent peak on rising edge. |
| `fujisakiAccentDur` | double | 0.0 | Accent pulse duration in samples. 0 = use DSP default (~7500 @ 22050 Hz). |
| `fujisakiAccentLen` | double | 0.0 | Accent filter length in samples. 0 = use DSP default (~1024 @ 22050 Hz). |

These parameters are interpolated during frame crossfades, just like the core frame parameters.

### Usage example (Python/ctypes, v3 struct)

```python
from ctypes import Structure, c_double, c_uint32, c_void_p, POINTER, byref, sizeof

# Constants (must match voicingTone.h)
SPEECHPLAYER_VOICINGTONE_MAGIC = 0x32544F56  # "VOT2"
SPEECHPLAYER_VOICINGTONE_VERSION = 3

class VoicingTone(Structure):
    _fields_ = [
        # Version detection header
        ("magic", c_uint32),
        ("structSize", c_uint32),
        ("structVersion", c_uint32),
        ("dspVersion", c_uint32),
        # Original parameters
        ("voicingPeakPos", c_double),
        ("voicedPreEmphA", c_double),
        ("voicedPreEmphMix", c_double),
        ("highShelfGainDb", c_double),
        ("highShelfFcHz", c_double),
        ("highShelfQ", c_double),
        ("voicedTiltDbPerOct", c_double),
        # V2 parameters
        ("noiseGlottalModDepth", c_double),
        ("pitchSyncF1DeltaHz", c_double),
        ("pitchSyncB1DeltaHz", c_double),
        # V3 parameters
        ("speedQuotient", c_double),
        ("aspirationTiltDbPerOct", c_double),
    ]
    
    @classmethod
    def defaults(cls):
        tone = cls()
        tone.magic = SPEECHPLAYER_VOICINGTONE_MAGIC
        tone.structSize = sizeof(cls)
        tone.structVersion = SPEECHPLAYER_VOICINGTONE_VERSION
        tone.dspVersion = 6
        tone.voicingPeakPos = 0.91
        tone.voicedPreEmphA = 0.92
        tone.voicedPreEmphMix = 0.35
        tone.highShelfGainDb = 4.0
        tone.highShelfFcHz = 2000.0
        tone.highShelfQ = 0.7
        tone.voicedTiltDbPerOct = 0.0
        tone.noiseGlottalModDepth = 0.0
        tone.pitchSyncF1DeltaHz = 0.0
        tone.pitchSyncB1DeltaHz = 0.0
        tone.speedQuotient = 2.0
        tone.aspirationTiltDbPerOct = 0.0
        return tone

# Set up function prototypes
dll.speechPlayer_setVoicingTone.argtypes = [c_void_p, POINTER(VoicingTone)]
dll.speechPlayer_setVoicingTone.restype = None

# Create and configure tone
tone = VoicingTone.defaults()
tone.voicedTiltDbPerOct = -6.0  # Brighter tilt (Eloquence-like)
tone.speedQuotient = 1.2  # Softer, more female-like pulse

# Apply to running synthesizer
dll.speechPlayer_setVoicingTone(handle, byref(tone))
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

### NVDA driver sliders

The driver exposes 10 sliders for real-time voice tuning:

#### VoicingTone sliders (global voice character)

| Slider | Range | Default | Maps to |
|--------|-------|---------|---------|
| Voice tilt (brightness) | 0–100 | 50 | `voicedTiltDbPerOct` (-24 to +24 dB/oct) |
| Noise glottal modulation | 0–100 | 0 | `noiseGlottalModDepth` (0.0–1.0) |
| Pitch-sync F1 delta | 0–100 | 50 | `pitchSyncF1DeltaHz` (-60 to +60 Hz) |
| Pitch-sync B1 delta | 0–100 | 50 | `pitchSyncB1DeltaHz` (-50 to +50 Hz) |
| Speed quotient | 0–100 | 50 | `speedQuotient` (0.5–4.0) |

#### FrameEx sliders (per-frame voice quality)

| Slider | Range | Default | Maps to |
|--------|-------|---------|---------|
| Creakiness | 0–100 | 0 | `creakiness` (0.0–1.0) |
| Breathiness | 0–100 | 0 | `breathiness` (0.0–1.0) |
| Jitter | 0–100 | 0 | `jitter` (0.0–1.0) |
| Shimmer | 0–100 | 0 | `shimmer` (0.0–1.0) |
| Glottal sharpness | 0–100 | 50 | `sharpness` (0.5–2.0 multiplier) |

Note: Sliders centered at 50 (tilt, pitch-sync, speed quotient, sharpness) have meaningful positive and negative ranges. Sliders starting at 0 (noise modulation, creakiness, breathiness, jitter, shimmer) are effect amounts that default to "off".

All slider values are stored per-voice, so different voice profiles can have different settings.

## The new frontend model (nvspFrontend.dll + YAML packs)
The new frontend replaces the Python IPA runtime pipeline. It is designed so that language changes can happen as data (YAML) rather than code.

### What the frontend does
Given an IPA string and a language tag, the frontend:
- normalizes IPA using YAML rules (`packs/lang/*.yaml`)
- tokenizes IPA (stress marks, length marks, tie bars, etc.)
- applies timing rules (vowels vs stops vs affricates, stress scaling, length marks)
- applies intonation and pitch shaping
- emits timed frames compatible with `speechPlayer_queueFrame()`

### Why YAML packs matter
YAML packs are intended to be community-friendly:
- easier to read and review than embedded code,
- allows language/dialect refinements without recompiling the DLL,
- keeps “phoneme sound definitions” separate from “language mapping rules”.

This also makes it easier to share improvements:
- phoneme tuning in `phonemes.yaml`,
- language and dialect behavior in `packs/lang/<lang>.yaml` and `packs/lang/<lang-region>.yaml`.

## Advanced Multilingual Tokenization

The C++ frontend features a **greedy longest-match tokenizer** designed for robust multilingual support. This allows language packs to define multi-character phonemes of arbitrary length without requiring tie bars in the phoneme keys.

### Why This Matters

Different languages represent sounds in fundamentally different ways:

| Language | Challenge | Solution |
|----------|-----------|----------|
| **Russian** | Palatalized consonants (`nʲ`, `lʲ`, `mʲ`) are single phonemic units | Greedy matching finds `nʲ` as one token, not `n` + `ʲ` |
| **Polish** | Complex affricates and palatalization | Multi-character keys like `t͡ɕ`, `d͡ʑ` match correctly |
| **Hungarian** | Geminate consonants, unique vowels | Proper handling of length marks and diacritics |
| **English** | Syllabic L/R in words like "bottle", "butter" | Normalization splits `ə͡l` → `ə` + `l` for proper lateral quality |

### Directional Tie-Bar Flexibility

The tokenizer intelligently handles IPA tie bars (◌͡◌) with **directional matching**:

- ✅ Input `n͡ʲ` matches pack key `nʲ` — extra tie bars in input are ignored
- ✅ Input `nʲ` matches pack key `nʲ` — exact matches always work  
- ❌ Input `əl` won't match pack key `ə͡l` — respects normalization decisions

This means:
- **Pack authors** can define phonemes with or without tie bars as they prefer
- **eSpeak variations** in tie bar usage are handled gracefully
- **Normalization rules** (like splitting syllabic L for English) are respected

### Thread-Safe Design

The sorted phoneme key list is computed once during pack loading and stored immutably in the `PackSet` structure, making the tokenizer fully thread-safe for concurrent speech synthesis.
### Language pack loading and inheritance
The loader merges packs in this order:
1. `packs/lang/default.yaml`
2. `packs/lang/<lang>.yaml` (example: `en.yaml`)
3. `packs/lang/<lang-region>.yaml` (example: `en-us.yaml`)
4. optional deeper variants (example: `en-us-nyc.yaml`)

This is how dialect differences can be expressed even when upstream IPA does not mark them clearly.

### phonemes.yaml
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

#### Voice profiles (optional)
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

##### Class names you can use
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

##### Supported scale fields
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

##### Per-phoneme overrides (`phonemeOverrides`)

Use `phonemeOverrides` when your profile is *mostly* right, but **one specific phoneme** still sounds off.

**What it does**
- Values in `phonemeOverrides` are **absolute assignments** for that phoneme (not multipliers).
- Overrides are applied **after** `classScales`.
- If an override sets a field, it **wins for that field only**. Other fields still come from the base phoneme + any class scaling.

**Effective order (per field)**
1. Start with the phoneme’s base value from `phonemes:`
2. Apply any matching `classScales` multipliers
3. Apply `phonemeOverrides` (only for the fields you specify)

**Tip**  
Override the **smallest number of fields** you can (often just `cf2`, or `cf1 + cf2` on one vowel).  
If you’re overriding lots of phonemes, it usually means your `classScales` need tuning.

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
##### How to enable a profile
Preferred: select a profile at runtime via the frontend API:
```c
// Enable
nvspFrontend_setVoiceProfile(handle, "female");

// Disable (back to base phonemes)
nvspFrontend_setVoiceProfile(handle, "");

// Read current profile ("" if none)
const char* profile = nvspFrontend_getVoiceProfile(handle);
```

##### Debugging: pack load warnings
If a profile isn’t taking effect (typo, YAML error, etc.), read the pack warnings:
```c
const char* warnings = nvspFrontend_getPackWarnings(handle);
if (warnings && warnings[0] != '\0') {
    // show or log warnings
}
```


### Language YAML example (en.yaml)
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

### Language pack settings reference
Below is a reference for language pack `settings` values used by the frontend.

#### Stress and timing
- `primaryStressDiv` (number, default `1.4`): Slows down the syllable carrying primary stress. Higher = more slowdown.
- `secondaryStressDiv` (number, default `1.1`): Slows down secondary stress syllables.

#### Stop closure insertion
These settings control the short “silence gap” inserted before stops/affricates (helps clarity, especially for consonant clusters).
- `stopClosureMode` (string, default `"vowel-and-cluster"`): One of:
  - `"always"`: insert a closure gap before every stop/affricate (clearest, can sound clicky)
  - `"after-vowel"`: only after vowels
  - `"vowel-and-cluster"`: after vowels, and also in some clusters (balanced)
  - `"none"`: never insert closure gaps
- `stopClosureClusterGapsEnabled` (bool, default `true`): Enables the cluster part of `"vowel-and-cluster"`.
- `stopClosureAfterNasalsEnabled` (bool, default `false`): If true, allows closure gaps before stops even after nasals. Helpful when nasal+stop clusters swallow the stop at higher rates.

#### Stop closure gap timing (ms at speed=1.0)
- `stopClosureVowelGapMs` (number, default `41.0`)
- `stopClosureVowelFadeMs` (number, default `10.0`)
  - Duration/fade of closure gaps inserted after vowels.
- `stopClosureClusterGapMs` (number, default `22.0`)
- `stopClosureClusterFadeMs` (number, default `4.0`)
  - Duration/fade of closure gaps inserted in clusters.
- `stopClosureWordBoundaryClusterGapMs` (number, default `0.0`)
- `stopClosureWordBoundaryClusterFadeMs` (number, default `0.0`)
  - If set (>0), overrides the cluster gap timing only when the stop/affricate is at a word boundary (word start).

#### Segment boundary timing (between chunks)
These settings help when callers stitch speech from multiple chunks (common in NVDA UI speech: label / role / value).
- `segmentBoundaryGapMs` (number, default `0.0`)
- `segmentBoundaryFadeMs` (number, default `0.0`)
  - If non-zero, inserts a short silence frame between consecutive frontend queue calls on the same handle.
- `segmentBoundarySkipVowelToVowel` (bool, default `true`)
  - If true, skips the segment-boundary silence when a chunk ends with a vowel/semivowel and the next chunk begins with a vowel/semivowel (to avoid audible gaps across diphthongs).
- `segmentBoundarySkipVowelToLiquid` (bool, default `false`)
  - If true, also skips the segment-boundary silence when a chunk ends with a vowel/semivowel and the next chunk begins with a liquid-like consonant (liquids/taps/trills). This can help reduce audible seams in vowel+R transitions across chunks if it's noticeable in your language.

#### Automatic diphthong handling
These settings optionally add tie bars for vowel+vowel sequences that should behave like a diphthong.
- `autoTieDiphthongs` (bool, default `false`): If true, the frontend can mark eligible vowel+vowel pairs as tied (diphthongs) even when the IPA lacks a tie bar.
- `autoDiphthongOffglideToSemivowel` (bool, default `false`): If true (and `autoTieDiphthongs` is enabled), convert the diphthong offglide to a semivowel (`i/ɪ → j`, `u/ʊ → w`) when those phonemes exist.

#### Semivowel offglide shortening
If your pack represents diphthongs using a vowel+semivowel sequence (for example `eɪ → ej`), you may hear a tiny “syllable break” when that semivowel is followed by a vowel or a liquid-like consonant inside the same word (e.g. “player”, “later”).

- `semivowelOffglideScale` (number, default `1.0`)
  - If set to anything other than `1.0`, the engine multiplies the duration and fade of semivowel tokens in the pattern `vowel + semivowel + (vowel | liquid | tap | trill)`.
  - Values below `1.0` make those semivowels shorter and more “glide-like” without affecting word-final diphthongs like “day”.

#### Liquid dynamics (optional)

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

#### Intra-word stressed vowel hiatus break (spelling aid)
These settings can insert a tiny silence between two adjacent vowels when the *second* vowel is explicitly stressed. This mostly exists to help spelled-out acronyms (initialisms) where two letter names meet with no consonant in between.
- `stressedVowelHiatusGapMs` (number, default `0.0`): Gap duration in milliseconds at speed=1.0. The engine divides it by current speed.
- `stressedVowelHiatusFadeMs` (number, default `0.0`): Fade duration (crossfade) for that gap frame.

#### Spelling diphthong handling
This is a more speech-like alternative to inserting a pause. It changes how some letter-name diphthongs are rendered when the word looks like a spelled-out acronym.
- `spellingDiphthongMode` (string, default "none"): One of:
  - `none`: do nothing
  - `monophthong`: in acronym-like words, treat the English letter name **A** (/eɪ/, often pack-normalized to /ej/) as a long monophthong by dropping the offglide and marking the nucleus lengthened. This reduces the unwanted "y"-glide in cases like "NVDA".

#### Post-stop aspiration insertion (English-style)
- `postStopAspirationEnabled` (bool, default `false`): Inserts a short aspiration phoneme after unvoiced stops in specific contexts.
- `postStopAspirationPhoneme` (string/IPA key, default `"h"`): Which phoneme key to insert for aspiration.

#### Positional allophones (optional)

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

#### Length mark handling (ː)
- `lengthenedScale` (number, default `1.05`): Duration multiplier when a phoneme is lengthened with ː.
- `lengthenedScaleHu` (number, default `1.3`): Hungarian-specific length scaling (used by legacy behavior).
- `applyLengthenedScaleToVowelsOnly` (bool, default `true`): If true, ː only lengthens vowels.
- `lengthenedVowelFinalCodaScale` (number, default `1.0`): Additional multiplier for lengthened vowels (ː) when they occur in a word-final closed syllable (vowel + final consonant cluster), e.g. “rules” /ruːlz/. This is intentionally conservative: it does not apply when there are later vowels in the same word (e.g. “ruler” /ruːlə/).

#### Length contrast and gemination (optional)

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

#### Language-specific duration tweaks
These are “compat switches” for behavior that existed in the legacy Python pipeline.
- `huShortAVowelEnabled` (bool, default `true`)
- `huShortAVowelKey` (string/IPA key, default `"ᴒ"`)
- `huShortAVowelScale` (number, default `0.85`): Scales Hungarian short “a” timing (using the phoneme key you map for it).
- `englishLongUShortenEnabled` (bool, default `true`)
- `englishLongUKey` (string/IPA key, default `"u"`)
- `englishLongUWordFinalScale` (number, default `0.80`): Shortens English long /uː/ in word-final position.

#### Default frame values (applied unless phoneme sets them)
- `defaultPreFormantGain` (number, default `1.0`)
- `defaultOutputGain` (number, default `1.5`)
- `defaultVibratoPitchOffset` (number, default `0.0`)
- `defaultVibratoSpeed` (number, default `0.0`)
- `defaultVoiceTurbulenceAmplitude` (number, default `0.0`)
- `defaultGlottalOpenQuotient` (number, default `0.0`)

#### Normalization cleanup
- `stripAllophoneDigits` (bool, default `true`): Removes eSpeak allophone digits from IPA streams (disable for tonal digit languages if needed).
- `stripHyphen` (bool, default `true`): Removes hyphens in IPA streams.

#### Pitch model selection

The frontend supports three pitch models, selected via `legacyPitchMode`:

- `legacyPitchMode: "espeak_style"` (default) — Table-based intonation model (same contours as `ipa.py`).
- `legacyPitchMode: "legacy"` — Older time-based pitch curves ported from the ee80f4d-era `ipa.py`.
- `legacyPitchMode: "fujisaki_style"` — Fujisaki pitch model with natural declination and accent peaks. This provides Eloquence-like intonation with phrase-level pitch fall and stressed syllable peaks.

Additional pitch settings:

- `legacyPitchInflectionScale` (number, default `0.58`): Multiplier for the caller-provided `inflection` (0..1) before applying pitch math. Historical NVSpeechPlayer used lower inflection values; this scaling prevents overly "excited" speech with modern configs.

##### Fujisaki pitch model settings

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

#### Tonal language support
- `tonal` (bool, default `false`): Enables tone parsing / tone contours.
- `toneDigitsEnabled` (bool, default `true`): Allows digits 1–5 as tone markers.
- `toneContoursMode` (string, default `"absolute"`): How tone contour points are interpreted:
  - `"absolute"`: points are absolute 0–100 pitch-percent values
  - `"relative"`: points are offsets from the syllable baseline
- `toneContoursAbsolute` (bool, default `true`): Low-level switch used by the code; `toneContoursMode` is the recommended YAML knob.

#### Other pack sections (not in settings)

### Normalization

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

#### preReplacements vs replacements: Order Matters!

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

#### Replacement conditions (`when`)

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

`transforms`:
- Rules that match phonemes by properties (`isVowel`, `isStop`, etc.) and then modify fields:
  - `set`: set field values
  - `scale`: multiply field values
  - `add`: add offsets to fields

`intonation`:
- Clause-type pitch shapes keyed by punctuation (example keys: `.`, `?`, `!`, `:`, `;`, `,`)
- Defaults for `.`, `,`, `?`, `!` are seeded by `applyLanguageDefaults()` unless overridden by packs.
- Each clause supports percent-based parameters used by `calculatePitches()` to apply pitch paths across prehead/head/nucleus/tail:
  - `preHeadStart`, `preHeadEnd`: Pitch percentages for the prehead region (before the first stressed syllable).
  - `headStart`, `headEnd`: Pitch percentages for the head region (from first stress up to the nucleus).
  - `headSteps`: Ordered percentages that select stepped head contours on stressed syllables (example: `[100, 85, 70, 55, 40, 25, 10, 0]`).
  - `headExtendFrom`: Index in `headSteps` to clamp/extend when the head is longer than the steps list.
  - `headStressEndDelta`: Delta (in percent units) applied from a stressed syllable’s start to its end.
  - `headUnstressedRunStartDelta`, `headUnstressedRunEndDelta`: Deltas applied over unstressed runs following a stress.
  - `nucleus0Start`, `nucleus0End`: Pitch percentages for the nucleus when the clause has no tail (single-word or tail-less cases).
  - `nucleusStart`, `nucleusEnd`: Pitch percentages for the nucleus when a tail exists.
  - `tailStart`, `tailEnd`: Pitch percentages for the tail region (after the nucleus).

`toneContours`:
- Tone contour definitions (tone string → list of pitch-percent points)

## Frontend rule passes settings

Recent versions of the frontend can optionally run **modular “passes”** over the token stream (after IPA parsing, and optionally after timing / pitch). These passes are meant to encode phonetic/prosodic “rules” (in the spirit of SSRS/Delta/Eloquence) without turning the core IPA engine into a single giant function.

All settings below live under `settings:` in a language YAML (for example `packs/lang/en.yaml`). Passes are **disabled by default** unless `enabled: true` is set for that pass.

### Coarticulation

The coarticulation pass makes consonants “aim” toward a vowel-dependent locus instead of snapping to a single fixed consonant target. In practice this reduces edgy segment boundaries (especially in stop clusters) while keeping consonants intelligible at high speech rates.

```yaml
settings:
  # Master switch for this pass.
  coarticulationEnabled: true

  # Overall amount of locus pull (0.0–1.0). Higher = stronger vowel influence on consonants.
  coarticulationStrength: 0.25

  # How far into the consonant we allow the transition to extend (0.0–1.0 of the token).
  # Higher = the consonant spends more time “moving” toward the vowel target.
  coarticulationTransitionExtent: 0.35

  # If true, the coarticulation blend is allowed to start inside the consonant,
  # not only at the consonant→vowel boundary.
  coarticulationFadeIntoConsonants: true

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

These define the vowel-independent “anchor” targets the pass pulls toward for common places of articulation. Think of them as the baseline F2 goals for labial / alveolar / velar stops and related consonants.

```yaml
settings:
  coarticulationLabialF2Locus: 800
  coarticulationAlveolarF2Locus: 1800
  coarticulationVelarF2Locus: 2200
```

#### Velar pinch (optional)

Velars often sound more natural when F2 and F3 move closer (“pinch”) near high-front vowel contexts. This option applies a controlled pinch when the adjacent vowel’s F2 suggests a fronting context.

```yaml
settings:
  coarticulationVelarPinchEnabled: true

  # Only apply pinch when the neighboring vowel’s F2 is at/above this threshold.
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

#### Alveolar back-vowel enhancement

Strengthens coarticulation for alveolars before back vowels (helps /t/ sound less fronted before /u/, /o/):

```yaml
settings:
  coarticulationAlveolarBackVowelEnabled: true
  coarticulationBackVowelF2Threshold: 1400      # F2 < this = "back vowel"
  coarticulationAlveolarBackVowelStrengthBoost: 1.25
```

#### Labialized fricative fronting

Pulls F2 forward slightly for fricatives before rounded vowels:

```yaml
settings:
  coarticulationLabializedFricativeFrontingEnabled: true
  coarticulationLabializedFricativeF2Pull: 0.15
```

Tuning notes:
- If consonants start feeling “too vowel-y”, lower `coarticulationStrength` or `coarticulationTransitionExtent`.
- If clusters get mushy at speed, keep `coarticulationGraduated: true` and reduce `coarticulationAdjacencyMaxConsonants` to `1`.
- If word onsets lose bite, lower `coarticulationWordInitialFadeScale`.


### Boundary smoothing

Boundary smoothing increases crossfade time only at “harsh” boundaries (for example vowel→stop, stop→vowel, vowel→fricative). It’s a simple way to reduce micro-clicks and make syllables feel less separated without changing phoneme targets.

```yaml
settings:
  boundarySmoothing:
    enabled: true

    # Milliseconds at speed=1.0
    vowelToStopFadeMs: 12
    stopToVowelFadeMs: 10
    vowelToFricFadeMs: 6
```

Tuning notes:
- If speech starts sounding “mushy”, reduce `stopToVowelFadeMs` first.
- If you still hear sharp releases on stops, raise `vowelToStopFadeMs` a little (2–4ms steps).

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

    # Upper cap on how much we are allowed to “spread” a change (ms at speed=1.0)
    windowMs: 25

    # If false, do not apply the limiter across word boundaries.
    applyAcrossWordBoundary: false
```

Tuning notes:
- Smaller `maxHzPerMs` = smoother transitions (but too small can blur consonant identity).
- Larger `windowMs` = the limiter has more room to soften big jumps (try 30–40ms if you want fewer “corners”).
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

    # If true, apply mostly to the “nucleus” (vowel-like) portion.
    nucleusOnlyMode: true
```

### Microprosody

Microprosody adds small F0 perturbations around consonant→vowel boundaries (e.g. voiceless consonants slightly raise the following vowel onset). This is subtle but can add a more “human” feel.

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

At high speech rates, some distinctions are better represented as reduction rather than “perfect” articulation. Start conservative.

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

For languages with phonemic length, it helps to enforce floors/ceilings and make geminates behave like closures rather than just “slightly longer consonants”.

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

## Legacy Python files (kept for reference)
`ipa.py` and `data.py` still live alongside the repo for now, but they are no longer the runtime path for NVDA.

They are retained for:
- historical reference,
- comparing behavior during development,
- and as a source for generating YAML.

If you want to tune phonemes or language rules going forward, update YAML packs instead.

## How to add or tune phonemes (data.py reference + YAML packs)
`data.py` is a dictionary: keys are IPA symbols (like `a`, `ɚ`, `t͡ʃ`, `ᴒ`, etc.) and values are parameter sets that describe how the formant synthesizer should shape that sound. While the runtime path is now YAML-based, the same concepts apply. Be sure to match any tuning with the equivalent entries in `packs/phonemes.yaml`, and map usage in `packs/lang/*.yaml` so your changes are actually used.

### Adding a new phoneme (recommended workflow)
1. **Pick a key**
   - Use a real IPA symbol if possible (`ɲ`, `ʎ`, `ɨ`, …).
   - If you need a language-specific variant, use a private/internal key (we use things like `ᴒ`, `ᴇ`, `ᴀ`, `ᴐ`).
2. **Clone the closest existing phoneme**
   - Copy an existing entry and adjust it.
   - This is important: the engine expects most fields to exist. A “minimal” entry can crash.
3. **Tune it**
   - Start by adjusting formant center frequencies (`cf1`, `cf2`, `cf3`).
   - Then adjust bandwidths (`cb1`, `cb2`, `cb3`) if it sounds “boxy/ringy”.
   - Only then adjust frication/aspiration settings.
4. **Wire it up in language-specific YAML**
   - Make sure `normalizeIPA()` (legacy path) or the YAML normalization rules output your new key for the right language/case.
   - If you don’t map it, the phoneme will never be used.

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
- `cf1`, `cf2`, `cf3`, `cf4`, `cf5`, `cf6` — “Cascade” formant frequencies. F1–F3 matter most for vowel identity.
- `pf1`, `pf2`, `pf3`, `pf4`, `pf5`, `pf6` — “Parallel” formant frequencies. Usually matched to the `cf*` values.

Quick intuition:
- Higher `cf1` → more open mouth (e.g. “ah”)
- Higher `cf2` → more front / brighter (e.g. “ee”)
- Lower `cf2` → more back / rounder (“oo”)
- Lower `cf3` → more “r-colored” (rhotic vowels)

**Bandwidths** define how wide each resonance is:
- `cb1..cb6` and `pb1..pb6`

Quick intuition:
- Narrow bandwidth (small numbers) → very “ringy / boxy / hollow”
- Wider bandwidth → smoother / less resonant / less “plastic box”

If something sounds “boxy”, widening `cb2`/`cb3` (and matching `pb2`/`pb3`) is often the first fix.

#### Amplitude / mixing controls
- `voiceAmplitude`: Strength of voicing. Lower it slightly if vowels feel “over-held” or harsh.
- `fricationAmplitude`: Noise level for fricatives (`s`, `ʃ`, `f`, `x`, etc.). If “s” is too hissy, reduce this.
- `aspirationAmplitude`: Breath noise used for aspirated/“h-like” behavior. Usually 0 for vowels.
- `parallelBypass`: Mix control between cascade and parallel paths. Most phonemes keep this at 0.0 unless you know you need it.
- `pa1..pa6`: Per-formant amplitude in the parallel path. Most entries keep these at 0.0. If a diphthong glide is too weak, a tiny `pa2`/`pa3` boost can help.

#### Nasal coupling (optional)
Some entries include:
- `cfN0`, `cfNP`, `cbN0`, `cbNP`, `caNP`

These relate to nasal resonance and coupling. We currently treat nasality conservatively; if you don’t know what to do, clone from an existing nasal vowel/consonant entry.

### Practical tuning tips (fast wins)
“This vowel sounds too much like another vowel”
- Adjust `cf1` and `cf2` first.
- Example: Hungarian short `a` vs long `á`: make short `a` lower `cf1` and lower `cf2` compared to `á`.

“This vowel is boxy / hollow / plastic”
- Widen `cb2`/`cb3` (and `pb2`/`pb3`) a bit.

“This sound is too sharp/hissy”
- Lower `fricationAmplitude`.

“This rhotic vowel (`ɚ`/`ɝ`) is too thick”
- Raise `cf3` slightly (less r-color) or widen `cb3`.

## Generating phonemes.yaml from Python data
If you are migrating from the legacy data dictionary, use the conversion tool in `tools/` to regenerate `packs/phonemes.yaml`.

(Exact script name may vary depending on the branch; check `tools/` for the converter.)

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
   - only add new engine behavior if the data model truly can’t express it

## Tonal languages (Chinese, Vietnamese, etc.)
Tonal languages are supported by treating tone as an optional overlay on top of the existing pitch model:
- enable `settings.tonal: true`
- define tone contours (digits or tone letters) in the language pack

This lets new tonal behavior be added mostly as YAML, while still allowing deeper future work (tone sandhi) if needed.

## Building
This repo can be built with CMake to produce:
- `speechPlayer.dll`
- `nvspFrontend.dll`

Example:
```bat
cmake -S . -B build-win32
cmake --build build-win32 --config Release
```

The NVDA add-on build process packages:
- the DLLs,
- and the `packs/` directory.

## NVDA add-on
The NVDA driver loads:
- `speechPlayer.dll` (DSP engine)
- `nvspFrontend.dll` (IPA → timed frames)
- `packs/` (YAML configuration)
- and uses eSpeak for text → IPA conversion.

This keeps the DSP core small and stable while making language updates fast and community-editable.
The driver now supports NVDA 2026.1, but should be compatible across NVDA 2023.2. This has created a lot of boilerplate code, unfortunately, as supporting older NVDA is complex. Nonetheless, I recognize some people still want to use this in Windows 7 environments, so we will continue to support older NVDA as long as we can until performance penalties arise.
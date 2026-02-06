# NV Speech Player – Developer Documentation

This document covers the DSP pipeline internals, VoicingTone API, FrameEx struct, frontend architecture, and other technical implementation details.

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

The `VoicingTone` struct contains parameters that shape the voiced sound source. As of DSP version 6, there are **14 parameters** plus a version detection header.

#### Version detection (v3 struct)

The v3 struct includes a header for backward compatibility:

| Field | Type | Description |
|-------|------|-------------|
| `magic` | uint32 | Magic number `0x32544F56` ("VOT2" in little-endian) |
| `structSize` | uint32 | Size of the struct in bytes |
| `structVersion` | uint32 | Struct version (currently 3) |
| `dspVersion` | uint32 | DSP version (currently 6) |

When the DLL receives a `VoicingTone` struct, it checks the magic number:
- **If magic matches**: Uses the full v3 struct with all 14 parameters
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
| `cascadeBwScale` | double | 0.8 | 0.2–2.0 | Global cascade formant bandwidth multiplier. Lower values (0.2–0.5) create sharper, more defined formant peaks with "Eloquence-like" clarity. Higher values (1.2–2.0) blur formants together for softer, more muffled quality. Default 0.8 provides good intelligibility. |
| `tremorDepth` | double | 0.0 | 0.0–0.4 | Vocal tremor depth for elderly/shaky voice effect. A 5 Hz low-frequency oscillator modulates pitch (±28%), open quotient (±0.12), and amplitude (±20%) simultaneously. This creates a realistic "wavering voice" characteristic of aging or emotional tremor. Combines well with jitter/shimmer for enhanced realism. 0.0 = off, 0.3–0.4 = noticeable elderly tremor. |

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
        ("cascadeBwScale", c_double),
        ("tremorDepth", c_double),
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
        tone.cascadeBwScale = 0.8
        tone.tremorDepth = 0.0
        return tone

# Set up function prototypes
dll.speechPlayer_setVoicingTone.argtypes = [c_void_p, POINTER(VoicingTone)]
dll.speechPlayer_setVoicingTone.restype = None

# Create and configure tone
tone = VoicingTone.defaults()
tone.voicedTiltDbPerOct = -6.0  # Brighter tilt (Eloquence-like)
tone.speedQuotient = 1.2  # Softer, more female-like pulse
tone.cascadeBwScale = 0.5  # Sharper formants
tone.tremorDepth = 0.3  # Elderly tremor effect

# Apply to running synthesizer
dll.speechPlayer_setVoicingTone(handle, byref(tone))
```

### NVDA driver sliders

The driver exposes 12 sliders for real-time voice tuning:

#### VoicingTone sliders (global voice character)

| Slider | Range | Default | Maps to |
|--------|-------|---------|---------|
| Voice tilt (brightness) | 0–100 | 50 | `voicedTiltDbPerOct` (-24 to +24 dB/oct) |
| Noise glottal modulation | 0–100 | 0 | `noiseGlottalModDepth` (0.0–1.0) |
| Pitch-sync F1 delta | 0–100 | 50 | `pitchSyncF1DeltaHz` (-60 to +60 Hz) |
| Pitch-sync B1 delta | 0–100 | 50 | `pitchSyncB1DeltaHz` (-50 to +50 Hz) |
| Speed quotient | 0–100 | 50 | `speedQuotient` (0.5–4.0) |
| Formant sharpness | 0–100 | 50 | `cascadeBwScale` (2.0–0.2, inverted: higher = sharper) |
| Tremor | 0–100 | 0 | `tremorDepth` (0.0–0.4) |

#### FrameEx sliders (per-frame voice quality)

| Slider | Range | Default | Maps to |
|--------|-------|---------|---------|
| Creakiness | 0–100 | 0 | `creakiness` (0.0–1.0) |
| Breathiness | 0–100 | 0 | `breathiness` (0.0–1.0) |
| Jitter | 0–100 | 0 | `jitter` (0.0–1.0) |
| Shimmer | 0–100 | 0 | `shimmer` (0.0–1.0) |
| Glottal sharpness | 0–100 | 50 | `sharpness` (0.5–2.0 multiplier) |

Note: Sliders centered at 50 (tilt, pitch-sync, speed quotient, formant sharpness, glottal sharpness) have meaningful positive and negative ranges. Sliders starting at 0 (noise modulation, tremor, creakiness, breathiness, jitter, shimmer) are effect amounts that default to "off".

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
- keeps "phoneme sound definitions" separate from "language mapping rules".

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

## Legacy Python files (kept for reference)
`ipa.py` and `data.py` still live alongside the repo for now, but they are no longer the runtime path for NVDA.

They are retained for:
- historical reference,
- comparing behavior during development,
- and as a source for generating YAML.

If you want to tune phonemes or language rules going forward, update YAML packs instead.

## Generating phonemes.yaml from Python data
If you are migrating from the legacy data dictionary, use the conversion tool in `tools/` to regenerate `packs/phonemes.yaml`.

(Exact script name may vary depending on the branch; check `tools/` for the converter.)
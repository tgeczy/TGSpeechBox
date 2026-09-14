# TGSpeechBox - Linux Build

A Klatt-based speech synthesis engine for Linux.

Pre-built binaries are available for Linux x86_64 and aarch64 (ARM64, e.g. Raspberry Pi 4/5).

## Contents

```
tgspeechbox-linux-<arch>/        # where <arch> is x86_64 or aarch64
├── bin/
│   ├── tgsbRender     # Core binary (IPA → raw PCM, optional --espeak mode)
│   ├── sd_tgsb        # Native Speech Dispatcher module
│   └── tgsp           # Wrapper script with paths configured
├── lib/
│   ├── libtgspeechbox.so     # DSP engine
│   └── libtgsbFrontend.so    # IPA → formant frames
├── share/tgspeechbox/
│   ├── packs/         # Language data (phoneme definitions)
│   │   ├── phonemes.yaml
│   │   └── lang/      # Language-specific rules
│   └── extras/
│       └── speech-dispatcher/  # Config files (native + generic)
├── install.sh         # Installation script
└── README.md          # This file
```

## Quick Start (No Installation)

You can run directly from this directory:

```bash
# Test synthesis (outputs raw PCM to stdout)
echo 'həˈloʊ wɜːld' | ./bin/tgsp --lang en-us > test.raw

# Play with aplay (ALSA)
echo 'həˈloʊ wɜːld' | ./bin/tgsp --lang en-us | aplay -q -r 22050 -f S16_LE -t raw -

# Convert to WAV with ffmpeg
echo 'həˈloʊ wɜːld' | ./bin/tgsp --lang en-us | \
    ffmpeg -f s16le -ar 22050 -ac 1 -i - output.wav
```

## Installation

### System-wide (requires root)

```bash
sudo ./install.sh
# Installs to /usr/local by default
```

### Custom prefix

```bash
./install.sh ~/tgspeechbox
# Then add ~/tgspeechbox/bin to your PATH
```

### Manual installation

```bash
# Copy files to desired locations:
cp bin/tgsbRender /usr/local/bin/
cp lib/*.so /usr/local/lib/
cp -r share/tgspeechbox /usr/local/share/

# Update library cache
sudo ldconfig

# Create wrapper or use LD_LIBRARY_PATH
```

## Usage

### tgsbRender Options

```
tgsbRender [options]

Reads IPA text from stdin (UTF-8) and writes raw 16-bit PCM to stdout.

Basic options:
  --packdir <path>      Path to packs directory (default: .)
  --lang <tag>          Language tag: en, en-us, en-gb, de, fr, etc. (default: en)
  --voice <name>        Voice profile name from phonemes.yaml (default: none)
  --list-voices         List available voice profiles and exit
  --text <string>       Original text for stress correction + IPA overrides
  --clause <char>       Clause type override: . ? ! , (default: auto-detect)
  --rate <int>          Speech rate -100..100 (default: 0)
  --rate-boost          Double effective speed with DSP time-stretch
  --pitch <int>         Base pitch 0..100 (default: 50)
  --volume <float>      Output gain multiplier (default: 1.0)
  --samplerate <int>    Output sample rate in Hz (default: 16000)
  --inflection <float>  Pitch variation amount (default: 0.5)
  --prepare-text        Text normalization mode (see below)
  -h, --help            Show help

VoicingTone parameters (0-100 sliders, 50 = neutral):
  --voicing-peak-pos <int>    Glottal pulse peak position
  --voiced-preemph-a <int>    Pre-emphasis coefficient
  --voiced-preemph-mix <int>  Pre-emphasis mix
  --high-shelf-gain <int>     High shelf gain dB
  --high-shelf-fc <int>       High shelf frequency
  --high-shelf-q <int>        High shelf Q factor
  --voiced-tilt <int>         Voiced spectral tilt dB/octave
  --noise-glottal-mod <int>   Noise glottal modulation depth
  --pitch-sync-f1 <int>       Pitch-sync F1 delta Hz
  --pitch-sync-b1 <int>       Pitch-sync B1 delta Hz
  --speed-quotient <int>      Glottal pulse asymmetry
  --aspiration-tilt <int>     Aspiration spectral tilt

FrameEx voice quality (0-100 sliders):
  --creakiness <int>    Laryngealization / creaky voice (default: 0)
  --breathiness <int>   Breath noise mixed into voicing (default: 0)
  --jitter <int>        Pitch period variation (default: 0)
  --shimmer <int>       Amplitude variation (default: 0)
  --sharpness <int>     Glottal closure sharpness (default: 50)
```

### Voice Profiles

Voice profiles are defined in `packs/phonemes.yaml` under `voiceProfiles:`. They
preset VoicingTone and FrameEx parameters for different voice characters.

```bash
# List available profiles
./bin/tgsp --list-voices

# Use a specific profile
echo 'həˈloʊ' | ./bin/tgsp --lang en-us --voice Clara | aplay -q -r 22050 -f S16_LE -t raw -
```

### Voice Quality Examples

```bash
# Creaky/vocal fry voice
echo 'həˈloʊ' | ./bin/tgsp --lang en-us --creakiness 60 | aplay -q -r 22050 -f S16_LE -t raw -

# Breathy voice
echo 'həˈloʊ' | ./bin/tgsp --lang en-us --breathiness 40 | aplay -q -r 22050 -f S16_LE -t raw -

# Add some naturalness with jitter/shimmer
echo 'həˈloʊ' | ./bin/tgsp --lang en-us --jitter 15 --shimmer 10 | aplay -q -r 22050 -f S16_LE -t raw -

# Brighter voice (more high frequencies)
echo 'həˈloʊ' | ./bin/tgsp --lang en-us --voiced-tilt 35 --aspiration-tilt 60 | aplay -q -r 22050 -f S16_LE -t raw -
```

### Rate Boost

Rate boost doubles the effective speech rate using DSP time-stretch, matching the behavior on NVDA, Android, iOS, and SAPI:

```bash
# Rate boost via command line
echo 'həˈloʊ wɜːld' | ./bin/tgsp --lang en-us --rate-boost | aplay -q -r 22050 -f S16_LE -t raw -

# Rate boost via environment variable (for Speech Dispatcher)
export TGSB_RATE_BOOST=1
```

### Text Normalization and Dictionary Support

The `--prepare-text` mode applies pronunciation dictionary replacements, compound splitting, and other text-level transforms before eSpeak phonemization. This enables the same dictionary and IPA override features available on other platforms.

```bash
# Normalize text before piping to eSpeak (used by tgsb-speak wrapper)
printf 'Hello world' | tgsbRender --prepare-text --packdir /usr/share/tgspeechbox --lang en-us
```

The `tgsb-speak` wrapper script runs this automatically. When `--text` is also passed during synthesis, the engine applies stress correction and IPA overrides from the pronunciation dictionary.

### With eSpeak-ng for Text-to-IPA

TGSpeechBox needs IPA input. Use eSpeak-ng to convert text to IPA:

```bash
# Using eSpeak-ng for text → IPA → tgsbRender for IPA → audio
espeak-ng --ipa=1 -v en-us "Hello world" 2>/dev/null | \
    ./bin/tgsp --lang en-us | \
    aplay -q -r 22050 -f S16_LE -t raw -
```

Install eSpeak-ng:
```bash
# Debian/Ubuntu
sudo apt install espeak-ng

# Fedora
sudo dnf install espeak-ng

# Arch
sudo pacman -S espeak-ng
```

## Using Third-Party Phonemizers

TGSpeechBox is an IPA-to-audio engine – it doesn't do text-to-IPA conversion itself. You can pair it with any phonemizer that outputs IPA to stdout.

### eSpeak-ng (recommended)

eSpeak-ng is fast, widely available, and supports many languages:
```bash
espeak-ng --ipa=1 -v en-us "Hello world" 2>/dev/null | \
    ./bin/tgsp --lang en-us | aplay -q -r 22050 -f S16_LE -t raw -
```

The `--ipa=1` flag outputs IPA with stress markers. Use `--ipa=3` for more detail (includes tie bars).

### Phonemizer (Python)

The [phonemizer](https://github.com/bootphon/phonemizer) package wraps multiple backends and offers fine control over IPA output:
```bash
pip install phonemizer

echo "Hello world" | phonemizer -l en-us -b espeak --strip | \
    ./bin/tgsp --lang en-us | aplay -q -r 22050 -f S16_LE -t raw -
```

Phonemizer can also use festival or segments backends for languages where eSpeak coverage is limited.

### Other phonemizers

Any tool that outputs IPA to stdout will work. Some options:

| Tool | Notes |
|------|-------|
| [Epitran](https://github.com/dmort27/epitran) | Rule-based G2P for many languages |
| [DeepPhonemizer](https://github.com/as-ideas/DeepPhonemizer) | Neural G2P, good for English |
| [Gruut](https://github.com/rhasspy/gruut) | Designed for TTS pipelines |
| [lexconvert](http://ssb22.user.srcf.net/gradint/lexconvert.html) | Convert between phoneme formats |

### Building a wrapper script

For convenience, create a wrapper that chains your preferred phonemizer:
```bash
#!/bin/bash
# tgsb-say: text-to-speech via eSpeak-ng + TGSpeechBox

LANG="${1:-en-us}"
RATE="${2:-0}"
shift 2 2>/dev/null

espeak-ng --ipa=1 -v "$LANG" "$@" 2>/dev/null | \
    tgsp --lang "$LANG" --rate "$RATE" | \
    aplay -q -r 22050 -f S16_LE -t raw -
```

Usage: `tgsb-say en-us 20 "Hello world"`

### Handling IPA dialect differences

Different phonemizers produce slightly different IPA. TGSpeechBox's normalization layer handles most variations, but you may need to adjust:

- **Tie bars**: eSpeak uses `t͡ʃ`, some tools output `tʃ` – both work
- **Stress markers**: TGSpeechBox expects `ˈ` (primary) and `ˌ` (secondary) before the stressed syllable
- **Length marks**: Use `ː` for long vowels
- **Word boundaries**: Spaces or `‖` between words

If your phonemizer outputs a format that doesn't work well, you can preprocess with `sed`:
```bash
# Example: convert X-SAMPA to IPA (simplified)
my-phonemizer "hello" | sed 's/"/ˈ/g; s/%/ˌ/g; s/:/ː/g' | ./bin/tgsp --lang en-us
```

### Streaming long text

For long documents, process line-by-line to avoid buffering delays:
```bash
cat document.txt | while IFS= read -r line; do
    espeak-ng --ipa=1 -v en-us "$line" 2>/dev/null | \
        ./bin/tgsp --lang en-us
done | aplay -q -r 22050 -f S16_LE -t raw -
```

## Speech Dispatcher Integration

TGSpeechBox includes a native Speech Dispatcher module (`sd_tgsb`) for desktop accessibility with screen readers like Orca. The `install.sh` script configures everything automatically.

### Native module vs generic module

There are two Speech Dispatcher integration paths:

| | Native module (`sd_tgsb`) | Generic module (`tgsb-generic.conf`) |
|---|---|---|
| **Config** | `tgsb-native.conf` | `tgsb-generic.conf` |
| **How it works** | Persistent process. Loads espeak-ng and TGSpeechBox once at startup. Sends audio via the 705 AUDIO protocol. | Spawns `tgsb-speak` per utterance, which calls `tgsbRender --espeak`. |
| **Audio quality** | Clean — single process, no pipes between phonemizer and renderer. | May have slight shimmer/discontinuity from pipe buffering between processes. |
| **STOP responsiveness** | Instant — polls stdin between audio chunks, same architecture as sd_espeak-ng. | Relies on SD killing the subprocess. |
| **Startup cost** | One-time at module load (espeak + engine initialized once). | Per-utterance (new process, new dlopen, new init each time). |

The installer prefers the native module when the `sd_tgsb` binary is available. The generic module serves as a fallback for systems where the native binary doesn't run.

### Automatic setup

```bash
sudo ./install.sh
killall speech-dispatcher
```

The installer:
- Copies `sd_tgsb` to `/usr/lib/speech-dispatcher-modules/`
- Installs config files to `/etc/speech-dispatcher/modules/`
- Adds `AddModule "tgsb" "sd_tgsb" "tgsb-native.conf"` to `speechd.conf`
- Copies a per-user config template to `~/.config/tgspeechbox/sd_tgsb.conf`
- Ensures espeak-ng is enabled as a fallback

Test with: `spd-say -o tgsb "Hello from TGSpeechBox"`

### Manual setup

If you prefer to configure manually:

```bash
# Copy the native module binary
sudo cp bin/sd_tgsb /usr/lib/speech-dispatcher-modules/
sudo chmod +x /usr/lib/speech-dispatcher-modules/sd_tgsb

# Copy the config
sudo cp share/tgspeechbox/extras/speech-dispatcher/tgsb-native.conf \
       /etc/speech-dispatcher/modules/

# Add to speechd.conf
echo 'AddModule "tgsb" "sd_tgsb" "tgsb-native.conf"' | \
    sudo tee -a /etc/speech-dispatcher/speechd.conf

# Restart
killall speech-dispatcher
```

### Configuration

The native module reads settings from a config file. Two locations are checked (per-user wins):

| Location | Purpose |
|----------|---------|
| `/etc/speech-dispatcher/modules/tgsb-native.conf` | System-wide (login screen, all users) |
| `~/.config/tgspeechbox/sd_tgsb.conf` | Per-user (personal voice preferences) |

The config file ships as a commented-out template showing all defaults. Uncomment any line to override:

```conf
# Pitch mode: espeak_style, fujisaki_style, impulse_style, klatt_style, arato_style
TGSBPitchMode fujisaki_style

# Pause between clauses: off, short, long
TGSBPauseMode short

# Sample rate in Hz
TGSBSampleRate 22050

# Voicing tone (0-100, 50 = neutral)
TGSBVoicedTilt 40
TGSBChorusDepth 20

# Voice quality (0-100)
TGSBBreathiness 10
```

After editing: `killall speech-dispatcher`

### Built-in voices

Five built-in voices are available: Adam, Benjamin, Caleb, David, Robert. YAML voice profiles (Beth, Bobby, and any user-defined profiles) also appear in Orca's voice list.

### Recovery

If you lose your voice after switching synthesizers:

```bash
sudo killall -9 speech-dispatcher orca
sudo sed -i 's/^DefaultModule.*/DefaultModule espeak-ng/' /etc/speech-dispatcher/speechd.conf
orca --replace &
```

## Supported Languages

The following language packs are included:

| Tag     | Language              |
|---------|-----------------------|
| en      | English (generic)     |
| en-us   | English (US)          |
| en-gb   | English (British)     |
| en-ca   | English (Canadian)    |
| de      | German                |
| fr      | French                |
| es      | Spanish               |
| es-mx   | Spanish (Mexican)     |
| it      | Italian               |
| pt      | Portuguese            |
| pt-br   | Portuguese (Brazilian)|
| nl      | Dutch                 |
| pl      | Polish                |
| ru      | Russian               |
| uk      | Ukrainian             |
| cs      | Czech                 |
| sk      | Slovak                |
| hu      | Hungarian             |
| ro      | Romanian              |
| hr      | Croatian              |
| bg      | Bulgarian             |
| sv      | Swedish               |
| da      | Danish                |
| fi      | Finnish               |
| tr      | Turkish               |
| zh      | Chinese               |

## Audio Output Format

- Sample rate: 22050 Hz (default, configurable via --samplerate)
- Bit depth: 16-bit signed
- Channels: Mono
- Byte order: Little-endian

## Building from Source

If you want to build from source instead:

```bash
# Clone the repo
git clone https://github.com/tgeczy/TGSpeechBox.git
cd TGSpeechBox

# Build with CMake
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DNVSP_BUILD_TOOLS=ON
cmake --build build -j

# Or with the included Makefile (no CMake required)
make -f Makefile.linux                                                  # shared libs (x86_64)
make -f Makefile.linux STATIC=1                                         # static build (x86_64)
make -f Makefile.linux CROSS_COMPILE=aarch64-linux-gnu-                 # shared libs (ARM64)
make -f Makefile.linux CROSS_COMPILE=aarch64-linux-gnu- STATIC=1        # static build (ARM64)
```

### Build Requirements

- GCC 9+ or Clang 10+ (C++17 support)
- CMake 3.21+ (if using CMake)
- pthreads library
- For ARM64 cross-compilation: `gcc-aarch64-linux-gnu` and `g++-aarch64-linux-gnu`

### Cross-compiling for ARM64

To build for Raspberry Pi or other aarch64 targets from an x86_64 host:

```bash
# Install the cross-compiler (Debian/Ubuntu)
sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu

# Build with Makefile
make -f Makefile.linux CROSS_COMPILE=aarch64-linux-gnu-

# Or static build (portable single binary)
make -f Makefile.linux CROSS_COMPILE=aarch64-linux-gnu- STATIC=1
```

## Troubleshooting

### "error while loading shared libraries"

Set the library path:
```bash
export LD_LIBRARY_PATH=/path/to/tgspeechbox/lib:$LD_LIBRARY_PATH
```

Or install to a system location and run `ldconfig`.

### No sound output

Check that:
1. Your audio system works: `aplay /usr/share/sounds/alsa/Front_Center.wav`
2. The IPA input is valid
3. You're using the correct audio parameters with aplay

### Language pack not found

Make sure `--packdir` points to a directory containing a `packs/` subdirectory
with `phonemes.yaml` and the `lang/` folder.

## License

TGSpeechBox core (DSP engine, frontend, tgsbRender) is MIT-licensed. The native Speech Dispatcher module (`sd_tgsb`) is GPL-3.0 because it requires espeak-ng. See [LICENSE](LICENSE) and the main [README](readme.md) for the full dual-license details.

Copyright (c) 2014 NV Access Limited
Copyright (c) 2025-2026 Tamas Geczy

## Links

- Source: https://github.com/tgeczy/TGSpeechBox
- Original project: https://github.com/nvaccess/nvSpeechPlayer
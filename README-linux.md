# TGSpeechBox - Linux Build

A Klatt-based speech synthesis engine for Linux.

This package contains pre-built binaries of TGSpeechBox for Linux x86_64.

## Contents

```
tgspeechbox-linux-x86_64/
├── bin/
│   ├── tgsbRender     # Core binary (IPA → raw PCM)
│   └── tgsp           # Wrapper script with paths configured
├── lib/
│   ├── libtgspeechbox.so     # DSP engine
│   └── libtgsbFrontend.so    # IPA → formant frames
├── share/tgspeechbox/
│   ├── packs/         # Language data (phoneme definitions)
│   │   ├── phonemes.yaml
│   │   └── lang/      # Language-specific rules
│   └── extras/
│       └── speech-dispatcher/  # Config for Speech Dispatcher
├── install.sh         # Installation script
└── README.md          # This file
```

## Quick Start (No Installation)

You can run directly from this directory:

```bash
# Test synthesis (outputs raw PCM to stdout)
echo 'həˈloʊ wɜːld' | ./bin/tgsp --lang en-us > test.raw

# Play with aplay (ALSA)
echo 'həˈloʊ wɜːld' | ./bin/tgsp --lang en-us | aplay -q -r 16000 -f S16_LE -t raw -

# Convert to WAV with ffmpeg
echo 'həˈloʊ wɜːld' | ./bin/tgsp --lang en-us | \
    ffmpeg -f s16le -ar 16000 -ac 1 -i - output.wav
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
  --rate <int>          Speech rate -100..100 (default: 0)
  --pitch <int>         Base pitch 0..100 (default: 50)
  --volume <float>      Output gain multiplier (default: 1.0)
  --samplerate <int>    Output sample rate in Hz (default: 16000)
  --inflection <float>  Pitch variation amount (default: 0.5)
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
echo 'həˈloʊ' | ./bin/tgsp --lang en-us --voice Clara | aplay -q -r 16000 -f S16_LE -t raw -
```

### Voice Quality Examples

```bash
# Creaky/vocal fry voice
echo 'həˈloʊ' | ./bin/tgsp --lang en-us --creakiness 60 | aplay -q -r 16000 -f S16_LE -t raw -

# Breathy voice
echo 'həˈloʊ' | ./bin/tgsp --lang en-us --breathiness 40 | aplay -q -r 16000 -f S16_LE -t raw -

# Add some naturalness with jitter/shimmer
echo 'həˈloʊ' | ./bin/tgsp --lang en-us --jitter 15 --shimmer 10 | aplay -q -r 16000 -f S16_LE -t raw -

# Brighter voice (more high frequencies)
echo 'həˈloʊ' | ./bin/tgsp --lang en-us --voiced-tilt 35 --aspiration-tilt 60 | aplay -q -r 16000 -f S16_LE -t raw -
```

### With eSpeak-ng for Text-to-IPA

TGSpeechBox needs IPA input. Use eSpeak-ng to convert text to IPA:

```bash
# Using eSpeak-ng for text → IPA → tgsbRender for IPA → audio
espeak-ng --ipa=1 -v en-us "Hello world" 2>/dev/null | \
    ./bin/tgsp --lang en-us | \
    aplay -q -r 16000 -f S16_LE -t raw -
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
    ./bin/tgsp --lang en-us | aplay -q -r 16000 -f S16_LE -t raw -
```

The `--ipa=1` flag outputs IPA with stress markers. Use `--ipa=3` for more detail (includes tie bars).

### Phonemizer (Python)

The [phonemizer](https://github.com/bootphon/phonemizer) package wraps multiple backends and offers fine control over IPA output:
```bash
pip install phonemizer

echo "Hello world" | phonemizer -l en-us -b espeak --strip | \
    ./bin/tgsp --lang en-us | aplay -q -r 16000 -f S16_LE -t raw -
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
    aplay -q -r 16000 -f S16_LE -t raw -
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
done | aplay -q -r 16000 -f S16_LE -t raw -
```

## Speech Dispatcher Integration

TGSpeechBox can be used as a Speech Dispatcher voice for desktop accessibility.

See `share/tgspeechbox/extras/speech-dispatcher/README.md` for setup instructions.

Quick summary:
1. Copy `tgsb-generic.conf` to `/etc/speech-dispatcher/modules/`
2. Edit paths in the config file
3. Add to `speechd.conf`:
   ```
   AddModule "tgsb" "sd_generic" "tgsb-generic.conf"
   DefaultModule tgsb
   ```
4. Test with: `spd-say "Hello from TGSpeechBox"`

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
| zh      | Chinese               |

## Audio Output Format

- Sample rate: 16000 Hz (default, configurable via --samplerate)
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
make
```

### Build Requirements

- GCC 9+ or Clang 10+ (C++17 support)
- CMake 3.21+ (if using CMake)
- pthreads library

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

TGSpeechBox is covered by the GNU General Public License (Version 2).

Copyright (c) 2014 TGSpeechBox contributors.

This is a community-maintained fork. The original project was created by NV Access Limited.

## Links

- Source: https://github.com/tgeczy/TGSpeechBox
- Original project: https://github.com/nvaccess/nvSpeechPlayer
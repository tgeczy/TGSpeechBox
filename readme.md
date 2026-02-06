# TGSpeechBox

A formant speech synthesis engine with LF glottal modeling, coarticulation, and 26+ languages, written in C++.

Author: Tamas Geczy. Originally forked from NV Speech Player by NV Access Limited.

### Acknowledgments
Special thanks to:
- **Rommix** for extensive testing, feedback, and collaboration on the Fujisaki pitch model implementation and DSP timing parameters.
- **Cleverson** for his contributions to the Portuguese packs. Without him, we would not have proper support for either variants of the language. His work and efforts can be heard in the language and tuning on it is most exclusively done by him. Hats off!
- **Fastfinge** - he helped us get the first part of the asymmetric cosign pulse right, and honestly without his initial PR we probably wouldn't be using it today.

## Project history and naming
TGSpeechBox began as a fork of NV Speech Player, which is no longer actively maintained by NV Access.
You are welcome to fork and view the original project at:
https://github.com/nvaccess/NVSpeechPlayer
This project has since been substantially rewritten — the DSP engine, frontend, language pipeline, and tooling are all new. The rename to TGSpeechBox reflects this divergence and avoids confusion with the original project.

TGSpeechBox is **not affiliated with, endorsed by, or supported by NV Access**.

### NVDA / trademarks
This project includes an NVDA add-on and necessarily refers to NVDA for compatibility and documentation. NVDA and related names/marks belong to their respective owners. This project does not claim any official relationship.

### Note on eSpeak-ng
The eSpeak-ng project also contains a separate copy of the original SpeechPlayer code as an alternative Klatt-style implementation. TGSpeechBox is independent of that copy.

## Overview
TGSpeechBox is a free and open-source speech synthesizer that can be used by NVDA. It generates speech using formant synthesis with an LF-inspired glottal model, Fujisaki pitch contours, and coarticulation — making it somewhat similar in character to speech synthesizers such as Dectalk and Eloquence, while adding modern voice quality controls.

Historically, the original NV Speech Player relied on Python (`ipa.py`) to:
- normalize phonemes (mostly from eSpeak IPA),
- apply language/dialect rules,
- generate timed frame tracks and intonation,
- and feed frames into the C++ DSP engine.

This repo has now transitioned to a new **frontend + YAML packs** model that replaces the Python IPA pipeline for runtime use.

## License and copyright
TGSpeechBox is Copyright (c) 2014 NV Speech Player contributors, Copyright (c) 2025 Tamas Geczy.

TGSpeechBox is covered by the GNU General Public License (Version 2).

You are free to share or change this software in any way you like as long as it is accompanied by the license and you make all source code available to anyone who wants it. This applies to both original and modified copies of this software, plus any derivative works.

For further details, you can view the license online at:
http://www.gnu.org/licenses/old-licenses/gpl-2.0.html

## Background
The 70s and 80s saw much research in speech synthesis. One of the most prominent synthesis models that appeared was a formant-frequency synthesis known as Klatt synthesis. Some well-known Klatt synthesizers are Dectalk and Eloquence. They are well suited for use by the blind because they are responsive, predictable, and small in memory footprint.

Research later moved toward other synthesis approaches because they can sound closer to a human voice, but they often trade away responsiveness and predictability.

TGSpeechBox exists as a modern formant synthesizer:
- to explore the "classic" fast-and-stable sound profile with modern voice quality enhancements,
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

## Installation

### NVDA add-on

The NVDA add-on is included with each [release](https://github.com/TGeczy/TGSpeechBox/releases). Download the `.nvda-addon` file and open it with NVDA to install.
It supports versions 2023.2 to 2026.1.

### Linux

Pre-built binaries are available for Linux x86_64. The package includes command-line tools for IPA-to-speech synthesis and optional Speech Dispatcher integration for desktop accessibility.

See **[README-linux.md](README-linux.md)** for installation options, usage examples, and supported languages.

### Phoneme Editor (Windows)

The Phoneme Editor is a Win32 GUI for editing `phonemes.yaml` and language pack files. It's useful for:

- **Language tuners** who want to adjust phoneme definitions or normalization rules
- **Anyone wanting a simple speak/preview tool** — type IPA, hear it spoken, save to WAV

The editor can preview individual phonemes, synthesize speech from IPA input, and save audio files. See **[README-phoneme-editor.md](README-phoneme-editor.md)** for build instructions and usage.

## Documentation

For more detailed information, see:

- **[Developers.md](Developers.md)** — DSP pipeline internals, VoicingTone API, FrameEx struct, frontend architecture, and technical implementation details.
- **[Tuning.md](Tuning.md)** — Language pack settings, phoneme tuning, normalization rules, voice profiles, and how to add or modify languages.
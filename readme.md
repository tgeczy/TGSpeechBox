# NV Speech Player

A Klatt-based speech synthesis engine written in C++.

Author: Original project by NV Access Limited. This repository is maintained as a community fork by Tamas Geczy.

### Acknowledgments
Special thanks to:
- **Rommix** for extensive testing, feedback, and collaboration on the Fujisaki pitch model implementation and DSP timing parameters.
- **Cleverson** for his contributions to the Portuguese packs. Without him, we would not have proper support for either variants of the language. His work and efforts can be heard in the language and tuning on it is most exclusively done by him. Hats off!
- ** Fastfinge ** - he helped us get the first part of the asymmetric cosign pulse right, and honestly without his initial PR we probably wouldn't be using it today.

## Project status (fork + naming)
NV Access is not actively maintaining the original NV Speech Player project.

This repository is an independently maintained fork. It is **not affiliated with, endorsed by, or supported by NV Access**.

### About the name
The "NV Speech Player" name is kept here for historical continuity and to help existing users find the project they already know. It should not be interpreted as NV Access involvement.

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
- to explore the "classic" fast-and-stable sound profile,
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

The NVDA add-on is included with each [release](https://github.com/TGeczy/NVSpeechPlayer/releases). Download the `.nvda-addon` file and open it with NVDA to install.
It supports versions 2023.2 to 2026.1.

### Linux

Pre-built binaries are available for Linux x86_64. The package includes command-line tools for IPA-to-speech synthesis and optional Speech Dispatcher integration for desktop accessibility.

See **[README-linux.md](README-linux.md)** for installation options, usage examples, and supported languages.

### Phoneme Editor (Windows)

The Phoneme Editor is a Win32 GUI for editing `phonemes.yaml` and language pack files. It's useful for:

- **Language tuners** who want to adjust phoneme definitions or normalization rules
- **Anyone wanting a simple speak/preview tool** – type IPA, hear it spoken, save to WAV

The editor can preview individual phonemes, synthesize speech from IPA input, and save audio files. See **[README-phoneme-editor.md](README-phoneme-editor.md)** for build instructions and usage.

## Documentation

For more detailed information, see:

- **[Developers.md](Developers.md)** – DSP pipeline internals, VoicingTone API, FrameEx struct, frontend architecture, and technical implementation details.
- **[Tuning.md](Tuning.md)** – Language pack settings, phoneme tuning, normalization rules, voice profiles, and how to add or modify languages.

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
TGSpeechBox is a free and open-source speech synthesizer that can be used by NVDA, or Speech dispatcher on Linux. It generates speech using formant synthesis with an LF-inspired glottal model, Fujisaki pitch contours, and coarticulation — making it somewhat similar in character to speech synthesizers such as Dectalk and Eloquence, while adding modern voice quality controls.
The provided phoneme editor tool for Windows also functions as a speak utility, should you wish to easily test it on that platform.
Historically, the original NV Speech Player relied on Python (`ipa.py`) to:
- normalize phonemes (mostly from eSpeak IPA),
- apply language/dialect rules,
- generate timed frame tracks and intonation,
- and feed frames into the C++ DSP engine.

This repo has now transitioned to a new **frontend + YAML packs** model that replaces the Python IPA pipeline for runtime use.

## License and copyright
TGSpeechBox is Copyright (c) 2014 NV Speech Player contributors, Copyright (c) 2025-2026 Tamas Geczy.

This project uses a dual-license model. The core source code is MIT-licensed, but distributed binaries that link against eSpeak-NG are covered by the GNU General Public License version 3 (GPLv3), since eSpeak-NG is GPLv3.

### What's MIT
- **DSP engine** (`src/`) — the formant synthesizer, speech wave generator, and signal processing
- **Frontend** (`src/frontend/`) — IPA normalization, phoneme engine, pitch models, frame emission
- **Language packs** (`packs/`) — YAML phoneme definitions, language rules, and dictionaries
- **NVDA driver** (`addon/synthDrivers/`) — Python driver for NVDA screen reader
- **Phoneme editor** (`tools/tgsbPhonemeEditorWin32/`) — Win32 pack editing and preview tool
- **tgsbRender** (`tools/tgsbRender.cpp`) — command-line IPA-to-PCM renderer. The `--espeak` flag optionally loads the system's libespeak-ng.so via dlopen at runtime, but the base tool (IPA from stdin) is phonemizer-agnostic and fully MIT.
- **Supporting tools and scripts**

You can use, modify, and redistribute any of these under the MIT license. See the `LICENSE` file in the repository root.

### What's GPLv3
When the MIT-licensed code is compiled and linked with eSpeak-NG (a GPLv3 phonemizer), the resulting combined binary is distributed under GPLv3. This applies to:

- **Android app** (`src/platforms/android/`) — eSpeak-NG statically linked into the TTS service
- **iOS and macOS app** (`src/platforms/ios/`) — eSpeak-NG statically linked into the Audio Unit extension
- **SAPI5 engine** (`src/sapi/`) — eSpeak-NG statically linked into the COM wrapper DLL
- **Native Speech Dispatcher module** (`src/platforms/speechd/sd_tgsb.cpp`) — dynamically loads libespeak-ng.so via dlopen and requires it to function. Source file is GPL-3.0 licensed.

The GPL boundary is at link time (or, for sd_tgsb, at the functional-dependency level). If you build these platforms from source, the combined work must be distributed under GPLv3. If you use only the core engine and frontend libraries (without eSpeak-NG), the MIT license applies.

For the full text of the GPLv3, see: https://www.gnu.org/licenses/gpl-3.0.html

## Background
The 70s and 80s saw much research in speech synthesis. One of the most prominent synthesis models that appeared was a formant-frequency synthesis known as Klatt synthesis. Some well-known Klatt synthesizers are Dectalk and Eloquence. They are well suited for use by the blind because they are responsive, predictable, and small in memory footprint.

Research later moved toward other synthesis approaches because they can sound closer to a human voice, but they often trade away responsiveness and predictability.

TGSpeechBox exists as a modern formant synthesizer:
- to explore the "classic" fast-and-stable sound profile with modern voice quality enhancements,
- and to keep conversation and experimentation alive around this synthesis method.

## Use of AI and code provenance
This project uses generative AI as a collaborative development tool — for debugging, brainstorming approaches, and working through DSP or phonetics problems together. AI does not architect this project, write its features wholesale, or make design decisions unilaterally. Every change is reviewed, tested, and understood by a human before it ships.

That said, we recognize the broader risks that generative AI brings to open-source work. Large language models are trained on vast amounts of public code, and there is a real danger of inadvertently reproducing patented algorithms, proprietary techniques, or copyrighted material through AI-generated suggestions. We take this seriously:

- **No patented content.** No code in this repository knowingly implements patented algorithms or proprietary techniques. Where standard DSP methods are used (LF models, formant synthesis, SVF resonators), they are drawn from published academic literature.
- **No lifted code.** No code has been taken directly from commercial or open-source synthesis engines (such as DECtalk, Eloquence, or eSpeak-ng internals) without the express permission of the rights holders. Where this engine draws inspiration from classic synthesizers — for example, Klatt-style pitch modes or voicing characteristics — the implementations are reconstructed from inferred behavior and published academic literature (Klatt 1980, Klatt & Klatt 1990, technical papers referencing DECTalk, etc.). Where this project diverges from its original NV Speech Player fork, the new code is original work.
- **Ear-tuned, not copy-pasted.** Many of the acoustic parameters, formant targets, and voicing characteristics in this engine were arrived at through careful listening and iterative adjustment — not by copying tables or coefficients from existing synthesizers. The tuning reflects what sounds right to real users, not what a reference implementation dictates. Espeak's formant tables (which this project used from the original Speechplayer) will gradually move away and become their own formant tables as the project evolves.

We believe transparency about AI use is important, especially in accessibility software where trust matters. If you have questions or concerns about the provenance of any code in this repository, please open an issue.

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

To look for a newer add-on, press **Check for Add-on Updates** in the TGSpeechBox category of NVDA's settings. The **Update channel** choice beside it says which releases are looked at: **Stable** is the full releases, release candidates included; **Prerelease** is those and the betas too. Nothing is checked until you press the button, and nothing is installed without asking you first.

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
- **[Dictionary-editing.md](Dictionary-editing.md)** — Pronunciation overrides, IPA injection, user dictionaries, stress/compound dictionaries, and cross-platform import/export.
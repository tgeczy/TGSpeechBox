# NVSP Phoneme Editor (Win32)

A simple Win32 GUI for editing:

- `packs/phonemes.yaml` (global phoneme table)
- `packs/lang/*.yaml` (language normalization mappings)

This tool is meant for community editing: no fancy UI frameworks, just standard Win32 controls.

## Build

This repo already uses CMake.

On Windows:

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The executable target is:

- `nvspPhonemeEditor`

## First run

1. **File > Open pack root...**
   - Pick the folder that contains the `packs` directory.
2. **Settings > Set DLL directory...**
   - Pick the folder that contains `speechPlayer.dll` and `nvspFrontend.dll`.
3. (Optional) **Settings > Set eSpeak directory...**
   - Needed if you want "Convert to IPA" for plain text.

The tool stores these paths in `nvspPhonemeEditor.ini` next to the exe.

## What it can do

- Browse all phonemes from `phonemes.yaml`.
- Clone a phoneme entry to a new key.
- Edit a phoneme's scalar fields.
- Load a language YAML and edit its `normalization.replacements` rules.
- Edit language-level `settings:` key/value pairs (existing ones or add new knobs).
- Preview a phoneme (synthesizes a short clip and plays it).
- Speak / save WAV from IPA via `nvspFrontend.dll`.

## Notes

- YAML comments and original formatting are not preserved when saving.
  Use git to review diffs.
- For the frontend DLL to work, your pack root must contain `packs/phonemes.yaml`.

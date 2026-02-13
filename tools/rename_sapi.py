#!/usr/bin/env python3
"""
One-shot script to rename NVSpeech→TGSpeech branding in copied SAPI files
and add MIT license headers.
"""

import os
import re
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))

SAPI_DIR = os.path.join(ROOT, "src", "sapi")
SETTINGS_DIR = os.path.join(ROOT, "tools", "tgsbSapiSettings")

# ── String replacements (order matters — longer/more specific first) ──

REPLACEMENTS = [
    # Include directives (file renames)
    ('#include "nvsp_runtime.hpp"', '#include "tgsb_runtime.hpp"'),
    ('#include "nvsp_settings.hpp"', '#include "tgsb_settings.hpp"'),

    # Display names — specific strings first
    ('L"NV Speech Player Voices"', 'L"TGSpeechBox Voices"'),
    ('L"NV Speech Player - "', 'L"TGSpeechBox - "'),
    ('L"NV Speech Player"', 'L"TGSpeechBox"'),
    ('"NV Speech SAPI Settings"', '"TGSpeechBox SAPI Settings"'),
    ('L"NV Speech SAPI Settings"', 'L"TGSpeechBox SAPI Settings"'),
    ('L"NV Speech"', 'L"TGSpeechBox"'),

    # Registry/attribute keys
    ('L"NVSpeech_LangTag"', 'L"TGSpeech_LangTag"'),
    ('L"NVSpeech_Preset"', 'L"TGSpeech_Preset"'),

    # Registry key name (standalone, not part of longer string)
    # These are registry subkey names in sapi_main.cpp and IEnumSpObjectTokensImpl.cpp
    ('L"NVSpeech"', 'L"TGSpeech"'),

    # AppData folder, log filename, DLL name, debug strings
    ('NVSpeechSapi', 'TGSpeechSapi'),

    # Namespaces — inner first (nvsp:: before NVSpeech::)
    ('namespace nvsp', 'namespace tgsb'),
    ('nvsp::', 'tgsb::'),
    ('// namespace nvsp', '// namespace tgsb'),
    ('namespace NVSpeech', 'namespace TGSpeech'),
    ('NVSpeech::', 'TGSpeech::'),
    ('// namespace NVSpeech', '// namespace TGSpeech'),

    # Comments mentioning NV Speech Player (not function pointers)
    ('NV Speech Player', 'TGSpeechBox'),
    # But NOT: nvspFrontend_*, speechPlayer_*, L"nvspFrontend.dll", L"speechPlayer.dll"
]

# Patterns to PROTECT from replacement (function pointers, DLL names)
PROTECT_PATTERNS = [
    'nvspFrontend_',
    'speechPlayer_',
    '"nvspFrontend.dll"',
    '"speechPlayer.dll"',
    'L"nvspFrontend.dll"',
    'L"speechPlayer.dll"',
]

# ── Template C files (BSTSpeech lineage) ──
TEMPLATE_C_FILES = {
    "com.cpp", "com.hpp",
    "registry.cpp", "registry.hpp",
    "voice_token.cpp", "voice_token.hpp",
    "IEnumSpObjectTokensImpl.cpp", "IEnumSpObjectTokensImpl.hpp",
}

# ── File descriptions ──
DESCRIPTIONS = {
    # src/sapi/
    "sapi_main.cpp": "SAPI5 DLL entry point and COM registration.",
    "com.cpp": "COM object factory and class registration framework.",
    "com.hpp": "COM object factory and reference counting utilities.",
    "registry.cpp": "Windows registry helper utilities.",
    "registry.hpp": "Windows registry helper declarations.",
    "ISpDataKeyImpl.cpp": "SAPI ISpDataKey implementation for voice token attributes.",
    "ISpDataKeyImpl.hpp": "SAPI ISpDataKey interface.",
    "ISpTTSEngineImpl.cpp": "SAPI ISpTTSEngineSite TTS engine implementation.",
    "ISpTTSEngineImpl.hpp": "SAPI TTS engine interface.",
    "IEnumSpObjectTokensImpl.cpp": "SAPI voice token enumerator (discovers installed voices).",
    "IEnumSpObjectTokensImpl.hpp": "SAPI voice token enumerator interface.",
    "tgsb_runtime.cpp": "DLL runtime loader and synthesis pipeline for SAPI.",
    "tgsb_runtime.hpp": "SAPI runtime class and function pointer definitions.",
    "tgsb_settings.cpp": "SAPI wrapper settings (INI file load/save).",
    "tgsb_settings.hpp": "SAPI wrapper settings declarations.",
    "debug_log.h": "Compile-time debug logging macros for SAPI engine.",
    "utils.hpp": "String conversion and COM pointer utilities.",
    "voice_attributes.hpp": "Voice definition structure and attributes.",
    "voice_token.cpp": "SAPI voice token implementation.",
    "voice_token.hpp": "SAPI voice token interface.",
    "TGSpeechSapi.def": "SAPI DLL export definitions.",
    # tools/tgsbSapiSettings/
    "settings_app.cpp": "SAPI settings dialog application.",
    "settings_app.rc": "SAPI settings dialog resource definitions.",
    "resource.h": "SAPI settings dialog resource identifiers.",
}


def make_header(filename, is_template_c):
    desc = DESCRIPTIONS.get(filename, f"{filename}.")
    lines = ["/*"]
    lines.append(f"TGSpeechBox \u2014 {desc}")
    if is_template_c:
        lines.append("Based on BSTSpeech SAPI5 Wrapper (github.com/gozaltech/BstSpeech-sapi).")
    lines.append("Copyright 2025-2026 Tamas Geczy.")
    lines.append("Licensed under the MIT License. See LICENSE for details.")
    lines.append("*/")
    return "\n".join(lines) + "\n"


def protect_line(line):
    """Check if a line contains protected patterns that shouldn't be renamed."""
    for pat in PROTECT_PATTERNS:
        if pat in line:
            return True
    return False


def apply_replacements(content):
    """Apply renaming replacements line by line, protecting function pointer lines."""
    lines = content.split('\n')
    result = []
    for line in lines:
        if protect_line(line):
            result.append(line)
        else:
            for old, new in REPLACEMENTS:
                line = line.replace(old, new)
            result.append(line)
    return '\n'.join(result)


def process_file(filepath):
    basename = os.path.basename(filepath)

    with open(filepath, 'r', encoding='utf-8-sig') as f:
        content = f.read()

    # Step 1: Apply string replacements
    content = apply_replacements(content)

    # Step 2: Add license header (if not already present)
    if 'Licensed under the MIT License' not in content[:500]:
        is_c = basename in TEMPLATE_C_FILES
        header = make_header(basename, is_c)
        content = header + "\n" + content

    with open(filepath, 'w', encoding='utf-8', newline='\n') as f:
        f.write(content)

    return basename


def main():
    dry_run = "--dry-run" in sys.argv
    count = 0

    for dirpath in [SAPI_DIR, SETTINGS_DIR]:
        for fname in sorted(os.listdir(dirpath)):
            if not fname.endswith(('.cpp', '.hpp', '.h', '.def', '.rc')):
                continue
            filepath = os.path.join(dirpath, fname)
            relpath = os.path.relpath(filepath, ROOT)

            if dry_run:
                print(f"  WOULD process: {relpath}")
            else:
                basename = process_file(filepath)
                is_c = basename in TEMPLATE_C_FILES
                tmpl = "C" if is_c else "B"
                print(f"  [{tmpl}] {relpath}")
                count += 1

    print(f"\nDone. {count} files processed.")


if __name__ == "__main__":
    main()

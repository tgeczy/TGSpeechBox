#!/usr/bin/env python3
"""
One-shot script to update license headers across TGSpeechBox.
Replaces GPL / partially-updated headers with MIT.
"""

import os
import re
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))

# ── Template A files (NV Access lineage) ──
# Matched by basename only.
TEMPLATE_A_BASENAMES = {
    "frame.cpp", "frame.h",
    "speechPlayer.cpp", "speechPlayer.h",
    "speechWaveGenerator.cpp", "speechWaveGenerator.h",
    "utils.h", "waveGenerator.h", "sample.h", "lock.h", "debug.h",
}

# ── Descriptions keyed by relative path (forward-slash normalized) ──
DESCRIPTIONS = {
    # src/ — DSP core (Template A)
    "src/frame.cpp": "Frame interpolation and queue management.",
    "src/frame.h": "Frame struct and queue definitions.",
    "src/speechPlayer.cpp": "Public C API for the DSP engine.",
    "src/speechPlayer.h": "Public C API header for the DSP engine.",
    "src/speechWaveGenerator.cpp": "Main speech wave generator (orchestrator).",
    "src/speechWaveGenerator.h": "Speech wave generator interface.",
    "src/utils.h": "Shared utility functions and math helpers.",
    "src/waveGenerator.h": "Wave generator base class.",
    "src/sample.h": "Audio sample type definitions.",
    "src/lock.h": "Lightweight locking primitives.",
    "src/debug.h": "Debug logging macros.",

    # src/ — DSP modules (Template B)
    "src/pitchModel.h": "Fujisaki-Bartman pitch contour model for the DSP.",
    "src/dspCommon.h": "DSP tuning constants, PRNG, lowpass, and utility classes.",
    "src/formantGenerator.h": "Cascade and parallel formant filter topologies.",
    "src/resonator.h": "All-pole resonator and pitch-synchronous F1 resonator.",
    "src/voiceGenerator.h": "LF glottal source with tilt, breathiness, and tremor.",
    "src/voicingTone.h": "VoicingTone parameter struct.",

    # src/frontend/
    "src/frontend/ipa_engine.cpp": "IPA-to-frame conversion engine.",
    "src/frontend/ipa_engine.h": "IPA engine interface and Token definitions.",
    "src/frontend/nvspFrontend.cpp": "Frontend public C API implementation.",
    "src/frontend/nvspFrontend.h": "Frontend public C API and FrameEx struct.",
    "src/frontend/pack.cpp": "Language pack YAML parser.",
    "src/frontend/pack.h": "Language pack data structures and phoneme definitions.",
    "src/frontend/frame_emit.cpp": "Token-to-FrameEx conversion and emission.",
    "src/frontend/text_parser.cpp": "Text parser with CMU Dict stress correction.",
    "src/frontend/text_parser.h": "Text parser interface.",
    "src/frontend/voice_profile.cpp": "Voice profile application (class scales and overrides).",
    "src/frontend/voice_profile.h": "Voice profile definitions.",
    "src/frontend/utf8.cpp": "UTF-8 encoding and decoding utilities.",
    "src/frontend/utf8.h": "UTF-8 utility function declarations.",
    "src/frontend/yaml_min.cpp": "Minimal YAML parser implementation.",
    "src/frontend/yaml_min.h": "Minimal YAML parser interface.",

    # src/frontend/passes/
    "src/frontend/passes/pass_pipeline.cpp": "Pass pipeline registration and execution.",
    "src/frontend/passes/pass_pipeline.h": "Pass pipeline interface.",
    "src/frontend/passes/pass_common.h": "Shared types and helpers for frontend passes.",
    "src/frontend/passes/allophones.cpp": "YAML-driven allophone rule engine.",
    "src/frontend/passes/allophones.h": "Allophone pass interface.",
    "src/frontend/passes/boundary_smoothing.cpp": "Boundary smoothing pass (crossfade and formant transitions).",
    "src/frontend/passes/boundary_smoothing.h": "Boundary smoothing pass interface.",
    "src/frontend/passes/cluster_blend.cpp": "Cluster blend pass (C-to-C formant coarticulation).",
    "src/frontend/passes/cluster_blend.h": "Cluster blend pass interface.",
    "src/frontend/passes/cluster_timing.cpp": "Cluster timing pass (consonant cluster duration scaling).",
    "src/frontend/passes/cluster_timing.h": "Cluster timing pass interface.",
    "src/frontend/passes/coarticulation.cpp": "MITalk-style locus coarticulation pass.",
    "src/frontend/passes/coarticulation.h": "Coarticulation pass interface.",
    "src/frontend/passes/length_contrast.cpp": "Phonemic length contrast and gemination pass.",
    "src/frontend/passes/length_contrast.h": "Length contrast pass interface.",
    "src/frontend/passes/liquid_dynamics.cpp": "Liquid dynamics pass (lateral onglide, rhotic F3 dip).",
    "src/frontend/passes/liquid_dynamics.h": "Liquid dynamics pass interface.",
    "src/frontend/passes/microprosody.cpp": "Microprosody pass (voiceless F0 raise, voiced F0 lower).",
    "src/frontend/passes/microprosody.h": "Microprosody pass interface.",
    "src/frontend/passes/nasalization.cpp": "Anticipatory nasalization pass.",
    "src/frontend/passes/nasalization.h": "Nasalization pass interface.",
    "src/frontend/passes/pitch_fujisaki.cpp": "Fujisaki pitch model pass.",
    "src/frontend/passes/pitch_fujisaki.h": "Fujisaki pitch model pass interface.",
    "src/frontend/passes/prominence.cpp": "Prominence pass (stress scoring and duration/amplitude realization).",
    "src/frontend/passes/prominence.h": "Prominence pass interface.",
    "src/frontend/passes/prosody.cpp": "Prosody pass (phrase-final lengthening).",
    "src/frontend/passes/prosody.h": "Prosody pass interface.",
    "src/frontend/passes/reduction.cpp": "Rate-dependent reduction pass.",
    "src/frontend/passes/reduction.h": "Reduction pass interface.",
    "src/frontend/passes/special_coartic.cpp": "Special coarticulation pass (language-specific formant deltas).",
    "src/frontend/passes/special_coartic.h": "Special coarticulation pass interface.",
    "src/frontend/passes/trajectory_limit.cpp": "Trajectory limiting pass (formant rate capping).",
    "src/frontend/passes/trajectory_limit.h": "Trajectory limiting pass interface.",

    # tools/tgsbPhonemeEditorWin32/
    "tools/tgsbPhonemeEditorWin32/main.cpp": "Phoneme editor Win32 entry point.",
    "tools/tgsbPhonemeEditorWin32/appcontroller.cpp": "Phoneme editor main application controller.",
    "tools/tgsbPhonemeEditorWin32/AppController.h": "Phoneme editor application controller interface.",
    "tools/tgsbPhonemeEditorWin32/tgsb_runtime.cpp": "DLL runtime loader for phoneme editor.",
    "tools/tgsbPhonemeEditorWin32/tgsb_runtime.h": "DLL runtime loader interface.",
    "tools/tgsbPhonemeEditorWin32/AccessibilityUtils.cpp": "Win32 accessibility and screen reader utilities.",
    "tools/tgsbPhonemeEditorWin32/AccessibilityUtils.h": "Accessibility utilities interface.",
    "tools/tgsbPhonemeEditorWin32/Dialogs.cpp": "Phoneme editor dialog implementations.",
    "tools/tgsbPhonemeEditorWin32/Dialogs.h": "Phoneme editor dialog declarations.",
    "tools/tgsbPhonemeEditorWin32/RulesEditor.cpp": "Allophone rules editor UI.",
    "tools/tgsbPhonemeEditorWin32/RulesEditor.h": "Allophone rules editor interface.",
    "tools/tgsbPhonemeEditorWin32/VoiceProfileEditor.cpp": "Voice profile editor UI.",
    "tools/tgsbPhonemeEditorWin32/VoiceProfileEditor.h": "Voice profile editor interface.",
    "tools/tgsbPhonemeEditorWin32/WinUtils.cpp": "Win32 utility functions.",
    "tools/tgsbPhonemeEditorWin32/WinUtils.h": "Win32 utility function declarations.",
    "tools/tgsbPhonemeEditorWin32/chunking.cpp": "Text chunking for speech synthesis.",
    "tools/tgsbPhonemeEditorWin32/chunking.h": "Text chunking interface.",
    "tools/tgsbPhonemeEditorWin32/phonemizer_cli.cpp": "eSpeak phonemizer CLI wrapper.",
    "tools/tgsbPhonemeEditorWin32/phonemizer_cli.h": "eSpeak phonemizer CLI interface.",
    "tools/tgsbPhonemeEditorWin32/process_util.cpp": "Process management utilities.",
    "tools/tgsbPhonemeEditorWin32/process_util.h": "Process management utility declarations.",
    "tools/tgsbPhonemeEditorWin32/resource.h": "Win32 resource identifiers for phoneme editor.",
    "tools/tgsbPhonemeEditorWin32/wav_writer.cpp": "WAV file writer.",
    "tools/tgsbPhonemeEditorWin32/wav_writer.h": "WAV file writer interface.",
    "tools/tgsbPhonemeEditorWin32/yaml_edit.cpp": "YAML round-trip editor for phoneme data.",
    "tools/tgsbPhonemeEditorWin32/yaml_edit.h": "YAML round-trip editor interface.",

    # tools/
    "tools/tgsbRender.cpp": "Command-line IPA-to-PCM renderer for Speech Dispatcher.",
}

SKIP_DIRS = {"build-x64", "build-x86", ".git", "node_modules"}

GPL_MARKERS = [
    "GNU General Public License",
    "GPL",
    "bitbucket.org/nvaccess",
    "Licensed under GNU",
]


def is_template_a(basename: str) -> bool:
    return basename in TEMPLATE_A_BASENAMES


def make_header(description: str, template_a: bool) -> str:
    lines = ["/*"]
    lines.append(f"TGSpeechBox \u2014 {description}")
    if template_a:
        lines.append("Copyright 2014 NV Access Limited.")
    lines.append("Copyright 2025-2026 Tamas Geczy.")
    lines.append("Licensed under the MIT License. See LICENSE for details.")
    lines.append("*/")
    return "\n".join(lines) + "\n"


def strip_old_header(content: str):
    """
    If the file starts with a /* ... */ block that contains GPL markers,
    strip it and return (stripped_content, had_header).
    """
    stripped = content.lstrip("\ufeff")  # BOM
    if not stripped.startswith("/*"):
        return content, False

    end = stripped.find("*/")
    if end < 0:
        return content, False

    block = stripped[: end + 2]

    # Check if this block is a license header (not just documentation)
    is_license = False
    for marker in GPL_MARKERS:
        if marker in block:
            is_license = True
            break

    # Also match the partially-updated TGSpeechBox header pattern
    if "TGSpeechBox" in block and "Licensed under" in block:
        is_license = True
    if "NV Speech Player" in block and "Licensed under" in block:
        is_license = True

    # Match blocks whose first content line starts with "TGSpeechBox" —
    # these are old headers even if they lack the "Licensed under" line.
    # (Does NOT match "TGSBRender" or other tool-specific doc blocks.)
    first_content = block.lstrip("/* \t\n")
    if first_content.startswith("TGSpeechBox"):
        is_license = True

    if not is_license:
        return content, False

    # Strip the block + any trailing blank lines
    rest = stripped[end + 2:]
    rest = rest.lstrip("\n")
    return rest, True


def get_description(relpath: str, basename: str) -> str:
    """Get description from the dictionary, or generate a fallback."""
    # Try exact relpath match
    key = relpath.replace("\\", "/")
    if key in DESCRIPTIONS:
        return DESCRIPTIONS[key]
    # Fallback: derive from filename
    name = os.path.splitext(basename)[0]
    # Convert camelCase/PascalCase to words
    words = re.sub(r"([a-z])([A-Z])", r"\1 \2", name)
    words = words.replace("_", " ")
    return f"{words}."


def collect_files():
    """Find all .cpp/.h files, excluding build dirs."""
    result = []
    for dirpath, dirnames, filenames in os.walk(ROOT):
        # Prune skip dirs
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
        for f in filenames:
            if f.endswith((".cpp", ".h")):
                fullpath = os.path.join(dirpath, f)
                relpath = os.path.relpath(fullpath, ROOT)
                result.append((fullpath, relpath, f))
    return result


def main():
    dry_run = "--dry-run" in sys.argv

    files = collect_files()
    modified = 0
    skipped = 0

    for fullpath, relpath, basename in sorted(files):
        with open(fullpath, "r", encoding="utf-8-sig") as fh:
            content = fh.read()

        content_stripped, had_old = strip_old_header(content)

        # Check if file already has the MIT header
        if "Licensed under the MIT License" in content[:500]:
            print(f"  SKIP (already MIT): {relpath}")
            skipped += 1
            continue

        ta = is_template_a(basename)
        desc = get_description(relpath, basename)
        header = make_header(desc, ta)

        new_content = header + "\n" + content_stripped

        action = "REPLACE" if had_old else "ADD"
        template = "A" if ta else "B"
        print(f"  {action} [{template}]: {relpath}")

        if not dry_run:
            with open(fullpath, "w", encoding="utf-8", newline="\n") as fh:
                fh.write(new_content)
            modified += 1

    print(f"\nDone. {modified} files {'would be ' if dry_run else ''}modified, {skipped} skipped.")


if __name__ == "__main__":
    main()

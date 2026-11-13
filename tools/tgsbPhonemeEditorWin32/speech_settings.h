/*
TGSpeechBox — Speech settings of the Win32 phoneme editor.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

// The settings Speak and Save WAV play with, and what a Save to Profile in
// the speech settings dialog makes of them.  No Win32 here, so tests/cpp can
// check it on every platform.

#pragma once

#include <string>
#include <vector>

namespace tgsb_editor {

// Mirrors the NVDA driver's public-facing speech settings.
// - Voice: preset that applies multipliers/overrides to the generated frames.
// - Rate/Pitch/Volume/Inflection: passed to nvspFrontend.dll (and/or applied to frames).
// - frameParams: 0..100 sliders that act as multipliers on each speechPlayer frame field,
//   with 50 meaning "neutral" (x1.0).
// - voicingParams: 0..100 sliders for VoicingTone parameters.
struct SpeechSettings {
  std::string voiceName = "Adam";
  int rate = 50;       // 0..100
  int pitch = 50;      // 0..100
  int volume = 90;     // 0..100
  int inflection = 50; // 0..100; 50 = the NVDA, Android and iOS default, so the editor's neutral is the listener's
  std::string pauseMode = "short"; // off | short | long
  std::vector<int> frameParams;   // size == frameParamNames().size()
  std::vector<int> voicingParams; // size == voicingParamNames().size()
  std::vector<int> frameExParams; // size == frameExParamNames().size() - FrameEx voice quality
};

// The Inflection slider position a profile's inflection scale is written
// for: Save to Profile puts the slider's distance from it into the scale.
constexpr int kInflectionNeutral = 50;

// The inflection scale Save to Profile suggests: the profile's stored scale
// times the Inflection slider's distance from neutral, so the profile at a
// neutral slider keeps the inflection that is heard now.  A scale is at
// most 3.
double inflectionScaleKeepingSound(double storedScale, int inflectionSlider);

// A successful Save to Profile in the speech settings dialog (#220).
//
// The save has written phonemes.yaml, and the profile's scale now holds what
// the Inflection slider held, so the settings have to follow at once: the
// old slider position on top of the new scale would play the inflection
// twice.  `dialog` is the dialog's copy, its voicing sliders already showing
// the saved profile's stored voice source; it gets the saved profile as its
// voice (`savedVoice`, with the profile prefix) and the Inflection slider at
// neutral.  `applied` is what the runtime and the INI hold.
//
// Returns what the runtime and the INI hold from now on, which is also what
// Cancel leaves in place: `applied` with the saved profile, its voicing
// sliders and the neutral Inflection slider.  Rate, pitch, volume, the frame
// sliders and the voice quality sliders are no part of a profile; they stay
// as `applied` has them until OK, so Cancel after a save drops them, and
// anything changed after the save, as it always did.
SpeechSettings speechSettingsAfterProfileSave(SpeechSettings& dialog,
                                              const SpeechSettings& applied,
                                              const std::string& savedVoice);

} // namespace tgsb_editor

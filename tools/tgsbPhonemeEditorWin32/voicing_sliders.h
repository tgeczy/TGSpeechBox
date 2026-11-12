/*
TGSpeechBox — Voicing sliders of the Win32 phoneme editor.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

// The speech settings dialog's voicing sliders (0-100): what value each
// position stands for, and where each slider is neutral.  No Win32 here, so
// tests/cpp can check it on every platform.

#pragma once

#include <cstddef>
#include <vector>

namespace tgsb_editor {

// The sliders, in TgsbRuntime::voicingParamNames() order.
constexpr int kVoicingSliderCount = 19;

// Slider position to VoicingTone value, and the position closest to a value.
double mapVoicingSliderToValue(int paramIndex, int sliderValue);
int mapVoicingValueToSlider(int paramIndex, double v);

// The position at which a slider leaves the voice as a built-in voice plays
// it: 50 for most, 0 for noise modulation, tremor and chorus depth, 33 for
// chorus detune.
int voicingSliderNeutral(int paramIndex);

// "Reset" on one slider, and "Reset all" (count sliders).
int voicingSliderResetValue(int paramIndex);
void resetVoicingSliders(std::vector<int>& sliders, size_t count);

} // namespace tgsb_editor

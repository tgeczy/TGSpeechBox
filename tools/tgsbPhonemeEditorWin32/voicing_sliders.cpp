/*
TGSpeechBox — Voicing sliders of the Win32 phoneme editor.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#include "voicing_sliders.h"

#include <cmath>

namespace tgsb_editor {

static int clampInt(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

// Voicing sliders (0-100) to VoicingTone values: two straight pieces per
// parameter, low end at 0, the value a built-in voice plays at the neutral
// position (50) in the middle, high end at 100.  The middle values are the
// DSP defaults and match the NVDA driver's sliders where it has them (speed
// quotient, formant sharpness, head size), so a voice at neutral sliders
// sounds the same in the editor and in NVDA, and a profile saved from
// neutral sliders writes nothing.  (Before 3.10 beta 10 the first six were
// straight lines whose midpoints were not the defaults: neutral sliders
// wrote pre-emphasis 0.485 and a 0 dB high shelf into every saved profile.)
struct VoicingSliderMap { double lo, mid, hi; };
static const VoicingSliderMap kVoicingSliderMap[] = {
  {0.85, 0.91, 0.95},     // 0 voicingPeakPos
  {0.0, 0.92, 0.97},      // 1 voicedPreEmphA
  {0.0, 0.35, 1.0},       // 2 voicedPreEmphMix
  {-12.0, 4.0, 12.0},     // 3 highShelfGainDb
  {500.0, 2000.0, 8000.0},// 4 highShelfFcHz
  {0.3, 0.7, 2.0},        // 5 highShelfQ
  {-24.0, 0.0, 24.0},     // 6 voicedTiltDbPerOct
  {0.0, 0.5, 1.0},        // 7 noiseGlottalModDepth (neutral = slider 0)
  {-60.0, 0.0, 60.0},     // 8 pitchSyncF1DeltaHz
  {-50.0, 0.0, 50.0},     // 9 pitchSyncB1DeltaHz
  {0.5, 2.0, 4.0},        // 10 speedQuotient (as NVDA)
  {-12.0, 0.0, 12.0},     // 11 aspirationTiltDbPerOct
  {2.0, 1.0, 0.3},        // 12 cascadeBwScale (as NVDA)
  {0.0, 0.2, 0.4},        // 13 tremorDepth (neutral = slider 0)
  {0.5, 1.0, 2.0},        // 14 nasalBwScale
  {1.25, 1.0, 0.85},      // 15 f4FreqScale (as NVDA's head size)
  {0.5, 1.0, 1.5},        // 16 nasalGainScale
  {0.0, 0.5, 1.0},        // 17 chorusDepth (neutral = slider 0)
  {0.5, 2.75, 5.0},       // 18 chorusDetuneHz (neutral = slider 33)
};
static constexpr int kVoicingSliderMapCount = static_cast<int>(sizeof(kVoicingSliderMap) / sizeof(kVoicingSliderMap[0]));
static_assert(kVoicingSliderMapCount == kVoicingSliderCount, "one map entry per voicing slider");

double mapVoicingSliderToValue(int paramIndex, int sliderValue) {
  if (paramIndex < 0 || paramIndex >= kVoicingSliderMapCount) return 0.0;
  const VoicingSliderMap& m = kVoicingSliderMap[paramIndex];
  const double sv = static_cast<double>(clampInt(sliderValue, 0, 100));
  if (sv <= 50.0) return m.lo + (sv / 50.0) * (m.mid - m.lo);
  return m.mid + ((sv - 50.0) / 50.0) * (m.hi - m.mid);
}

// The slider position whose value is closest to v (the inverse of the above).
int mapVoicingValueToSlider(int paramIndex, double v) {
  if (paramIndex < 0 || paramIndex >= kVoicingSliderMapCount) return 50;
  const VoicingSliderMap& m = kVoicingSliderMap[paramIndex];
  double s;
  if ((v - m.mid) * (m.lo - m.mid) >= 0.0) {
    s = (m.mid == m.lo) ? 0.0 : (v - m.lo) / (m.mid - m.lo) * 50.0;
  } else {
    s = (m.hi == m.mid) ? 100.0 : 50.0 + (v - m.mid) / (m.hi - m.mid) * 50.0;
  }
  return clampInt(static_cast<int>(std::lround(s)), 0, 100);
}

int voicingSliderNeutral(int paramIndex) {
  // noiseGlottalModDepth (7), tremorDepth (13) and chorusDepth (17) are
  // neutral at 0; chorusDetuneHz (18) at 33.
  if (paramIndex == 7 || paramIndex == 13 || paramIndex == 17) return 0;
  if (paramIndex == 18) return 33;
  return 50;
}

// Both Resets go by the one neutral table.  "Reset" used to set 50 whatever
// the slider, and "Reset all" a list of its own that left out noise
// modulation, so a reset voice played 0.5 of it and Save to Profile wrote
// that into the profile (#218).
int voicingSliderResetValue(int paramIndex) {
  return voicingSliderNeutral(paramIndex);
}

void resetVoicingSliders(std::vector<int>& sliders, size_t count) {
  sliders.assign(count, 50);
  for (size_t i = 0; i < sliders.size(); ++i) sliders[i] = voicingSliderNeutral(static_cast<int>(i));
}

} // namespace tgsb_editor

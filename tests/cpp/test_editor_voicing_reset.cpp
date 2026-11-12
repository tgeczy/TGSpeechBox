// The phoneme editor's voicing Reset buttons (#218).
//
// The speech settings dialog has a slider per voice-source parameter.  Most
// are neutral in the middle (50); noise modulation, tremor and chorus depth
// are neutral at 0, chorus detune at 33.  "Reset" on one slider always set
// 50, and "Reset all" set 50 and then put right three of the four that are
// not neutral there, leaving noise modulation (noiseGlottalModDepth) at 50:
// the preview then played a modulation depth of 0.5, and Save to Profile
// wrote `noiseGlottalModDepth: 0.5` into the profile.  A per-slider Reset of
// tremor gave tremorDepth 0.2 the same way.
//
// What is checked here: after either Reset, every slider sends the engine
// the value a built-in voice plays with no slider moved.

#include "doctest.h"

#include "voicing_sliders.h"

#include <string>
#include <vector>

using tgsb_editor::kVoicingSliderCount;

namespace {

// What a built-in voice plays with every slider untouched: the DSP's
// defaults (speechPlayer's VoicingTone, and the editor's kToneNeutral), in
// slider order.  Chorus detune has no audible value while chorus depth is 0;
// its slider's resting position is 33.
struct Neutral { const char* name; double value; };
const Neutral kNeutral[kVoicingSliderCount] = {
  {"voicingPeakPos", 0.91}, {"voicedPreEmphA", 0.92}, {"voicedPreEmphMix", 0.35},
  {"highShelfGainDb", 4.0}, {"highShelfFcHz", 2000.0}, {"highShelfQ", 0.7},
  {"voicedTiltDbPerOct", 0.0}, {"noiseGlottalModDepth", 0.0},
  {"pitchSyncF1DeltaHz", 0.0}, {"pitchSyncB1DeltaHz", 0.0},
  {"speedQuotient", 2.0}, {"aspirationTiltDbPerOct", 0.0}, {"cascadeBwScale", 1.0},
  {"tremorDepth", 0.0}, {"nasalBwScale", 1.0}, {"f4FreqScale", 1.0}, {"nasalGainScale", 1.0},
  {"chorusDepth", 0.0}, {"chorusDetuneHz", tgsb_editor::mapVoicingSliderToValue(18, 33)},
};

}  // namespace

TEST_CASE("voicing reset: Reset all leaves every slider where it has no effect") {
  std::vector<int> sliders(kVoicingSliderCount, 77);
  tgsb_editor::resetVoicingSliders(sliders, kVoicingSliderCount);
  REQUIRE(sliders.size() == static_cast<size_t>(kVoicingSliderCount));
  for (int i = 0; i < kVoicingSliderCount; ++i) {
    INFO("slider " << i << ": " << std::string(kNeutral[i].name));
    CHECK(sliders[static_cast<size_t>(i)] == tgsb_editor::voicingSliderNeutral(i));
    CHECK(tgsb_editor::mapVoicingSliderToValue(i, sliders[static_cast<size_t>(i)]) ==
          doctest::Approx(kNeutral[i].value));
  }
  // The one Reset all missed: breath and hiss noise pulsing with the voice.
  CHECK(sliders[7] == 0);
  CHECK(tgsb_editor::mapVoicingSliderToValue(7, sliders[7]) == 0.0);
}

TEST_CASE("voicing reset: Reset on one slider puts it where it has no effect") {
  for (int i = 0; i < kVoicingSliderCount; ++i) {
    INFO("slider " << i << ": " << std::string(kNeutral[i].name));
    const int pos = tgsb_editor::voicingSliderResetValue(i);
    CHECK(pos == tgsb_editor::voicingSliderNeutral(i));
    CHECK(tgsb_editor::mapVoicingSliderToValue(i, pos) == doctest::Approx(kNeutral[i].value));
  }
  // The four whose neutral is not the middle.
  CHECK(tgsb_editor::mapVoicingSliderToValue(7, tgsb_editor::voicingSliderResetValue(7)) == 0.0);    // noise modulation
  CHECK(tgsb_editor::mapVoicingSliderToValue(13, tgsb_editor::voicingSliderResetValue(13)) == 0.0);  // tremor
  CHECK(tgsb_editor::mapVoicingSliderToValue(17, tgsb_editor::voicingSliderResetValue(17)) == 0.0);  // chorus depth
  CHECK(tgsb_editor::voicingSliderResetValue(18) == 33);                                             // chorus detune
}

TEST_CASE("voicing reset: Reset all sizes the list to the sliders there are") {
  std::vector<int> sliders;
  tgsb_editor::resetVoicingSliders(sliders, 14);  // an older layout: up to tremor
  REQUIRE(sliders.size() == 14);
  CHECK(sliders[7] == 0);
  CHECK(sliders[13] == 0);
  CHECK(sliders[0] == 50);
}

TEST_CASE("voicing reset: a slider's neutral position maps to its value and back") {
  for (int i = 0; i < kVoicingSliderCount; ++i) {
    INFO("slider " << i << ": " << std::string(kNeutral[i].name));
    const int neutral = tgsb_editor::voicingSliderNeutral(i);
    CHECK(tgsb_editor::mapVoicingValueToSlider(i, tgsb_editor::mapVoicingSliderToValue(i, neutral)) == neutral);
  }
}

// The phoneme editor's Save to Profile and the chorus sliders (#219).
//
// A voice profile's voice source can carry chorus since 3.10 (#124): the
// frontend reads chorusDepth and chorusDetuneHz from its voicingTone.  The
// editor has a slider for each, but its list of the keys a profile's voice
// source holds stopped at 17, before them.  Save to Profile dropped the
// chorus sliders without a word, and loading a profile that has chorus left
// the sliders where they were, so the preview played the sliders' chorus,
// not the profile's.
//
// What is checked here: the sliders' chorus reaches the saved profile, and
// the frontend reads it from there; a profile's chorus reaches the sliders;
// neutral sliders still write nothing, chorus included; a Reset slider
// writes nothing (the second half of #218).

#include "doctest.h"

#include "editor_test_util.h"
#include "profile_tone.h"
#include "voice_profile.h"
#include "voicing_sliders.h"

#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace tgsb_test;
using tgsb_editor::ProfileSaveRequest;
using tgsb_editor::VPVoiceProfile;

namespace {

const int kChorusDepth = 17;   // slider indexes, in voicingParamNames order
const int kChorusDetune = 18;
const int kNoiseMod = 7;
const int kTremor = 13;

std::vector<int> neutralSliders() {
  std::vector<int> s;
  tgsb_editor::resetVoicingSliders(s, tgsb_editor::kVoicingSliderCount);
  return s;
}

// Save to Profile from a built-in voice, under a new name: every slider counts.
VPVoiceProfile saveNew(const std::vector<int>& sliders, std::vector<VPVoiceProfile>* all = nullptr) {
  std::vector<VPVoiceProfile> profiles;
  ProfileSaveRequest req;
  req.name = "test";
  req.baseVoice = "Adam";
  tgsb_editor::slidersToSaveRequest(sliders, "", {}, req);
  std::string note;
  tgsb_editor::applyProfileSave(profiles, req, note);
  REQUIRE(profiles.size() == 1);
  if (all) *all = profiles;
  return profiles[0];
}

double toneNumber(const VPVoiceProfile& p, const char* key) {
  auto it = p.voicingTone.find(key);
  REQUIRE_MESSAGE(it != p.voicingTone.end(), "no " << std::string(key) << " in the profile's voicingTone");
  return std::stod(it->second);
}

// The frontend's own reading of a profile with this voicingTone.
nvsp_frontend::VoicingTone frontendTone(const VPVoiceProfile& p) {
  std::string yaml = "phonemes:\n  a:\n    cf1: 700\nvoiceProfiles:\n  test:\n    voicingTone:\n";
  for (const auto& kv : p.voicingTone) yaml += "      " + kv.first + ": " + kv.second + "\n";
  const fs::path packs = editorScratchDir("profile_chorus_frontend") / "packs";
  writeAllBytes(packs / "phonemes.yaml", yaml);
  fs::create_directories(packs / "lang");
  fs::copy_file(shippedLangFile("default"), packs / "lang" / "default.yaml");
  nvsp_frontend::PackSet pack;
  std::string err;
  REQUIRE_MESSAGE(nvsp_frontend::loadPackSet(packs.parent_path().string(), "default", pack, err), err);
  REQUIRE(pack.voiceProfiles != nullptr);
  const nvsp_frontend::VoiceProfile* vp = pack.voiceProfiles->getProfile("test");
  REQUIRE(vp != nullptr);
  REQUIRE(vp->hasVoicingTone);
  return vp->voicingTone;
}

}  // namespace

TEST_CASE("profile chorus: the chorus sliders are among the keys a profile's voice source holds") {
  REQUIRE(tgsb_editor::kProfileToneKeyCount == tgsb_editor::kVoicingSliderCount);
  CHECK(std::string(tgsb_editor::kProfileToneKeys[kChorusDepth]) == "chorusDepth");
  CHECK(std::string(tgsb_editor::kProfileToneKeys[kChorusDetune]) == "chorusDetuneHz");
  // A built-in voice has no chorus: depth 0, and the engine's 2 Hz detune.
  CHECK(tgsb_editor::profileToneNeutral(kChorusDepth) == 0.0);
  CHECK(tgsb_editor::profileToneNeutral(kChorusDetune) == 2.0);
}

TEST_CASE("profile chorus: Save to Profile writes the chorus sliders, and the frontend reads them") {
  std::vector<int> sliders = neutralSliders();
  sliders[kChorusDepth] = 50;                                              // depth 0.5
  sliders[kChorusDetune] = tgsb_editor::mapVoicingValueToSlider(kChorusDetune, 3.0);
  REQUIRE(tgsb_editor::mapVoicingSliderToValue(kChorusDepth, 50) == doctest::Approx(0.5));
  const double detune = tgsb_editor::mapVoicingSliderToValue(kChorusDetune, sliders[kChorusDetune]);
  REQUIRE(detune == doctest::Approx(3.0).epsilon(0.02));

  const VPVoiceProfile saved = saveNew(sliders);
  CHECK(saved.hasVoicingTone);
  CHECK(toneNumber(saved, "chorusDepth") == doctest::Approx(0.5));
  CHECK(toneNumber(saved, "chorusDetuneHz") == doctest::Approx(detune));

  const nvsp_frontend::VoicingTone tone = frontendTone(saved);
  CHECK(tone.chorusDepth_set);
  CHECK(tone.chorusDepth == doctest::Approx(0.5));
  CHECK(tone.chorusDetuneHz_set);
  CHECK(tone.chorusDetuneHz == doctest::Approx(detune));
}

TEST_CASE("profile chorus: a profile's chorus moves the chorus sliders") {
  VPVoiceProfile p;
  p.name = "choir";
  p.hasVoicingTone = true;
  p.voicingTone["chorusDepth"] = "0.4";
  p.voicingTone["chorusDetuneHz"] = "3.5";
  std::vector<int> sliders = neutralSliders();
  std::vector<double> values;
  tgsb_editor::profileToneToSliders(p, sliders, &values);
  REQUIRE(sliders.size() == static_cast<size_t>(tgsb_editor::kVoicingSliderCount));
  CHECK(tgsb_editor::mapVoicingSliderToValue(kChorusDepth, sliders[kChorusDepth]) == doctest::Approx(0.4));
  CHECK(tgsb_editor::mapVoicingSliderToValue(kChorusDetune, sliders[kChorusDetune]) == doctest::Approx(3.5).epsilon(0.02));
  // The exact stored values, for the preview of sliders that did not move.
  REQUIRE(values.size() == static_cast<size_t>(tgsb_editor::kVoicingSliderCount));
  CHECK(values[kChorusDepth] == 0.4);
  CHECK(values[kChorusDetune] == 3.5);
}

TEST_CASE("profile chorus: a profile without chorus puts the chorus sliders at neutral") {
  VPVoiceProfile p;
  p.name = "plain";
  p.hasVoicingTone = true;
  p.voicingTone["speedQuotient"] = "2.4";
  std::vector<int> sliders(tgsb_editor::kVoicingSliderCount, 77);
  tgsb_editor::profileToneToSliders(p, sliders, nullptr);
  CHECK(sliders[kChorusDepth] == tgsb_editor::voicingSliderNeutral(kChorusDepth));
  CHECK(sliders[kChorusDetune] == tgsb_editor::voicingSliderNeutral(kChorusDetune));
}

TEST_CASE("profile chorus: neutral sliders write no voice source at all") {
  const VPVoiceProfile saved = saveNew(neutralSliders());
  INFO("keys written: " << saved.voicingTone.size());
  CHECK(saved.voicingTone.empty());
  CHECK(!saved.hasVoicingTone);
}

TEST_CASE("profile chorus: a slider put back with Reset writes nothing") {
  // Tremor and noise modulation, moved and then Reset (#218).
  std::vector<int> sliders = neutralSliders();
  sliders[kTremor] = 80;
  sliders[kNoiseMod] = 80;
  sliders[kChorusDepth] = 80;
  sliders[kTremor] = tgsb_editor::voicingSliderResetValue(kTremor);
  sliders[kNoiseMod] = tgsb_editor::voicingSliderResetValue(kNoiseMod);
  sliders[kChorusDepth] = tgsb_editor::voicingSliderResetValue(kChorusDepth);
  const VPVoiceProfile saved = saveNew(sliders);
  CHECK(saved.voicingTone.count("tremorDepth") == 0);
  CHECK(saved.voicingTone.count("noiseGlottalModDepth") == 0);
  CHECK(saved.voicingTone.count("chorusDepth") == 0);
  CHECK(saved.voicingTone.empty());
}

TEST_CASE("profile chorus: saving a profile over itself keeps chorus whose sliders did not move") {
  VPVoiceProfile p;
  p.name = "choir";
  p.hasVoicingTone = true;
  p.voicingTone["chorusDepth"] = "0.37";       // not a slider step
  p.voicingTone["chorusDetuneHz"] = "3.33";
  std::vector<VPVoiceProfile> profiles = {p};

  std::vector<int> loaded = neutralSliders();
  tgsb_editor::profileToneToSliders(p, loaded, nullptr);
  std::vector<int> sliders = loaded;
  sliders[10] = 70;  // speed quotient moved; chorus untouched

  ProfileSaveRequest req;
  req.name = "choir";
  req.sourceProfile = "choir";
  tgsb_editor::slidersToSaveRequest(sliders, "choir", loaded, req);
  std::string note;
  tgsb_editor::applyProfileSave(profiles, req, note);
  REQUIRE(profiles.size() == 1);
  CHECK(profiles[0].voicingTone.at("chorusDepth") == "0.37");
  CHECK(profiles[0].voicingTone.at("chorusDetuneHz") == "3.33");
  CHECK(profiles[0].voicingTone.count("speedQuotient") == 1);

  // Moving the chorus depth slider to 0 takes the chorus out.
  sliders[kChorusDepth] = 0;
  tgsb_editor::slidersToSaveRequest(sliders, "choir", loaded, req);
  tgsb_editor::applyProfileSave(profiles, req, note);
  CHECK(profiles[0].voicingTone.count("chorusDepth") == 0);
}

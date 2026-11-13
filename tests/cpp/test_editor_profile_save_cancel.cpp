// The phoneme editor's Save to Profile, and Cancel after it (#220).
//
// Save to Profile in the speech settings dialog writes the profile to
// phonemes.yaml at once, with the Inflection slider's distance from 50 put
// into the profile's inflection scale (slider 80 on a scale of 1 writes
// 1.6), and moves the dialog's slider back to 50.  The runtime and the INI
// only heard of that on OK.  A profile is tuned by ear (move the slider,
// OK, Speak, open the dialog again), so the runtime usually has the 80
// already when the save comes; after Cancel it still had it, and Speak
// played 80 on the new scale: the inflection twice (0.8 x 1.6 = 1.28 in
// place of 0.8), until the slider was moved by hand, restarts included.
//
// The maintainer's decision: a successful save is applied at once (the
// saved profile selected, its voice source in the voicing sliders,
// inflection 50) to the runtime and the INI, and Cancel afterwards drops
// only what changed after the save.  speechSettingsAfterProfileSave() in
// speech_settings.cpp is that decision; the dialog calls it.
//
// What is checked here: the sequences save then Cancel, save then changes
// then Cancel, save then OK; that the frames the frontend makes after save
// and Cancel are the ones heard before the save, at every rate; and what
// the save leaves alone (rate, pitch, volume, the frame and voice quality
// sliders stay pending until OK).

#include "doctest.h"

#include "editor_test_util.h"
#include "speech_settings.h"
#include "voicing_sliders.h"

#include <cmath>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace tgsb_test;
using tgsb_editor::SpeechSettings;

namespace {

const char* const kProfilePrefix = "profile:";  // TgsbRuntime::kVoiceProfilePrefix

std::vector<int> neutralSliders() {
  std::vector<int> s;
  tgsb_editor::resetVoicingSliders(s, tgsb_editor::kVoicingSliderCount);
  return s;
}

bool same(const SpeechSettings& a, const SpeechSettings& b) {
  return a.voiceName == b.voiceName && a.rate == b.rate && a.pitch == b.pitch &&
         a.volume == b.volume && a.inflection == b.inflection && a.pauseMode == b.pauseMode &&
         a.frameParams == b.frameParams && a.voicingParams == b.voicingParams &&
         a.frameExParams == b.frameExParams;
}

// The editor around the speech settings dialog.  Settings > Speech settings
// (appcontroller.cpp) opens the dialog on a copy of the runtime's settings
// and hands the copy to the runtime and the INI on OK, nothing on Cancel.
// Save to Profile (Dialogs.cpp) reloads the saved profile's voice source
// into the dialog's voicing sliders, calls speechSettingsAfterProfileSave()
// and hands what it returns to the runtime and the INI.
struct Editor {
  SpeechSettings runtime;  // what Speak and Save WAV play with
  SpeechSettings ini;      // tgsbPhonemeEditor.ini: what the next start plays with
  SpeechSettings dialog;   // the dialog's controls

  explicit Editor(const SpeechSettings& atStart) : runtime(atStart), ini(atStart) {}

  void open() { dialog = runtime; }

  void saveToProfile(const std::string& profile, const std::vector<int>& storedSliders) {
    dialog.voicingParams = storedSliders;
    const SpeechSettings applied =
        tgsb_editor::speechSettingsAfterProfileSave(dialog, runtime, std::string(kProfilePrefix) + profile);
    runtime = applied;
    ini = applied;
  }

  void close(bool ok) {
    if (ok) {
      runtime = dialog;
      ini = dialog;
    }
  }
};

// A profile selected, its voice source in the sliders, a listener's own
// rate, pitch, volume, frame and voice quality sliders.  The inflection is
// 80 unless said: a profile is tuned by ear, which is the Inflection slider
// moved, OK, Speak, and the dialog opened again for the save.
SpeechSettings profileSelected(const std::string& profile, int inflection = 80) {
  SpeechSettings s;
  s.voiceName = std::string(kProfilePrefix) + profile;
  s.rate = 60;
  s.pitch = 45;
  s.volume = 80;
  s.inflection = inflection;
  s.frameParams.assign(40, 50);
  s.voicingParams = neutralSliders();
  s.frameExParams = {0, 0, 0, 0, 50};
  return s;
}

// The inflection the frontend works with: the slider as the runtime passes
// it (0..100 to 0..1) times the active profile's inflection scale.
double heardInflection(const SpeechSettings& s, double profileScale) {
  return static_cast<double>(s.inflection) / 100.0 * profileScale;
}

// A pack root of this test's own: the shipped packs, with one more profile
// that has only an inflection scale.
fs::path packRootWithProfile(const std::string& dirName, const std::string& profile, double scale) {
  const fs::path root = editorScratchDir(dirName);
  std::string yaml = readAllBytes(repoRoot() / "packs" / "phonemes.yaml");
  const std::string key = "\nvoiceProfiles:";
  const size_t at = yaml.find(key);
  REQUIRE(at != std::string::npos);
  const size_t eol = yaml.find('\n', at + 1);
  REQUIRE(eol != std::string::npos);
  char num[32];
  snprintf(num, sizeof(num), "%.4f", scale);
  yaml.insert(eol + 1, "  " + profile + ":\n    inflectionScale: " + num + "\n");
  writeAllBytes(root / "packs" / "phonemes.yaml", yaml);
  fs::create_directories(root / "packs" / "lang");
  for (const auto& e : fs::directory_iterator(repoRoot() / "packs" / "lang")) {
    if (e.path().extension() == ".yaml") fs::copy_file(e.path(), root / "packs" / "lang" / e.path().filename());
  }
  return root;
}

// The frames the editor's Speak gets from the frontend for these settings:
// every frame's duration, fade and fields, flattened.
std::vector<double> spokenFrames(const fs::path& packRoot, const SpeechSettings& s, double speed) {
  REQUIRE(s.voiceName.rfind(kProfilePrefix, 0) == 0);
  const std::string profile = s.voiceName.substr(std::string(kProfilePrefix).size());
  std::vector<double> out;
  nvspFrontend_handle_t h = nvspFrontend_create(packRoot.string().c_str());
  REQUIRE(h != nullptr);
  REQUIRE(nvspFrontend_setLanguage(h, "en-us") == 1);
  REQUIRE(nvspFrontend_setVoiceProfile(h, profile.c_str()) == 1);
  auto cb = [](void* user, const nvspFrontend_Frame* frame, double durationMs, double fadeMs, int) {
    auto* v = static_cast<std::vector<double>*>(user);
    v->push_back(durationMs);
    v->push_back(fadeMs);
    if (!frame) { v->push_back(-12345.0); return; }
    const size_t n = sizeof(nvspFrontend_Frame) / sizeof(double);
    std::vector<double> fields(n);
    std::memcpy(fields.data(), frame, n * sizeof(double));
    v->insert(v->end(), fields.begin(), fields.end());
  };
  const double basePitch = 25.0 + 21.25 * (static_cast<double>(s.pitch) / 12.5);
  const int ok = nvspFrontend_queueIPA(h, "h\xC9\x99l\xCB\x88o\xCA\x8A w\xCB\x88\xC9\x9C\xCB\x90ld", speed, basePitch,
                                       static_cast<double>(s.inflection) / 100.0, ".", 0, cb, &out);
  nvspFrontend_destroy(h);
  REQUIRE(ok == 1);
  return out;
}

// How many of two frame lists' numbers differ (-1: not the same length).
int differingNumbers(const std::vector<double>& a, const std::vector<double>& b) {
  if (a.size() != b.size()) return -1;
  int n = 0;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::isnan(a[i]) && std::isnan(b[i])) continue;
    if (!(std::fabs(a[i] - b[i]) <= 1e-9 * (1.0 + std::fabs(a[i])))) ++n;
  }
  return n;
}

}  // namespace

TEST_CASE("profile save and Cancel: the suggested scale holds the slider's distance from 50") {
  CHECK(tgsb_editor::kInflectionNeutral == 50);
  CHECK(tgsb_editor::inflectionScaleKeepingSound(1.0, 50) == doctest::Approx(1.0));
  CHECK(tgsb_editor::inflectionScaleKeepingSound(1.0, 80) == doctest::Approx(1.6));
  CHECK(tgsb_editor::inflectionScaleKeepingSound(1.25, 40) == doctest::Approx(1.0));
  CHECK(tgsb_editor::inflectionScaleKeepingSound(1.3, 0) == doctest::Approx(0.0));
  // A scale is at most 3.
  CHECK(tgsb_editor::inflectionScaleKeepingSound(2.5, 80) == doctest::Approx(3.0));
}

TEST_CASE("profile save and Cancel: save, then Cancel, leaves the saved profile at inflection 50") {
  Editor ed(profileSelected("Probe"));
  REQUIRE(ed.runtime.inflection == 80);
  ed.open();
  ed.saveToProfile("Probe", neutralSliders());
  CHECK(ed.dialog.inflection == 50);
  ed.close(false);

  CHECK(ed.runtime.voiceName == "profile:Probe");
  CHECK(ed.runtime.inflection == 50);
  // The next start of the editor reads the same.
  CHECK(ed.ini.voiceName == "profile:Probe");
  CHECK(ed.ini.inflection == 50);
  CHECK(same(ed.runtime, ed.ini));
}

TEST_CASE("profile save and Cancel: after save and Cancel, Speak has the inflection heard before the save") {
  struct Row { double storedScale; int slider; };
  const Row rows[] = {{1.0, 80}, {1.0, 20}, {1.3, 65}, {0.8, 100}, {1.6, 0}, {1.0, 50}, {2.5, 80}};
  // The slider was at its place when the dialog opened (set, OK, Speak,
  // opened again), or is moved there in the dialog that saves.
  for (bool movedInThisDialog : {false, true}) {
    for (const Row& r : rows) {
      INFO("stored scale " << r.storedScale << ", Inflection slider " << r.slider
           << std::string(movedInThisDialog ? ", moved in this dialog" : ", applied before"));
      Editor ed(profileSelected("Probe", movedInThisDialog ? 50 : r.slider));
      ed.open();
      ed.dialog.inflection = r.slider;
      const double before = heardInflection(ed.dialog, r.storedScale);
      const double savedScale = tgsb_editor::inflectionScaleKeepingSound(r.storedScale, r.slider);
      ed.saveToProfile("Probe", neutralSliders());
      ed.close(false);

      const double after = heardInflection(ed.runtime, savedScale);
      if (r.storedScale * r.slider / 50.0 <= 3.0) {
        CHECK(after == doctest::Approx(before));
      } else {
        // The scale stopped at 3: less than before, never the slider on top of it.
        CHECK(after == doctest::Approx(0.5 * 3.0));
        CHECK(after < before);
      }
      // And the next start of the editor.
      CHECK(heardInflection(ed.ini, savedScale) == doctest::Approx(after));
    }
  }
}

TEST_CASE("profile save and Cancel: the frontend's frames after save and Cancel are those from before the save") {
  // A profile with a scale of 1.2 and the Inflection slider at 80: the save
  // writes 1.92.
  const double storedScale = 1.2;
  const double savedScale = tgsb_editor::inflectionScaleKeepingSound(storedScale, 80);
  REQUIRE(savedScale == doctest::Approx(1.92));
  const fs::path packsBefore = packRootWithProfile("profile_save_cancel_before", "Probe", storedScale);
  const fs::path packsAfter = packRootWithProfile("profile_save_cancel_after", "Probe", savedScale);

  Editor ed(profileSelected("Probe"));
  ed.open();
  const SpeechSettings heard = ed.runtime;  // what Speak played before the save
  REQUIRE(heard.inflection == 80);
  ed.saveToProfile("Probe", neutralSliders());
  ed.close(false);

  for (double speed : kEditorTestSpeeds) {
    INFO("speed " << speed);
    const std::vector<double> before = spokenFrames(packsBefore, heard, speed);
    const std::vector<double> after = spokenFrames(packsAfter, ed.runtime, speed);
    REQUIRE(before.size() > 100);
    CHECK(differingNumbers(before, after) == 0);
    // The comparison can tell: the same slider on the saved scale is not the same frames.
    CHECK(differingNumbers(before, spokenFrames(packsAfter, heard, speed)) != 0);
  }
}

TEST_CASE("profile save and Cancel: Cancel drops what changed after the save, and only that") {
  Editor ed(profileSelected("Probe"));
  ed.open();
  std::vector<int> stored = neutralSliders();
  stored[10] = 70;  // speed quotient, as saved
  ed.saveToProfile("Probe", stored);
  const SpeechSettings atSave = ed.runtime;

  // After the save: another voice, its sliders, another inflection.
  ed.dialog.voiceName = "Benjamin";
  ed.dialog.voicingParams = neutralSliders();
  ed.dialog.voicingParams[6] = 30;
  ed.dialog.inflection = 70;
  ed.dialog.rate = 90;
  ed.close(false);

  CHECK(same(ed.runtime, atSave));
  CHECK(same(ed.ini, atSave));
  CHECK(ed.runtime.voiceName == "profile:Probe");
  CHECK(ed.runtime.inflection == 50);
  CHECK(ed.runtime.voicingParams == stored);
}

TEST_CASE("profile save and Cancel: the save applies the saved profile's voice source with it") {
  // The sliders belong to the voice: the saved profile with the sliders of
  // the voice that was selected when the dialog opened would be another voice.
  SpeechSettings start = profileSelected("Probe", 65);
  start.voiceName = "Adam";
  start.voicingParams[3] = 20;
  Editor ed(start);
  ed.open();
  ed.dialog.voicingParams[10] = 70;
  std::vector<int> stored = neutralSliders();
  stored[3] = 20;
  stored[10] = 70;
  // From a built-in voice the save makes a new profile.
  ed.saveToProfile("Adam variant", stored);
  ed.close(false);

  CHECK(ed.runtime.voiceName == "profile:Adam variant");
  CHECK(ed.runtime.voicingParams == stored);
  CHECK(ed.runtime.inflection == 50);
  CHECK(same(ed.runtime, ed.ini));
}

TEST_CASE("profile save and Cancel: listener settings changed before the save still wait for OK") {
  // Rate, pitch, volume, the frame sliders and the voice quality sliders are
  // no part of a profile, so the save does not apply them.
  const SpeechSettings start = profileSelected("Probe");
  Editor ed(start);
  ed.open();
  ed.dialog.rate = 85;
  ed.dialog.pitch = 30;
  ed.dialog.volume = 100;
  ed.dialog.pauseMode = "long";
  ed.dialog.frameParams[2] = 75;
  ed.dialog.frameExParams[1] = 40;
  ed.saveToProfile("Probe", neutralSliders());

  CHECK(ed.runtime.rate == start.rate);
  CHECK(ed.runtime.pitch == start.pitch);
  CHECK(ed.runtime.volume == start.volume);
  CHECK(ed.runtime.pauseMode == start.pauseMode);
  CHECK(ed.runtime.frameParams == start.frameParams);
  CHECK(ed.runtime.frameExParams == start.frameExParams);
  // The dialog still shows them.
  CHECK(ed.dialog.rate == 85);
  CHECK(ed.dialog.frameParams[2] == 75);

  SUBCASE("Cancel drops them") {
    ed.close(false);
    SpeechSettings want = start;
    want.inflection = 50;
    CHECK(same(ed.runtime, want));
    CHECK(same(ed.ini, want));
  }
  SUBCASE("OK applies them") {
    ed.close(true);
    CHECK(ed.runtime.rate == 85);
    CHECK(ed.runtime.pitch == 30);
    CHECK(ed.runtime.volume == 100);
    CHECK(ed.runtime.pauseMode == "long");
    CHECK(ed.runtime.frameParams[2] == 75);
    CHECK(ed.runtime.frameExParams[1] == 40);
    CHECK(ed.runtime.inflection == 50);
    CHECK(same(ed.runtime, ed.ini));
  }
}

TEST_CASE("profile save and Cancel: a second save, then Cancel, leaves the second save") {
  Editor ed(profileSelected("Probe"));
  ed.open();
  ed.saveToProfile("Probe", neutralSliders());
  ed.dialog.inflection = 30;
  std::vector<int> stored = neutralSliders();
  stored[15] = 60;
  ed.dialog.voicingParams = stored;
  ed.saveToProfile("Probe two", stored);
  ed.dialog.inflection = 90;
  ed.close(false);

  CHECK(ed.runtime.voiceName == "profile:Probe two");
  CHECK(ed.runtime.inflection == 50);
  CHECK(ed.runtime.voicingParams == stored);
  CHECK(same(ed.runtime, ed.ini));
}

// What the fix must leave alone.

TEST_CASE("profile save and Cancel: save, then OK, applies the dialog as it is") {
  Editor ed(profileSelected("Probe"));
  ed.open();
  ed.dialog.rate = 75;
  ed.saveToProfile("Probe", neutralSliders());
  CHECK(ed.dialog.voiceName == "profile:Probe");
  CHECK(ed.dialog.inflection == 50);
  CHECK(ed.dialog.rate == 75);
  const SpeechSettings shown = ed.dialog;
  ed.close(true);

  CHECK(same(ed.runtime, shown));
  CHECK(same(ed.ini, shown));
  CHECK(ed.runtime.inflection == 50);
}

TEST_CASE("profile save and Cancel: save, then changes, then OK, applies the changes") {
  Editor ed(profileSelected("Probe"));
  ed.open();
  ed.saveToProfile("Probe", neutralSliders());
  ed.dialog.inflection = 70;  // livelier still, on purpose, on top of the saved scale
  ed.dialog.voiceName = "Caleb";
  const SpeechSettings shown = ed.dialog;
  ed.close(true);

  CHECK(same(ed.runtime, shown));
  CHECK(same(ed.ini, shown));
  CHECK(ed.runtime.inflection == 70);
  CHECK(ed.runtime.voiceName == "Caleb");
}

TEST_CASE("profile save and Cancel: without a save, Cancel changes nothing and OK applies the dialog") {
  const SpeechSettings start = profileSelected("Probe");
  SUBCASE("Cancel") {
    Editor ed(start);
    ed.open();
    ed.dialog.inflection = 30;
    ed.dialog.voiceName = "David";
    ed.dialog.rate = 10;
    ed.close(false);
    CHECK(same(ed.runtime, start));
    CHECK(same(ed.ini, start));
  }
  SUBCASE("OK") {
    Editor ed(start);
    ed.open();
    ed.dialog.inflection = 30;
    ed.dialog.voiceName = "David";
    const SpeechSettings shown = ed.dialog;
    ed.close(true);
    CHECK(same(ed.runtime, shown));
    CHECK(same(ed.ini, shown));
    CHECK(ed.runtime.inflection == 30);
  }
}

TEST_CASE("profile save and Cancel: the save changes nothing else in the dialog's settings") {
  Editor ed(profileSelected("Probe"));
  ed.open();
  ed.dialog.rate = 85;
  ed.dialog.frameParams[5] = 10;
  ed.dialog.frameExParams[4] = 60;
  std::vector<int> stored = neutralSliders();
  stored[12] = 40;
  SpeechSettings want = ed.dialog;
  want.voiceName = "profile:Copy";
  want.inflection = 50;
  want.voicingParams = stored;
  ed.saveToProfile("Copy", stored);
  CHECK(same(ed.dialog, want));
}

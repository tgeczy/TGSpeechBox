// The phoneme editor's Language settings dialog (#161).
//
// The dialog lists a language's `settings:` as key/value rows; a setting
// inside a block (rateCompensation.minimumDurations.vowelMs) is shown under
// one joined name.  Pressing OK used to rebuild the whole section from those
// rows: blocks the editor had no table entry for came back as flat keys the
// frontend does not read (Hungarian lost its rate floors and got 26% shorter
// at speed 3.0), lists were joined into one value, and Danish, which has both
// a flat boundarySmoothingEnabled and a boundarySmoothing block, had one
// overwrite the other.  All of that happened with nothing changed.
//
// What is checked here, on copies of the shipped packs: OK with nothing
// changed leaves every pack as it was, to the frontend and at every rate; an
// edit changes its own key and nothing else; a row always names one setting.

#include "doctest.h"

#include "editor_test_util.h"
#include "yaml_edit.h"

#include <set>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using namespace tgsb_test;
using tgsb_editor::LanguageYaml;
using Rows = std::vector<std::pair<std::string, std::string>>;

namespace {

LanguageYaml loadEditor(const fs::path& p) {
  LanguageYaml y;
  std::string err;
  const bool ok = y.load(p.string(), err);
  REQUIRE_MESSAGE(ok, "editor could not load " << p.string() << ": " << err);
  return y;
}

void saveEditor(const LanguageYaml& y) {
  std::string err;
  const bool ok = y.save(err);
  REQUIRE_MESSAGE(ok, "editor could not save: " << err);
}

std::string* rowValue(Rows& rows, const std::string& key) {
  for (auto& kv : rows) if (kv.first == key) return &kv.second;
  return nullptr;
}

// What the dialog does on OK: hand the rows back to the model, then Ctrl+S.
void pressOkAndSave(LanguageYaml& y, const Rows& rows) {
  y.setSettings(rows);
  saveEditor(y);
}

const char* const kPhrases[][2] = {
  {"hu", "ˈmeɡːondolom ˈhoɡ ˈɛz ˈjoː ˈlɛs ˈkeːʃøbb"},
  {"da", "ˈhɛj ˈmɛð ˈdɑj ˈʁøð ˈɡʁøð"},
  {"en-us", "ˈlʊk ˈwɛl ˈfʊl ˈbɔl ˈfiːl ˈtuː ˈjuː ˈbʊk"},
  {"fi", "ˈhyvæː ˈpæiʋæː ˈkukːɑ ˈtuli"},
  {"pl", "ˈdʑɛɲ ˈdɔbrɨ ˈpaɲstfu"},
  {"tr", "ˈmerhaba ˈnasɯlsɯn ˈtʃok ˈjaʃa"},
  {"es", "ˈbuskaɾ ˈesto ˈamoɾ ˈeɾa"},
  {"de", "ˈɡuːtən ˈtaːk ˈʃpʁɛçən ˈziː"},
};

}  // namespace

TEST_CASE("language settings: a save with no dialog at all keeps every pack's data") {
  // The ground the other cases stand on: load and save alone change nothing.
  const fs::path dir = editorScratchDir("settings_plain_save");
  for (const std::string& tag : languageTags()) {
    INFO("pack: " << tag);
    const fs::path copy = copyLangToOverride(dir, tag);
    const EditorNode before = parseYamlFile(copy);
    LanguageYaml y = loadEditor(copy);
    saveEditor(y);
    CHECK(sameTree(before, parseYamlFile(copy)));
  }
}

TEST_CASE("language settings: OK with nothing changed leaves every pack as it was") {
  const fs::path dir = editorScratchDir("settings_ok_unchanged");
  for (const std::string& tag : languageTags()) {
    INFO("pack: " << tag);
    const fs::path copy = copyLangToOverride(dir, tag);
    const EditorNode before = parseYamlFile(copy);
    LanguageYaml y = loadEditor(copy);
    pressOkAndSave(y, y.settings());
    CHECK(sameTree(before, parseYamlFile(copy)));
  }
}

TEST_CASE("language settings: OK with nothing changed leaves speech the same at every rate") {
  const fs::path dir = editorScratchDir("settings_ok_unchanged_frames");
  for (const auto& p : kPhrases) {
    const fs::path copy = copyLangToOverride(dir, p[0]);
    LanguageYaml y = loadEditor(copy);
    pressOkAndSave(y, y.settings());
  }
  for (const auto& p : kPhrases) {
    for (double speed : kEditorTestSpeeds) {
      INFO("pack: " << std::string(p[0]) << "  speed: " << speed);
      const std::vector<double> shipped = framesFor(p[0], p[1], speed);
      const std::vector<double> edited = framesFor(p[0], p[1], speed, dir);
      REQUIRE(!shipped.empty());
      CHECK(shipped == edited);
    }
  }
}

TEST_CASE("language settings: every row names one setting") {
  // Two rows with one key cannot be told apart in the dialog, and an edit
  // of one would land on the other.
  const fs::path dir = editorScratchDir("settings_unique_rows");
  for (const std::string& tag : languageTags()) {
    INFO("pack: " << tag);
    LanguageYaml y = loadEditor(copyLangToOverride(dir, tag));
    std::set<std::string> seen;
    for (const auto& kv : y.settings()) {
      INFO("row: " << kv.first);
      CHECK(seen.insert(kv.first).second);
    }
  }
}

TEST_CASE("language settings: Danish shows its flat switch and its block's switch as two rows") {
  // da.yaml has `boundarySmoothingEnabled: true` and, further down,
  // `boundarySmoothing: enabled: false`; the frontend reads the block last.
  const fs::path dir = editorScratchDir("settings_danish");
  const fs::path copy = copyLangToOverride(dir, "da");
  const nvsp_frontend::PackSet shipped = loadPackWithOverride("da");
  REQUIRE(shipped.lang.boundarySmoothingEnabled == false);

  LanguageYaml y = loadEditor(copy);
  Rows rows = y.settings();
  std::string* flat = rowValue(rows, "boundarySmoothingEnabled");
  std::string* inBlock = rowValue(rows, "boundarySmoothing.enabled");
  REQUIRE(flat != nullptr);
  REQUIRE(inBlock != nullptr);
  CHECK(*flat == "true");
  CHECK(*inBlock == "false");

  // Turning the block's switch on changes that node and no other.
  EditorNode expected = parseYamlFile(copy);
  EditorNode* node = nodeAt(expected, {"settings", "boundarySmoothing", "enabled"});
  REQUIRE(node != nullptr);
  node->scalar = "true";
  *inBlock = "true";
  pressOkAndSave(y, rows);
  CHECK(sameTree(expected, parseYamlFile(copy)));
  CHECK(loadPackWithOverride("da", dir).lang.boundarySmoothingEnabled == true);
}

TEST_CASE("language settings: changing a flat setting changes that key only") {
  const fs::path dir = editorScratchDir("settings_edit_flat");
  const fs::path copy = copyLangToOverride(dir, "hu");
  EditorNode expected = parseYamlFile(copy);
  EditorNode* node = nodeAt(expected, {"settings", "primaryStressDiv"});
  REQUIRE(node != nullptr);
  node->scalar = "1.25";

  LanguageYaml y = loadEditor(copy);
  Rows rows = y.settings();
  std::string* v = rowValue(rows, "primaryStressDiv");
  REQUIRE(v != nullptr);
  *v = "1.25";
  pressOkAndSave(y, rows);

  CHECK(sameTree(expected, parseYamlFile(copy)));
  const nvsp_frontend::PackSet shipped = loadPackWithOverride("hu");
  const nvsp_frontend::PackSet edited = loadPackWithOverride("hu", dir);
  CHECK(edited.lang.primaryStressDiv == doctest::Approx(1.25));
  // The rate floors the old rewrite lost.
  CHECK(edited.lang.rateCompEnabled == shipped.lang.rateCompEnabled);
  CHECK(edited.lang.rateCompVowelFloorMs == shipped.lang.rateCompVowelFloorMs);
  CHECK(edited.lang.rateCompAffricateFloorMs == shipped.lang.rateCompAffricateFloorMs);
  CHECK(edited.lang.rateCompTrillFloorMs == shipped.lang.rateCompTrillFloorMs);
}

TEST_CASE("language settings: changing a setting inside a block writes it back into the block") {
  // The row is named rateCompensationMinimumDurationsVowelMs; the frontend
  // reads rateCompensation.minimumDurations.vowelMs, not a flat key of that name.
  const fs::path dir = editorScratchDir("settings_edit_nested");
  const fs::path copy = copyLangToOverride(dir, "hu");
  EditorNode expected = parseYamlFile(copy);
  EditorNode* node = nodeAt(expected, {"settings", "rateCompensation", "minimumDurations", "vowelMs"});
  REQUIRE(node != nullptr);
  REQUIRE(node->scalar == "28");
  node->scalar = "33";

  LanguageYaml y = loadEditor(copy);
  Rows rows = y.settings();
  std::string* v = rowValue(rows, "rateCompensationMinimumDurationsVowelMs");
  REQUIRE(v != nullptr);
  CHECK(*v == "28");
  *v = "33";
  pressOkAndSave(y, rows);

  CHECK(sameTree(expected, parseYamlFile(copy)));
  const nvsp_frontend::PackSet shipped = loadPackWithOverride("hu");
  const nvsp_frontend::PackSet edited = loadPackWithOverride("hu", dir);
  CHECK(shipped.lang.rateCompVowelFloorMs == doctest::Approx(28.0));
  CHECK(edited.lang.rateCompVowelFloorMs == doctest::Approx(33.0));
  CHECK(edited.lang.rateCompFricativeFloorMs == shipped.lang.rateCompFricativeFloorMs);
}

TEST_CASE("language settings: an edit and its undo bring every pack back to where it was") {
  // Any change makes the save write the settings section out again from the
  // model, so this also checks that nothing is lost in that writing.
  const fs::path dir = editorScratchDir("settings_edit_undo");
  for (const std::string& tag : languageTags()) {
    INFO("pack: " << tag);
    const fs::path copy = copyLangToOverride(dir, tag);
    const EditorNode before = parseYamlFile(copy);
    LanguageYaml y = loadEditor(copy);
    const Rows original = y.settings();
    if (original.empty()) continue;

    Rows rows = original;
    rows.front().second = "0.123";
    pressOkAndSave(y, rows);
    LanguageYaml again = loadEditor(copy);
    CHECK(again.settings() == rows);

    pressOkAndSave(again, original);
    CHECK(sameTree(before, parseYamlFile(copy)));
  }
}

TEST_CASE("language settings: a list stays a list when it is edited") {
  const fs::path dir = editorScratchDir("settings_edit_list");
  const fs::path copy = dir / "packs" / "lang" / "hu.yaml";
  writeAllBytes(copy,
      "settings:\n"
      "  primaryStressDiv: 1.05\n"
      "  trajectoryLimit:\n"
      "    enabled: true\n"
      "    applyTo: [cf2, cf3, pf2, pf3]\n"
      "  prominence:\n"
      "    monosyllableExclude:\n"
      "      - \"ðə\"\n"
      "      - \"ə\"\n");
  LanguageYaml y = loadEditor(copy);
  Rows rows = y.settings();
  std::string* applyTo = rowValue(rows, "trajectoryLimitApplyTo");
  REQUIRE(applyTo != nullptr);
  CHECK(*applyTo == "cf2,cf3,pf2,pf3");
  *applyTo = "cf2, cf3";
  std::string* excl = rowValue(rows, "prominenceMonosyllableExclude");
  REQUIRE(excl != nullptr);
  CHECK(*excl == "ðə,ə");
  pressOkAndSave(y, rows);

  EditorNode after = parseYamlFile(copy);
  EditorNode* list = nodeAt(after, {"settings", "trajectoryLimit", "applyTo"});
  REQUIRE(list != nullptr);
  REQUIRE(list->isSeq());
  REQUIRE(list->seq.size() == 2);
  CHECK(list->seq[0].scalar == "cf2");
  CHECK(list->seq[1].scalar == "cf3");
  // The list that was not touched is still a list of two.
  EditorNode* untouched = nodeAt(after, {"settings", "prominence", "monosyllableExclude"});
  REQUIRE(untouched != nullptr);
  REQUIRE(untouched->isSeq());
  CHECK(untouched->seq.size() == 2);
  CHECK(loadPackWithOverride("hu", dir).lang.prominenceMonosyllableExclude.size() == 2);
}

TEST_CASE("language settings: removing a row removes that setting only") {
  const fs::path dir = editorScratchDir("settings_remove");
  const fs::path copy = copyLangToOverride(dir, "hu");
  EditorNode expected = parseYamlFile(copy);
  EditorNode* block = nodeAt(expected, {"settings", "rateCompensation", "minimumDurations"});
  REQUIRE(block != nullptr);
  REQUIRE(block->map.erase("trillMs") == 1);

  LanguageYaml y = loadEditor(copy);
  Rows rows = y.settings();
  const size_t n = rows.size();
  rows.erase(std::remove_if(rows.begin(), rows.end(), [](const auto& kv) {
    return kv.first == "rateCompensationMinimumDurationsTrillMs";
  }), rows.end());
  REQUIRE(rows.size() == n - 1);
  pressOkAndSave(y, rows);
  CHECK(sameTree(expected, parseYamlFile(copy)));
}

TEST_CASE("language settings: a new setting is written where the frontend reads it") {
  const fs::path dir = editorScratchDir("settings_add");
  const fs::path copy = dir / "packs" / "lang" / "hu.yaml";
  writeAllBytes(copy,
      "settings:\n"
      "  primaryStressDiv: 1.05\n"
      "  allophoneRules:\n"
      "    enabled: true\n"
      "    rules:\n"
      "      - name: keep_me\n"
      "        phonemes: [t]\n"
      "        action: scale\n"
      "        durationScale: 0.9\n");
  LanguageYaml y = loadEditor(copy);
  Rows rows = y.settings();
  // Names from the dialog's key list: the frontend reads each as a flat key.
  rows.emplace_back("rateCompEnabled", "true");
  rows.emplace_back("rateCompVowelFloorMs", "31");
  rows.emplace_back("boundarySmoothingAlveolarF1Scale", "0.5");
  // A dotted name goes into the block it names.
  rows.emplace_back("rateCompensation.minimumDurations.trillMs", "17");
  pressOkAndSave(y, rows);

  const nvsp_frontend::PackSet edited = loadPackWithOverride("hu", dir);
  CHECK(edited.lang.rateCompEnabled == true);
  CHECK(edited.lang.rateCompVowelFloorMs == doctest::Approx(31.0));
  CHECK(edited.lang.boundarySmoothingAlveolarF1Scale == doctest::Approx(0.5));
  CHECK(edited.lang.rateCompTrillFloorMs == doctest::Approx(17.0));
  CHECK(edited.lang.primaryStressDiv == doctest::Approx(1.05));
  // The rule list has no row of its own and is not touched.
  EditorNode after = parseYamlFile(copy);
  EditorNode* rules = nodeAt(after, {"settings", "allophoneRules", "rules"});
  REQUIRE(rules != nullptr);
  REQUIRE(rules->isSeq());
  CHECK(rules->seq.size() == 1);
}

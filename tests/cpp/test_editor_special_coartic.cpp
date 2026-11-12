// The phoneme editor's special coarticulation rules (#213).
//
// A rule moves a vowel's F2 or F3 next to certain consonants.  The frontend
// needs three of its keys spelled out: a rule with no `formant` is dropped
// when the pack loads, one with no `side` is never applied, one with no
// `vowelFilter` matches no vowel.  The editor left each of them out when it
// held the value its dialog starts from (f2, both, all), and any change in
// the dialog rewrites the whole list, so one edit silently switched off most
// of the rules in en-us, en-gb, en-au and tr, and a rule added with the
// dialog's own defaults never took effect.
//
// What is checked here, on copies of the shipped packs read back with the
// frontend's loader: writing the rules back keeps every rule as the frontend
// reads it, and the frames at every rate; a new rule with the dialog's
// defaults is kept and applied; a key missing from a hand-written rule is
// not invented.

#include "doctest.h"

#include "editor_test_util.h"
#include "yaml_edit.h"

#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace tgsb_test;
using tgsb_editor::LanguageYaml;
using tgsb_editor::SpecialCoarticRuleEntry;

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

void checkSameRules(const std::vector<nvsp_frontend::SpecialCoarticRule>& a,
                    const std::vector<nvsp_frontend::SpecialCoarticRule>& b) {
  REQUIRE(a.size() == b.size());
  for (size_t i = 0; i < a.size(); ++i) {
    INFO("rule " << i << ": " << a[i].name);
    CHECK(a[i].name == b[i].name);
    CHECK(a[i].triggers == b[i].triggers);
    CHECK(a[i].vowelFilter == b[i].vowelFilter);
    CHECK(a[i].formant == b[i].formant);
    CHECK(a[i].deltaHz == b[i].deltaHz);
    CHECK(a[i].side == b[i].side);
    CHECK(a[i].cumulative == b[i].cumulative);
    CHECK(a[i].unstressedScale == b[i].unstressedScale);
    CHECK(a[i].phraseFinalStressedScale == b[i].phraseFinalStressedScale);
  }
}

// The packs that ship special coarticulation rules.
const char* const kPhrases[][2] = {
  {"en-us", "ˈlʊk ˈwɛl ˈfʊl ˈbɔl ˈfiːl ˈtuː ˈjuː ˈbʊk"},
  {"en-gb", "ˈlʊk ˈwɛl ˈfʊl ˈbɔːl ˈfiːl ˈtuː ˈjuː ˈbʊk"},
  {"en-au", "ˈlʊk ˈwɛl ˈfʊl ˈbɔːl ˈfiːl ˈtuː ˈjuː ˈbʊk"},
  {"tr", "ˈkɯz ˈɡyl ˈkɑl ˈbil ˈjol ˈɡel ˈkuʃ"},
};

}  // namespace

TEST_CASE("special coarticulation: the test's packs are the ones that ship rules") {
  // If a pack gains rules, it belongs in kPhrases above.
  std::vector<std::string> withRules;
  for (const std::string& tag : languageTags()) {
    LanguageYaml y = loadEditor(shippedLangFile(tag));
    if (!y.specialCoarticRules().empty()) withRules.push_back(tag);
  }
  std::vector<std::string> expected;
  for (const auto& p : kPhrases) expected.push_back(p[0]);
  std::sort(expected.begin(), expected.end());
  CHECK(withRules == expected);
}

TEST_CASE("special coarticulation: writing the rules back keeps every rule the frontend reads") {
  const fs::path dir = editorScratchDir("coartic_roundtrip");
  for (const auto& p : kPhrases) {
    const std::string tag = p[0];
    INFO("pack: " << tag);
    const fs::path copy = copyLangToOverride(dir, tag);
    LanguageYaml y = loadEditor(copy);
    // What the dialog's OK does after any change (a rule moved up and back down).
    y.setSpecialCoarticRules(y.specialCoarticRules());
    saveEditor(y);

    const nvsp_frontend::PackSet shipped = loadPackWithOverride(tag);
    const nvsp_frontend::PackSet edited = loadPackWithOverride(tag, dir);
    REQUIRE(!shipped.lang.specialCoarticRules.empty());
    checkSameRules(shipped.lang.specialCoarticRules, edited.lang.specialCoarticRules);
    CHECK(edited.lang.specialCoarticulationEnabled == shipped.lang.specialCoarticulationEnabled);
    CHECK(edited.lang.specialCoarticMaxDeltaHz == shipped.lang.specialCoarticMaxDeltaHz);
  }
}

TEST_CASE("special coarticulation: writing the rules back leaves speech the same at every rate") {
  const fs::path dir = editorScratchDir("coartic_roundtrip_frames");
  for (const auto& p : kPhrases) {
    LanguageYaml y = loadEditor(copyLangToOverride(dir, p[0]));
    y.setSpecialCoarticRules(y.specialCoarticRules());
    saveEditor(y);
  }
  for (const auto& p : kPhrases) {
    for (double speed : kEditorTestSpeeds) {
      INFO("pack: " << std::string(p[0]) << "  speed: " << speed);
      CHECK(framesFor(p[0], p[1], speed) == framesFor(p[0], p[1], speed, dir));
    }
  }
}

TEST_CASE("special coarticulation: a new rule with the dialog's defaults is kept and applied") {
  const fs::path dir = editorScratchDir("coartic_new_rule");
  const fs::path copy = copyLangToOverride(dir, "en-us");
  LanguageYaml y = loadEditor(copy);
  auto rules = y.specialCoarticRules();
  const size_t n = rules.size();
  SpecialCoarticRuleEntry fresh;  // the dialog starts a new rule from this: f2, both, all
  fresh.name = "test_m_f2";
  fresh.triggers = {"m"};
  fresh.deltaHz = -300.0;
  rules.push_back(fresh);
  y.setSpecialCoarticRules(rules);
  saveEditor(y);

  const nvsp_frontend::PackSet edited = loadPackWithOverride("en-us", dir);
  REQUIRE(edited.lang.specialCoarticRules.size() == n + 1);
  const auto& got = edited.lang.specialCoarticRules.back();
  CHECK(got.name == "test_m_f2");
  CHECK(got.formant == "f2");
  CHECK(got.side == "both");
  CHECK(got.vowelFilter == "all");
  CHECK(got.deltaHz == doctest::Approx(-300.0));

  // Applied: a vowel between two m's moves, at every rate.
  for (double speed : kEditorTestSpeeds) {
    INFO("speed: " << speed);
    CHECK(framesFor("en-us", "ˈmɑmɑ ˈmimi", speed) != framesFor("en-us", "ˈmɑmɑ ˈmimi", speed, dir));
  }
}

TEST_CASE("special coarticulation: a key missing from a hand-written rule is not invented") {
  // Such a rule does nothing in the frontend.  The editor writing `side: both`
  // into it would switch it on and change the language's sound.
  const fs::path dir = editorScratchDir("coartic_missing_key");
  const fs::path copy = dir / "packs" / "lang" / "hu.yaml";
  writeAllBytes(copy,
      "settings:\n"
      "  specialCoarticulation:\n"
      "    enabled: true\n"
      "    rules:\n"
      "      - name: no_side\n"
      "        triggers: [m]\n"
      "        vowelFilter: front\n"
      "        formant: f3\n"
      "        deltaHz: -200\n"
      "      - name: whole\n"
      "        triggers: [n]\n"
      "        vowelFilter: all\n"
      "        formant: f2\n"
      "        deltaHz: -100\n"
      "        side: both\n");
  LanguageYaml y = loadEditor(copy);
  const auto rules = y.specialCoarticRules();
  REQUIRE(rules.size() == 2);
  CHECK(rules[0].side.empty());
  CHECK(rules[1].side == "both");
  y.setSpecialCoarticRules(rules);
  saveEditor(y);

  EditorNode after = parseYamlFile(copy);
  EditorNode* list = nodeAt(after, {"settings", "specialCoarticulation", "rules"});
  REQUIRE(list != nullptr);
  REQUIRE(list->isSeq());
  REQUIRE(list->seq.size() == 2);
  CHECK(list->seq[0].get("side") == nullptr);
  CHECK(list->seq[0].get("formant") != nullptr);
  CHECK(list->seq[1].get("side") != nullptr);
  CHECK(list->seq[1].get("formant") != nullptr);
  CHECK(list->seq[1].get("vowelFilter") != nullptr);

  const nvsp_frontend::PackSet edited = loadPackWithOverride("hu", dir);
  REQUIRE(edited.lang.specialCoarticRules.size() == 2);
  CHECK(edited.lang.specialCoarticRules[0].side.empty());
  CHECK(edited.lang.specialCoarticRules[1].side == "both");
}

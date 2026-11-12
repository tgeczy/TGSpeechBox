// The phoneme editor's normalization mappings (#214).
//
// Adding, editing or removing any mapping rewrites the language's whole
// `replacements:` list from the editor's own copy of it.  That copy had no
// room for the four cross-word conditions (nextWordStartsClass,
// nextWordStartsNotClass, prevWordEndsClass, prevWordEndsNotClass), so one
// mapping change dropped them from every rule.  Spanish has one: the
// word-final ɾ gets its extra trill only when the next word does not start
// with a vowel (#42), and after any edit "buscar esto" trilled again.  A `to`
// written as a list of candidates kept only its first item.
//
// What is checked here, on copies of the shipped packs read back with the
// frontend's loader: writing the mappings back keeps every rule in every
// pack, and Spanish frames at every rate; so does adding an unrelated
// mapping; every item of a `to` list survives.

#include "doctest.h"

#include "editor_test_util.h"
#include "yaml_edit.h"

#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace tgsb_test;
using tgsb_editor::LanguageYaml;
using tgsb_editor::ReplacementRule;

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

void checkSameRules(const std::vector<nvsp_frontend::ReplacementRule>& a,
                    const std::vector<nvsp_frontend::ReplacementRule>& b) {
  REQUIRE(a.size() == b.size());
  for (size_t i = 0; i < a.size(); ++i) {
    INFO("rule " << i << ": from " << nvsp_frontend::u32ToUtf8(a[i].from));
    CHECK(a[i].from == b[i].from);
    CHECK(a[i].to == b[i].to);
    CHECK(a[i].when.atWordStart == b[i].when.atWordStart);
    CHECK(a[i].when.atWordEnd == b[i].when.atWordEnd);
    CHECK(a[i].when.beforeClass == b[i].when.beforeClass);
    CHECK(a[i].when.afterClass == b[i].when.afterClass);
    CHECK(a[i].when.notBeforeClass == b[i].when.notBeforeClass);
    CHECK(a[i].when.notAfterClass == b[i].when.notAfterClass);
    CHECK(a[i].when.nextWordStartsClass == b[i].when.nextWordStartsClass);
    CHECK(a[i].when.nextWordStartsNotClass == b[i].when.nextWordStartsNotClass);
    CHECK(a[i].when.prevWordEndsClass == b[i].when.prevWordEndsClass);
    CHECK(a[i].when.prevWordEndsNotClass == b[i].when.prevWordEndsNotClass);
  }
}

const char* const kSpanish = "ˈbuskaɾ ˈesto ˈamoɾ ˈeɾa";

}  // namespace

TEST_CASE("mappings: writing them back keeps every pack's rules as the frontend reads them") {
  const fs::path dir = editorScratchDir("mappings_roundtrip");
  for (const std::string& tag : languageTags()) {
    INFO("pack: " << tag);
    const fs::path copy = copyLangToOverride(dir, tag);
    LanguageYaml y = loadEditor(copy);
    y.setReplacements(y.replacements());
    saveEditor(y);
  }
  for (const std::string& tag : languageTags()) {
    INFO("pack: " << tag);
    const nvsp_frontend::PackSet shipped = loadPackWithOverride(tag);
    const nvsp_frontend::PackSet edited = loadPackWithOverride(tag, dir);
    checkSameRules(shipped.lang.replacements, edited.lang.replacements);
    checkSameRules(shipped.lang.preReplacements, edited.lang.preReplacements);
    CHECK(shipped.lang.classes == edited.lang.classes);
  }
}

TEST_CASE("mappings: writing them back keeps the rest of every pack's normalization block") {
  const fs::path dir = editorScratchDir("mappings_roundtrip_tree");
  for (const std::string& tag : languageTags()) {
    INFO("pack: " << tag);
    const fs::path copy = copyLangToOverride(dir, tag);
    EditorNode before = parseYamlFile(copy);
    LanguageYaml y = loadEditor(copy);
    y.setReplacements(y.replacements());
    saveEditor(y);
    EditorNode after = parseYamlFile(copy);
    // Everything but the rule list is the same tree; the rule list is
    // compared rule by rule through the frontend in the case above.
    if (EditorNode* n = nodeAt(before, {"normalization"}); n && n->isMap()) n->map.erase("replacements");
    if (EditorNode* n = nodeAt(after, {"normalization"}); n && n->isMap()) n->map.erase("replacements");
    CHECK(sameTree(before, after));
  }
}

TEST_CASE("mappings: Spanish keeps its word-final r rule when a mapping is added, at every rate") {
  const fs::path dir = editorScratchDir("mappings_es_add");
  const fs::path copy = copyLangToOverride(dir, "es");
  LanguageYaml y = loadEditor(copy);
  std::vector<ReplacementRule> rules = y.replacements();
  // An unrelated mapping, as the Add mapping dialog makes one.
  ReplacementRule added;
  added.from = "q̃";
  added.to = "k";
  rules.push_back(added);
  y.setReplacements(rules);
  saveEditor(y);

  const nvsp_frontend::PackSet shipped = loadPackWithOverride("es");
  const nvsp_frontend::PackSet edited = loadPackWithOverride("es", dir);
  REQUIRE(edited.lang.replacements.size() == shipped.lang.replacements.size() + 1);
  std::vector<nvsp_frontend::ReplacementRule> allButNew(edited.lang.replacements.begin(),
                                                        edited.lang.replacements.end() - 1);
  checkSameRules(shipped.lang.replacements, allButNew);

  for (double speed : kEditorTestSpeeds) {
    INFO("speed: " << speed);
    CHECK(framesFor("es", kSpanish, speed) == framesFor("es", kSpanish, speed, dir));
  }
}

TEST_CASE("mappings: the editor holds the Spanish rule's cross-word condition, through an edit of the rule") {
  const fs::path dir = editorScratchDir("mappings_es_edit");
  const fs::path copy = copyLangToOverride(dir, "es");
  LanguageYaml y = loadEditor(copy);
  std::vector<ReplacementRule> rules = y.replacements();
  ReplacementRule* finalR = nullptr;
  for (auto& r : rules) {
    if (r.from == "ɾ" && r.when.atWordEnd && !r.when.nextWordStartsNotClass.empty()) finalR = &r;
  }
  REQUIRE(finalR != nullptr);
  CHECK(finalR->when.nextWordStartsNotClass == "VOWELS");
  CHECK(!finalR->when.isEmpty());

  // The Edit mapping dialog sets from, to, the two word flags and the four
  // class boxes; what it has no control for stays as it was.
  ReplacementRule edited = *finalR;
  edited.to = "ɾ_wf";
  edited.when.atWordEnd = true;
  edited.when.beforeClass.clear();
  *finalR = edited;
  y.setReplacements(rules);
  saveEditor(y);

  const nvsp_frontend::PackSet pack = loadPackWithOverride("es", dir);
  bool found = false;
  for (const auto& r : pack.lang.replacements) {
    if (r.from != U"ɾ" || !r.when.atWordEnd) continue;
    found = true;
    CHECK(r.when.nextWordStartsNotClass == "VOWELS");
    REQUIRE(r.to.size() == 1);
    CHECK(r.to[0] == U"ɾ_wf");
  }
  CHECK(found);
}

TEST_CASE("mappings: a rule with only a cross-word condition is a rule, not a no-op") {
  // from == to with no condition is skipped on save; a cross-word condition
  // is a condition.
  tgsb_editor::ReplacementWhen w;
  CHECK(w.isEmpty());
  w.prevWordEndsNotClass = "VOWELS";
  CHECK(!w.isEmpty());
  for (std::string tgsb_editor::ReplacementWhen::* field :
       {&tgsb_editor::ReplacementWhen::nextWordStartsClass,
        &tgsb_editor::ReplacementWhen::nextWordStartsNotClass,
        &tgsb_editor::ReplacementWhen::prevWordEndsClass,
        &tgsb_editor::ReplacementWhen::prevWordEndsNotClass}) {
    tgsb_editor::ReplacementWhen one;
    one.*field = "VOWELS";
    CHECK(!one.isEmpty());
  }
}

TEST_CASE("mappings: a file with no mappings gets no empty list from an untouched save") {
  const fs::path dir = editorScratchDir("mappings_none");
  const fs::path copy = dir / "packs" / "lang" / "hu.yaml";
  const std::string text = "settings:\n  primaryStressDiv: 1.05\n";
  writeAllBytes(copy, text);
  LanguageYaml y = loadEditor(copy);
  y.setReplacements(y.replacements());
  saveEditor(y);
  CHECK(readAllBytes(copy) == text);
}

TEST_CASE("mappings: every item of a `to` list survives") {
  const fs::path dir = editorScratchDir("mappings_to_list");
  const fs::path copy = dir / "packs" / "lang" / "hu.yaml";
  writeAllBytes(copy,
      "normalization:\n"
      "  classes:\n"
      "    VOWELS: [a, e]\n"
      "  replacements:\n"
      "    - from: x\n"
      "      to: [k_special, k, c]\n"
      "    - from: y\n"
      "      to: j\n"
      "      when:\n"
      "        atWordEnd: true\n"
      "        prevWordEndsClass: VOWELS\n");
  const EditorNode before = parseYamlFile(copy);
  LanguageYaml y = loadEditor(copy);
  std::vector<ReplacementRule> rules = y.replacements();
  REQUIRE(rules.size() == 2);
  CHECK(rules[0].to == "k_special");
  // Change the other rule, as the Edit mapping dialog would.
  rules[1].to = "i";
  y.setReplacements(rules);
  saveEditor(y);

  EditorNode expected = before;
  EditorNode* to = nodeAt(expected, {"normalization", "replacements"});
  REQUIRE(to != nullptr);
  REQUIRE(to->seq.size() == 2);
  to->seq[1].map["to"].scalar = "i";
  CHECK(sameTree(expected, parseYamlFile(copy)));

  const nvsp_frontend::PackSet edited = loadPackWithOverride("hu", dir);
  bool found = false;
  for (const auto& r : edited.lang.replacements) {
    if (r.from != U"x") continue;
    found = true;
    CHECK(r.to.size() == 3);
  }
  CHECK(found);
}

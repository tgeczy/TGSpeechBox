// The phoneme editor's Class Editor (#162).
//
// Normalization classes (VOWELS, NASAL, STOPS...) are lists of IPA symbols
// that a language's rules test with beforeClass, afterClass and the like.
// The editor read only classes written as one string, which no pack does, so
// the Class Editor opened empty for every language, and its OK wrote that
// emptiness back: every class gone from the file at the next save, and every
// rule that named one no longer matching.  A class added there was written
// as a string, which the frontend does not read.
//
// What is checked here, on copies of the shipped packs: the editor sees the
// classes with their members, one per line; OK with nothing changed leaves
// every pack as it was, to the frontend too; an edit changes its own class
// and is written as the list the frontend reads.

#include "doctest.h"

#include "editor_test_util.h"
#include "yaml_edit.h"

#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace tgsb_test;
using tgsb_editor::LanguageYaml;
using ClassTexts = std::map<std::string, std::string>;

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

// The members edit box is a Win32 multi-line edit: it hands lines back with \r\n.
std::string asEditBoxReturns(const std::string& text) {
  std::string out;
  for (char c : text) {
    if (c == '\n') out += "\r\n";
    else out.push_back(c);
  }
  return out;
}

std::vector<std::string> u8(const std::vector<std::u32string>& items) {
  std::vector<std::string> out;
  for (const auto& s : items) out.push_back(nvsp_frontend::u32ToUtf8(s));
  return out;
}

}  // namespace

TEST_CASE("class editor: Spanish shows its classes, one member per line") {
  const fs::path dir = editorScratchDir("classes_es_shown");
  LanguageYaml y = loadEditor(copyLangToOverride(dir, "es"));
  const ClassTexts classes = y.classes();
  REQUIRE(classes.count("VOWELS") == 1);
  REQUIRE(classes.count("NASAL") == 1);
  REQUIRE(classes.count("STOPS") == 1);
  CHECK(classes.at("VOWELS") == "a\ne\ni\no\nu");
  CHECK(classes.size() == y.classNamesSorted().size());
}

TEST_CASE("class editor: every pack's classes are shown as the frontend reads them") {
  const fs::path dir = editorScratchDir("classes_all_shown");
  for (const std::string& tag : languageTags()) {
    INFO("pack: " << tag);
    const fs::path copy = copyLangToOverride(dir, tag);
    EditorNode tree = parseYamlFile(copy);
    const EditorNode* inFile = nodeAt(tree, {"normalization", "classes"});
    LanguageYaml y = loadEditor(copy);
    const ClassTexts classes = y.classes();
    if (!inFile || !inFile->isMap()) { CHECK(classes.empty()); continue; }
    CHECK(classes.size() == inFile->map.size());
    for (const auto& kv : inFile->map) {
      INFO("class: " << kv.first);
      REQUIRE(kv.second.isSeq());
      std::string expected;
      for (const auto& item : kv.second.seq) {
        if (!expected.empty()) expected += "\n";
        expected += item.scalar;
      }
      REQUIRE(classes.count(kv.first) == 1);
      CHECK(classes.at(kv.first) == expected);
    }
  }
}

TEST_CASE("class editor: OK with nothing changed leaves every pack as it was") {
  const fs::path dir = editorScratchDir("classes_ok_unchanged");
  for (const std::string& tag : languageTags()) {
    INFO("pack: " << tag);
    const fs::path copy = copyLangToOverride(dir, tag);
    const EditorNode before = parseYamlFile(copy);
    LanguageYaml y = loadEditor(copy);
    ClassTexts classes = y.classes();
    // Every class as the edit box would hand it back.
    for (auto& kv : classes) kv.second = asEditBoxReturns(kv.second);
    y.setClasses(classes);
    saveEditor(y);
    CHECK(sameTree(before, parseYamlFile(copy)));
  }
}

TEST_CASE("class editor: OK with nothing changed leaves the frontend's classes and speech as they were") {
  const fs::path dir = editorScratchDir("classes_ok_unchanged_frontend");
  const char* const phrases[][2] = {
    {"es", "ˈbuskaɾ ˈesto ˈamoɾ ˈeɾa kombenˈseɾ anˈdaɾ"},
    {"pt-br", "ˈoj ˈtudu ˈbẽj̃ ˈkazɐ ˈmujtu obɾiˈɡadu"},
    {"hu", "ˈmeɡːondolom ˈhoɡ ˈɛz ˈjoː ˈlɛs ˈkeːʃøbb"},
    {"en-gb", "ˈbɑːθ ˈlɑːst ˈkɑːnt ˈhɒt ˈwɔːtə"},
    {"en-us", "ˈlʊk ˈwɛl ˈfʊl ˈbɔl ˈfiːl ˈtuː ˈjuː ˈbʊk"},
    {"tr", "ˈmerhaba ˈnasɯlsɯn ˈtʃok ˈjaʃa"},
  };
  // pt-br, en-gb and en-us take classes from pt.yaml and en.yaml as well.
  for (const char* tag : {"es", "pt", "pt-br", "hu", "en", "en-gb", "en-us", "tr"}) {
    const fs::path copy = copyLangToOverride(dir, tag);
    LanguageYaml y = loadEditor(copy);
    y.setClasses(y.classes());
    saveEditor(y);
  }
  for (const auto& p : phrases) {
    const std::string tag = p[0];
    INFO("pack: " << tag);
    const nvsp_frontend::PackSet shipped = loadPackWithOverride(tag);
    const nvsp_frontend::PackSet edited = loadPackWithOverride(tag, dir);
    REQUIRE(!shipped.lang.classes.empty());
    CHECK(shipped.lang.classes == edited.lang.classes);
    for (double speed : kEditorTestSpeeds) {
      INFO("speed: " << speed);
      CHECK(framesFor(tag, p[1], speed) == framesFor(tag, p[1], speed, dir));
    }
  }
}

TEST_CASE("class editor: a changed class is written as a list, and the others are left alone") {
  const fs::path dir = editorScratchDir("classes_edit");
  const fs::path copy = copyLangToOverride(dir, "es");
  EditorNode expected = parseYamlFile(copy);
  EditorNode* vowels = nodeAt(expected, {"normalization", "classes", "VOWELS"});
  REQUIRE(vowels != nullptr);
  REQUIRE(vowels->isSeq());
  EditorNode extra;
  extra.type = EditorNode::Type::Scalar;
  extra.scalar = "ə";
  vowels->seq.push_back(extra);

  LanguageYaml y = loadEditor(copy);
  ClassTexts classes = y.classes();
  classes["VOWELS"] = asEditBoxReturns(classes["VOWELS"]) + "\r\n  ə  \r\n\r\n";
  y.setClasses(classes);
  saveEditor(y);

  CHECK(sameTree(expected, parseYamlFile(copy)));
  const nvsp_frontend::PackSet shipped = loadPackWithOverride("es");
  const nvsp_frontend::PackSet edited = loadPackWithOverride("es", dir);
  CHECK(u8(edited.lang.classes.at("VOWELS")) == std::vector<std::string>{"a", "e", "i", "o", "u", "ə"});
  CHECK(edited.lang.classes.at("NASAL") == shipped.lang.classes.at("NASAL"));
  CHECK(edited.lang.classes.at("STOPS") == shipped.lang.classes.at("STOPS"));
}

TEST_CASE("class editor: a new class reaches the frontend, a removed one is gone") {
  const fs::path dir = editorScratchDir("classes_add_remove");
  const fs::path copy = copyLangToOverride(dir, "es");
  LanguageYaml y = loadEditor(copy);
  ClassTexts classes = y.classes();
  classes["NEW_CLASS"] = "t͡ʃ\r\nʝ";
  REQUIRE(classes.erase("STOPS") == 1);
  y.setClasses(classes);
  saveEditor(y);

  const nvsp_frontend::PackSet shipped = loadPackWithOverride("es");
  const nvsp_frontend::PackSet edited = loadPackWithOverride("es", dir);
  REQUIRE(edited.lang.classes.count("NEW_CLASS") == 1);
  CHECK(u8(edited.lang.classes.at("NEW_CLASS")) == std::vector<std::string>{"t͡ʃ", "ʝ"});
  CHECK(edited.lang.classes.count("STOPS") == 0);
  CHECK(edited.lang.classes.at("VOWELS") == shipped.lang.classes.at("VOWELS"));
  CHECK(edited.lang.classes.at("NASAL") == shipped.lang.classes.at("NASAL"));

  // And the editor reads back what it wrote.
  LanguageYaml again = loadEditor(copy);
  CHECK(again.classes().at("NEW_CLASS") == "t͡ʃ\nʝ");
  CHECK(again.classes().count("STOPS") == 0);
}

TEST_CASE("class editor: a file with no classes gets none from an untouched OK") {
  const fs::path dir = editorScratchDir("classes_none");
  const fs::path copy = dir / "packs" / "lang" / "hu.yaml";
  const std::string text = "settings:\n  primaryStressDiv: 1.05\n";
  writeAllBytes(copy, text);
  LanguageYaml y = loadEditor(copy);
  CHECK(y.classes().empty());
  y.setClasses(y.classes());
  saveEditor(y);
  CHECK(readAllBytes(copy) == text);
}

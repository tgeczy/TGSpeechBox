// The phoneme editor's "Yes, copy for this language" (#215).
//
// Editing a shared phoneme (k) with a language loaded asks whether to make a
// copy just for that language (k_ptbr).  When the copy was already there,
// the editor replaced the whole copy with the shared phoneme plus the edit,
// and the tuning that made the copy different was gone: k_ptbr's 18 ms burst
// (Brazilian listeners chose it, #150) became the shared k's 11 ms.
//
// What is checked here, on a copy of the shipped phonemes.yaml: the edit
// lands on the existing copy as an edit, field by field, for k_ptbr and for
// every other language copy in the file; what the edit did not touch stays.

#include "doctest.h"

#include "editor_test_util.h"
#include "yaml_edit.h"

#include <cctype>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using namespace tgsb_test;
using tgsb_editor::PhonemesYaml;

namespace {

fs::path copyPhonemes(const fs::path& dir) {
  const fs::path dst = dir / "packs" / "phonemes.yaml";
  fs::create_directories(dst.parent_path());
  fs::copy_file(repoRoot() / "packs" / "phonemes.yaml", dst, fs::copy_options::overwrite_existing);
  return dst;
}

PhonemesYaml loadEditor(const fs::path& p) {
  PhonemesYaml y;
  std::string err;
  const bool ok = y.load(p.string(), err);
  REQUIRE_MESSAGE(ok, "editor could not load " << p.string() << ": " << err);
  return y;
}

EditorNode scalar(const std::string& s) {
  EditorNode n;
  n.type = EditorNode::Type::Scalar;
  n.scalar = s;
  return n;
}

std::string field(const EditorNode& phoneme, const char* key) {
  const EditorNode* n = phoneme.get(key);
  return (n && n->isScalar()) ? n->scalar : std::string("(none)");
}

// Every (shared, copy) pair in the file: "k" and "k_ptbr", "a" and "a_es"...
std::vector<std::pair<std::string, std::string>> languageCopies(const PhonemesYaml& y) {
  std::vector<std::pair<std::string, std::string>> out;
  for (const std::string& key : y.phonemeKeysSorted()) {
    const size_t us = key.rfind('_');
    if (us == std::string::npos || us == 0 || key.size() - us - 1 < 2) continue;
    bool letters = true;
    for (size_t i = us + 1; i < key.size(); ++i) {
      if (!std::isalpha(static_cast<unsigned char>(key[i]))) letters = false;
    }
    if (!letters) continue;
    const std::string shared = key.substr(0, us);
    const EditorNode* s = y.getPhonemeNode(shared);
    const EditorNode* c = y.getPhonemeNode(key);
    if (s && s->isMap() && c && c->isMap()) out.emplace_back(shared, key);
  }
  return out;
}

}  // namespace

TEST_CASE("copy for this language: an edit of k keeps k_ptbr's own tuning") {
  const fs::path dir = editorScratchDir("phoneme_copy_k_ptbr");
  const fs::path file = copyPhonemes(dir);
  PhonemesYaml y = loadEditor(file);
  const EditorNode* k = y.getPhonemeNode("k");
  const EditorNode* kPtbr = y.getPhonemeNode("k_ptbr");
  REQUIRE(k != nullptr);
  REQUIRE(kPtbr != nullptr);
  REQUIRE(field(*k, "burstDurationMs") == "11");
  REQUIRE(field(*kPtbr, "burstDurationMs") == "18");
  const EditorNode sharedBefore = *k;
  const EditorNode copyBefore = *kPtbr;

  // The edit dialog starts from the shared phoneme; one field is changed.
  EditorNode working = sharedBefore;
  REQUIRE(working.map.count("fricationAmplitude") == 1);
  working.map["fricationAmplitude"] = scalar("0.77");

  REQUIRE(y.applyEditToPhoneme("k_ptbr", sharedBefore, working));

  EditorNode expected = copyBefore;
  expected.map["fricationAmplitude"] = scalar("0.77");
  const EditorNode* after = y.getPhonemeNode("k_ptbr");
  REQUIRE(after != nullptr);
  CHECK(field(*after, "burstDurationMs") == "18");
  CHECK(field(*after, "fricationAmplitude") == "0.77");
  CHECK(sameTree(expected, *after));
  // The shared phoneme is not touched by a "Yes".
  CHECK(sameTree(sharedBefore, *y.getPhonemeNode("k")));

  // And it is what the save writes and the frontend loads for pt-br.
  std::string err;
  REQUIRE(y.save(err));
  fs::create_directories(dir / "packs" / "lang");
  for (const char* tag : {"default", "pt", "pt-br"}) {
    fs::copy_file(shippedLangFile(tag), dir / "packs" / "lang" / (std::string(tag) + ".yaml"));
  }
  nvsp_frontend::PackSet pack;
  REQUIRE(nvsp_frontend::loadPackSet(dir.string(), "pt-br", pack, err));
  const auto it = pack.phonemes.find(U"k_ptbr");
  REQUIRE(it != pack.phonemes.end());
  CHECK(it->second.burstDurationMs == doctest::Approx(18.0));
}

TEST_CASE("copy for this language: added and removed fields are carried over, the rest stays") {
  const fs::path dir = editorScratchDir("phoneme_copy_add_remove");
  PhonemesYaml y = loadEditor(copyPhonemes(dir));
  const EditorNode sharedBefore = *y.getPhonemeNode("k");
  const EditorNode copyBefore = *y.getPhonemeNode("k_ptbr");
  REQUIRE(sharedBefore.map.count("_isStop") == 1);
  REQUIRE(copyBefore.map.count("_isStop") == 1);

  EditorNode working = sharedBefore;
  working.map["testOnlyField"] = scalar("3");
  working.keyOrder.push_back("testOnlyField");
  working.map.erase("_isStop");

  REQUIRE(y.applyEditToPhoneme("k_ptbr", sharedBefore, working));

  EditorNode expected = copyBefore;
  expected.map["testOnlyField"] = scalar("3");
  expected.map.erase("_isStop");
  const EditorNode* after = y.getPhonemeNode("k_ptbr");
  CHECK(sameTree(expected, *after));
  CHECK(field(*after, "burstDurationMs") == "18");
  // A removed key is out of the key order too, or the save would trip on it.
  CHECK(std::find(after->keyOrder.begin(), after->keyOrder.end(), "_isStop") == after->keyOrder.end());
  CHECK(std::find(after->keyOrder.begin(), after->keyOrder.end(), "testOnlyField") != after->keyOrder.end());
}

TEST_CASE("copy for this language: an edit inside frameEx keeps the copy's other frameEx fields") {
  const fs::path dir = editorScratchDir("phoneme_copy_frameex");
  const fs::path file = dir / "packs" / "phonemes.yaml";
  writeAllBytes(file,
      "phonemes:\n"
      "  x:\n"
      "    cf1: 500\n"
      "    frameEx:\n"
      "      creakiness: 0.1\n"
      "      breathiness: 0.2\n"
      "  x_hu:\n"
      "    cf1: 450\n"
      "    frameEx:\n"
      "      creakiness: 0.1\n"
      "      breathiness: 0.6\n"
      "      jitter: 0.05\n");
  PhonemesYaml y = loadEditor(file);
  const EditorNode sharedBefore = *y.getPhonemeNode("x");
  EditorNode working = sharedBefore;
  working.map["frameEx"].map["creakiness"] = scalar("0.3");

  REQUIRE(y.applyEditToPhoneme("x_hu", sharedBefore, working));
  const EditorNode* after = y.getPhonemeNode("x_hu");
  CHECK(field(*after, "cf1") == "450");
  const EditorNode* ex = after->get("frameEx");
  REQUIRE(ex != nullptr);
  REQUIRE(ex->isMap());
  CHECK(field(*ex, "creakiness") == "0.3");
  CHECK(field(*ex, "breathiness") == "0.6");
  CHECK(field(*ex, "jitter") == "0.05");
}

// The reviewer's case: the shared phoneme has no frameEx block at all and the
// copy has one (in phonemes.yaml: the palatal lateral and its Spanish copy).
// The dialog creates the block when a frameEx field is first set, so the edit
// arrives as "frameEx added"; that must add the one field to the copy's block,
// not replace the block.
TEST_CASE("copy for this language: a frameEx field added to a shared phoneme without the block keeps the copy's block") {
  const fs::path dir = editorScratchDir("phoneme_copy_new_frameex");
  const fs::path file = dir / "packs" / "phonemes.yaml";
  writeAllBytes(file,
      "phonemes:\n"
      "  k:\n"
      "    cf1: 500\n"
      "  k_xx:\n"
      "    cf1: 450\n"
      "    frameEx:\n"
      "      creakiness: 0.1\n"
      "      breathiness: 0.6\n");
  PhonemesYaml y = loadEditor(file);
  const EditorNode sharedBefore = *y.getPhonemeNode("k");
  EditorNode working = sharedBefore;
  EditorNode block;
  block.type = EditorNode::Type::Map;
  block.map["jitter"] = scalar("0.05");
  block.keyOrder.push_back("jitter");
  working.map["frameEx"] = block;
  working.keyOrder.push_back("frameEx");

  REQUIRE(y.applyEditToPhoneme("k_xx", sharedBefore, working));
  const EditorNode* after = y.getPhonemeNode("k_xx");
  CHECK(field(*after, "cf1") == "450");
  const EditorNode* ex = after->get("frameEx");
  REQUIRE(ex != nullptr);
  REQUIRE(ex->isMap());
  CHECK(field(*ex, "jitter") == "0.05");
  CHECK(field(*ex, "creakiness") == "0.1");
  CHECK(field(*ex, "breathiness") == "0.6");
}

TEST_CASE("copy for this language: every language copy in phonemes.yaml takes a one-field edit as one field") {
  const fs::path dir = editorScratchDir("phoneme_copy_all");
  PhonemesYaml y = loadEditor(copyPhonemes(dir));
  const auto pairs = languageCopies(y);
  // a_es, s_es, s_mx, e_hu, k_ptbr, a_gb and the rest.
  REQUIRE(pairs.size() >= 30);
  for (const auto& pr : pairs) {
    INFO("shared: " << pr.first << "  copy: " << pr.second);
    const EditorNode sharedBefore = *y.getPhonemeNode(pr.first);
    const EditorNode copyBefore = *y.getPhonemeNode(pr.second);
    EditorNode working = sharedBefore;
    working.map["voiceAmplitude"] = scalar("0.123");

    REQUIRE(y.applyEditToPhoneme(pr.second, sharedBefore, working));
    EditorNode expected = copyBefore;
    expected.map["voiceAmplitude"] = scalar("0.123");
    CHECK(sameTree(expected, *y.getPhonemeNode(pr.second)));
  }
}

TEST_CASE("copy for this language: a copy that is not there is reported, not made") {
  const fs::path dir = editorScratchDir("phoneme_copy_missing");
  PhonemesYaml y = loadEditor(copyPhonemes(dir));
  const EditorNode shared = *y.getPhonemeNode("k");
  CHECK(!y.applyEditToPhoneme("k_nosuchlang", shared, shared));
  CHECK(y.getPhonemeNode("k_nosuchlang") == nullptr);
}

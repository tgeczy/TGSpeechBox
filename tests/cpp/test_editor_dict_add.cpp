// The phoneme editor's dictionary dialog: Add on a word that is already in
// the dictionary (#221).
//
// Add put the new entry at the end of the list without looking for the
// word, and Save wrote the file with the word in it twice.  For stress,
// compound and character dictionaries the frontend keeps the first row of a
// word and ignores the rest, so the added entry was saved and never used;
// en-us-stress.tsv has 110,000 rows and the list shows 5,000, so the tuner
// could not even see the row in the way.  For pronunciation and user
// dictionaries the last row wins, so the same Add did take effect there and
// left a dead row behind.
//
// What is checked here: Add on an existing word replaces its row, where it
// stands, in every dictionary type; the saved file has the word once, and
// the frontend's loader then gives the new entry.

#include "doctest.h"

#include "dict_tsv.h"
#include "editor_test_util.h"

#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace tgsb_test;
using tgsb_editor::DictEntryData;

namespace {

// A scratch pack for language "en" that the frontend can load.
fs::path scratchPack(const std::string& name) {
  const fs::path packs = editorScratchDir(name) / "packs";
  fs::create_directories(packs / "lang");
  fs::create_directories(packs / "dict");
  fs::copy_file(repoRoot() / "packs" / "phonemes.yaml", packs / "phonemes.yaml");
  for (const char* tag : {"default", "en"}) {
    fs::copy_file(shippedLangFile(tag), packs / "lang" / (std::string(tag) + ".yaml"));
  }
  return packs;
}

nvsp_frontend::PackSet loadScratch(const fs::path& packs) {
  nvsp_frontend::PackSet pack;
  std::string err;
  const bool ok = nvsp_frontend::loadPackSet(packs.parent_path().string(), "en", pack, err);
  REQUIRE_MESSAGE(ok, "loadPackSet failed: " << err);
  return pack;
}

// Open the file as the dialog does, Add an entry, Save.  Returns the rows saved.
std::vector<DictEntryData> openAddSave(const fs::path& file, const char* type, const DictEntryData& entry,
                                       bool* wasThere = nullptr) {
  std::vector<DictEntryData> entries;
  std::string err;
  REQUIRE(tgsb_editor::loadDictTsv(file.wstring(), type, entries, err));
  const bool there = tgsb_editor::putDictEntry(entries, entry);
  if (wasThere) *wasThere = there;
  REQUIRE(tgsb_editor::saveDictTsv(file.wstring(), type, entries, err));
  std::vector<DictEntryData> saved;
  REQUIRE(tgsb_editor::loadDictTsv(file.wstring(), type, saved, err));
  return saved;
}

size_t rowsFor(const std::vector<DictEntryData>& entries, const std::string& word) {
  size_t n = 0;
  for (const auto& e : entries) if (e.fromText == word) ++n;
  return n;
}

}  // namespace

TEST_CASE("dictionary add: a stress pattern added for a word already there is the one used") {
  const fs::path packs = scratchPack("dict_add_stress");
  const fs::path file = packs / "dict" / "en-stress.tsv";
  writeAllBytes(file, "# test\naaqq\t1 0\nbbqq\t1 0\nccqq\t0 1\n");
  REQUIRE(loadScratch(packs).stressDict.at("bbqq") == std::vector<int>{1, 0});

  bool wasThere = false;
  const auto saved = openAddSave(file, "stress", {"bbqq", "0 1", "", "", ""}, &wasThere);
  CHECK(wasThere);
  CHECK(saved.size() == 3);
  CHECK(rowsFor(saved, "bbqq") == 1);
  // The row is where it was.
  REQUIRE(saved.size() >= 2);
  CHECK(saved[1].fromText == "bbqq");
  CHECK(saved[1].toText == "0 1");

  const nvsp_frontend::PackSet pack = loadScratch(packs);
  CHECK(pack.stressDict.at("bbqq") == std::vector<int>{0, 1});
  CHECK(pack.stressDict.at("aaqq") == std::vector<int>{1, 0});
  CHECK(pack.stressDict.at("ccqq") == std::vector<int>{0, 1});
}

TEST_CASE("dictionary add: a compound split added for a word already there is the one used") {
  const fs::path packs = scratchPack("dict_add_compound");
  const fs::path file = packs / "dict" / "en-compounds.tsv";
  writeAllBytes(file, "# test\naaqq\taa qq\nbbqq\tbb qq\n");
  const auto saved = openAddSave(file, "compound", {"aaqq", "a aqq", "", "", ""});
  CHECK(rowsFor(saved, "aaqq") == 1);
  const nvsp_frontend::PackSet pack = loadScratch(packs);
  CHECK(pack.compoundMap.at("aaqq") == std::vector<std::string>{"a", "aqq"});
  CHECK(pack.compoundMap.at("bbqq") == std::vector<std::string>{"bb", "qq"});
}

TEST_CASE("dictionary add: a character name added for a symbol already there is the one used") {
  const fs::path packs = scratchPack("dict_add_character");
  const fs::path file = packs / "dict" / "en-letters.tsv";
  writeAllBytes(file, "# test\n§\tqq one\n¶\tqq two\n");
  const auto saved = openAddSave(file, "character", {"§", "qq three", "", "", ""});
  CHECK(rowsFor(saved, "§") == 1);
  const nvsp_frontend::PackSet pack = loadScratch(packs);
  CHECK(pack.letterDict.at("§") == "qq three");
  CHECK(pack.letterDict.at("¶") == "qq two");
}

TEST_CASE("dictionary add: pronunciation and user entries are replaced too, not doubled") {
  for (const char* type : {"pronounce", "user"}) {
    INFO("type: " << std::string(type));
    const fs::path packs = scratchPack(std::string("dict_add_") + type);
    const fs::path file = packs / "dict" / (std::string("en-") + (std::string(type) == "user" ? "user" : "dict") + ".tsv");
    writeAllBytes(file, "# test\nqqaa\tkyoo ay\t\t\tnames\nqqbb\tkyoo bee\n");
    const auto saved = openAddSave(file, type, {"qqaa", "queue a", "", "", "tech"});
    CHECK(saved.size() == 2);
    CHECK(rowsFor(saved, "qqaa") == 1);
    REQUIRE(!saved.empty());
    CHECK(saved[0].fromText == "qqaa");
    CHECK(saved[0].toText == "queue a");
    CHECK(saved[0].category == "tech");
    const nvsp_frontend::PackSet pack = loadScratch(packs);
    REQUIRE(pack.pronDict.entries.count("qqaa") == 1);
    CHECK(pack.pronDict.entries.at("qqaa").toText == "queue a");
    CHECK(pack.pronDict.entries.count("qqbb") == 1);
  }
}

TEST_CASE("dictionary add: a new word goes to the end, and other words are left alone") {
  std::vector<DictEntryData> entries = {{"aaqq", "1 0", "", "", ""}, {"bbqq", "0 1", "", "", ""}};
  CHECK(!tgsb_editor::putDictEntry(entries, {"ccqq", "1 0 0", "", "", ""}));
  REQUIRE(entries.size() == 3);
  CHECK(entries[0].fromText == "aaqq");
  CHECK(entries[1].fromText == "bbqq");
  CHECK(entries[2].fromText == "ccqq");
  // Words are compared exactly, as the frontend's loaders compare them.
  CHECK(!tgsb_editor::putDictEntry(entries, {"AAQQ", "0 1", "", "", ""}));
  CHECK(entries.size() == 4);
  CHECK(tgsb_editor::findDictEntry(entries, "bbqq") == 1);
  CHECK(tgsb_editor::findDictEntry(entries, "zzqq") == tgsb_editor::kNoDictEntry);
}

TEST_CASE("dictionary add: a word a file already has twice ends up there once") {
  // What an earlier Add left behind.
  std::vector<DictEntryData> entries = {
      {"aaqq", "1 0", "", "", ""}, {"bbqq", "1 0", "", "", ""}, {"aaqq", "0 1", "", "", ""}};
  CHECK(tgsb_editor::putDictEntry(entries, {"aaqq", "1 1", "", "", ""}));
  REQUIRE(entries.size() == 2);
  CHECK(entries[0].fromText == "aaqq");
  CHECK(entries[0].toText == "1 1");
  CHECK(entries[1].fromText == "bbqq");
}

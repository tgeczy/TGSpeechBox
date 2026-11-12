// The phoneme editor's dictionary dialog and the dictionaries a language's
// regional varieties share (#216).
//
// English compound splits are one file, en-compounds.tsv, which en-us reads
// too; Spanish and Portuguese letter names are es-letters.tsv and
// pt-letters.tsv, read by es-mx, es-es, pt-br and pt-pt.  The frontend reads
// the base language's file only while the regional one has no entries.  The
// dialog built its file name from the exact tag: on en-us the Compound list
// was empty, and saving one entry made an en-us-compounds.tsv holding only
// that entry, which hid all 3,600 shared splits from en-us from then on.
//
// What is checked here: the dialog's file for a language and type is the one
// the frontend reads that language's entries from, in the shipped packs and
// in a scratch pack; an entry added on a regional tag joins the shared
// entries instead of replacing them, as the frontend's own loader sees it.

#include "doctest.h"

#include "dict_tsv.h"
#include "editor_test_util.h"

#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace tgsb_test;
using tgsb_editor::DictEntryData;

namespace {

std::string fileToEdit(const fs::path& packs, const char* lang, const char* type) {
  return fs::path(tgsb_editor::dictTsvPathToEdit(packs.wstring(), lang, type)).filename().string();
}

// A scratch pack the frontend can load: the phoneme table, the language
// files asked for, and an empty dict folder.
fs::path scratchPack(const std::string& name, std::initializer_list<const char*> langs) {
  const fs::path packs = editorScratchDir(name) / "packs";
  fs::create_directories(packs / "lang");
  fs::create_directories(packs / "dict");
  fs::copy_file(repoRoot() / "packs" / "phonemes.yaml", packs / "phonemes.yaml");
  for (const char* tag : langs) {
    fs::copy_file(shippedLangFile(tag), packs / "lang" / (std::string(tag) + ".yaml"));
  }
  return packs;
}

nvsp_frontend::PackSet loadScratch(const fs::path& packs, const std::string& tag) {
  nvsp_frontend::PackSet pack;
  std::string err;
  const bool ok = nvsp_frontend::loadPackSet(packs.parent_path().string(), tag, pack, err);
  REQUIRE_MESSAGE(ok, "loadPackSet failed for '" << tag << "': " << err);
  return pack;
}

// What the dialog does: open the file for the language and type, add an
// entry, save.
size_t openAddSave(const fs::path& packs, const char* lang, const char* type, const DictEntryData& entry) {
  const std::wstring path = tgsb_editor::dictTsvPathToEdit(packs.wstring(), lang, type);
  std::vector<DictEntryData> entries;
  std::string err;
  if (fs::exists(path)) REQUIRE(tgsb_editor::loadDictTsv(path, type, entries, err));
  const size_t shown = entries.size();
  entries.push_back(entry);
  REQUIRE(tgsb_editor::saveDictTsv(path, type, entries, err));
  return shown;
}

}  // namespace

TEST_CASE("shared dictionaries: in the shipped packs a regional tag opens the file it speaks from") {
  const fs::path packs = repoRoot() / "packs";
  // Shared by every variety: there is no regional file.
  CHECK(fileToEdit(packs, "en-us", "compound") == "en-compounds.tsv");
  CHECK(fileToEdit(packs, "en-gb", "compound") == "en-compounds.tsv");
  CHECK(fileToEdit(packs, "es-mx", "character") == "es-letters.tsv");
  CHECK(fileToEdit(packs, "es-es", "character") == "es-letters.tsv");
  CHECK(fileToEdit(packs, "pt-br", "character") == "pt-letters.tsv");
  CHECK(fileToEdit(packs, "pt-pt", "character") == "pt-letters.tsv");
  // The base tag is its own file.
  CHECK(fileToEdit(packs, "en", "compound") == "en-compounds.tsv");
  CHECK(fileToEdit(packs, "es", "character") == "es-letters.tsv");
  // A regional file with entries is the one to edit.
  CHECK(fileToEdit(packs, "en-us", "pronounce") == "en-us-dict.tsv");
  // The frontend has no base-language fallback for stress dictionaries, and
  // reads a language's user file next to the base one, not instead of it.
  CHECK(fileToEdit(packs, "en-us", "stress") == "en-us-stress.tsv");
  CHECK(fileToEdit(packs, "en-gb", "stress") == "en-gb-stress.tsv");
  CHECK(fileToEdit(packs, "en-au", "stress") == "en-au-stress.tsv");
  CHECK(fileToEdit(packs, "en-gb", "user") == "en-gb-user.tsv");
  // Nothing shared to fall back to: the language's own (new) file.
  CHECK(fileToEdit(packs, "hu", "compound") == "hu-compounds.tsv");
  CHECK(fileToEdit(packs, "en-gb", "character") == "en-gb-letters.tsv");
}

TEST_CASE("shared dictionaries: the regional file is used once it has entries, as in the frontend") {
  const fs::path packs = editorScratchDir("dict_shared_rule") / "packs";
  writeAllBytes(packs / "dict" / "en-compounds.tsv", "# shared\naaqq\taa qq\nbbqq\tbb qq\n");
  CHECK(fileToEdit(packs, "en-us", "compound") == "en-compounds.tsv");

  // A regional file with nothing but a header has no entries: the frontend
  // still reads the shared file.
  writeAllBytes(packs / "dict" / "en-us-compounds.tsv", "# en-us only\n\n");
  CHECK(fileToEdit(packs, "en-us", "compound") == "en-compounds.tsv");

  writeAllBytes(packs / "dict" / "en-us-compounds.tsv", "# en-us only\nccqq\tcc qq\n");
  CHECK(fileToEdit(packs, "en-us", "compound") == "en-us-compounds.tsv");
  CHECK(fileToEdit(packs, "en-gb", "compound") == "en-compounds.tsv");

  // The same rule for pronunciation dictionaries and letter names.
  writeAllBytes(packs / "dict" / "en-dict.tsv", "# shared\nqqa\tkyoo ay\n");
  CHECK(fileToEdit(packs, "en-gb", "pronounce") == "en-dict.tsv");
  writeAllBytes(packs / "dict" / "es-letters.tsv", "# shared\n§\tqq\n");
  CHECK(fileToEdit(packs, "es-mx", "character") == "es-letters.tsv");
}

TEST_CASE("shared dictionaries: a compound added on en-us joins the shared splits") {
  const fs::path packs = scratchPack("dict_shared_compound", {"default", "en", "en-us"});
  writeAllBytes(packs / "dict" / "en-compounds.tsv",
                "# shared by every English\naaqq\taa qq\nbbqq\tbb qq\nccqq\tcc qq\n");
  REQUIRE(loadScratch(packs, "en-us").compoundMap.size() == 3);

  const size_t shown = openAddSave(packs, "en-us", "compound", {"ddqq", "dd qq", "", "", ""});
  CHECK(shown == 3);  // the list the tuner sees is not empty

  const nvsp_frontend::PackSet enUs = loadScratch(packs, "en-us");
  CHECK(enUs.compoundMap.size() == 4);
  CHECK(enUs.compoundMap.count("aaqq") == 1);
  CHECK(enUs.compoundMap.count("ddqq") == 1);
  CHECK(!fs::exists(packs / "dict" / "en-us-compounds.tsv"));
  CHECK(readAllBytes(packs / "dict" / "en-compounds.tsv").rfind("# shared by every English\n", 0) == 0);
}

TEST_CASE("shared dictionaries: a character added on es-mx keeps the shipped Spanish letter names") {
  const fs::path packs = scratchPack("dict_shared_letters", {"default", "es", "es-mx"});
  fs::copy_file(repoRoot() / "packs" / "dict" / "es-letters.tsv", packs / "dict" / "es-letters.tsv");
  const nvsp_frontend::PackSet before = loadScratch(packs, "es-mx");
  REQUIRE(before.letterDict.size() > 5);
  REQUIRE(before.letterDict.count("ñ") == 1);

  const size_t shown = openAddSave(packs, "es-mx", "character", {"§", "qq", "", "", ""});
  CHECK(shown == before.letterDict.size());

  const nvsp_frontend::PackSet after = loadScratch(packs, "es-mx");
  CHECK(after.letterDict.size() == before.letterDict.size() + 1);
  CHECK(after.letterDict.count("ñ") == 1);
  CHECK(after.letterDict.at("ñ") == before.letterDict.at("ñ"));
  CHECK(after.letterDict.count("§") == 1);
  CHECK(!fs::exists(packs / "dict" / "es-mx-letters.tsv"));
}

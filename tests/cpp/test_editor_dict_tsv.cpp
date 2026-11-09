// The phoneme editor's dictionary files: which file each dictionary type
// edits, and what a save keeps.
//
// The editor's "User" type edited <lang>-dict.tsv, the shipped pronunciation
// dictionary, instead of <lang>-user.tsv (Dictionary-editing.md, and every
// other platform, put user entries there).  And a save rewrote the file with a
// one-line "# TGSpeechBox dictionary" header, dropping the comments at the top:
// for en-us-stress.tsv and en-gb-stress.tsv those are the CMU and Reece Dunn
// copyright and BSD licence notices, which a redistributed copy must keep.

#include "doctest.h"

#include "dict_tsv.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using tgsb_editor::DictEntryData;

namespace {

fs::path scratchDir(const char* name) {
  fs::path d = fs::temp_directory_path() / "tgsb_editor_dict_tsv" / name;
  fs::remove_all(d);
  fs::create_directories(d);
  return d;
}

std::string readAll(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

void writeAll(const fs::path& p, const std::string& s) {
  std::ofstream f(p, std::ios::binary);
  f << s;
}

std::string fileName(const std::wstring& p) { return fs::path(p).filename().string(); }

}  // namespace

TEST_CASE("each dictionary type edits its own file") {
  const std::wstring packs = fs::path("packs").wstring();
  CHECK(fileName(tgsb_editor::dictTsvPath(packs, "en-us", "pronounce")) == "en-us-dict.tsv");
  CHECK(fileName(tgsb_editor::dictTsvPath(packs, "en-us", "stress")) == "en-us-stress.tsv");
  CHECK(fileName(tgsb_editor::dictTsvPath(packs, "en-us", "compound")) == "en-us-compounds.tsv");
  CHECK(fileName(tgsb_editor::dictTsvPath(packs, "pt-br", "character")) == "pt-br-letters.tsv");
  CHECK(fileName(tgsb_editor::dictTsvPath(packs, "en-us", "user")) == "en-us-user.tsv");
  CHECK(fs::path(tgsb_editor::dictTsvPath(packs, "hu", "user")).parent_path().filename() == "dict");
}

TEST_CASE("a save keeps the comments at the top of the file") {
  const fs::path p = scratchDir("keep_header") / "en-us-stress.tsv";
  const std::string header =
      "# Derived from the CMU Pronouncing Dictionary (https://github.com/cmusphinx/cmudict).\n"
      "# Copyright (C) 1993-2015 Carnegie Mellon University. BSD 2-clause license: see LICENSE-CMUdict.txt.\n";
  writeAll(p, header + "'frisco\t1 0\nabandon\t0 1 0\n");

  std::vector<DictEntryData> entries;
  std::string err;
  REQUIRE(tgsb_editor::loadDictTsv(p.wstring(), "stress", entries, err));
  REQUIRE(entries.size() == 2);
  entries.push_back({"zebra", "1 0", "", "", ""});
  REQUIRE(tgsb_editor::saveDictTsv(p.wstring(), "stress", entries, err));

  const std::string out = readAll(p);
  CHECK(out.rfind(header, 0) == 0);
  CHECK(out.find("# TGSpeechBox dictionary") == std::string::npos);
  CHECK(out.find("zebra\t1 0\n") != std::string::npos);

  std::vector<DictEntryData> again;
  REQUIRE(tgsb_editor::loadDictTsv(p.wstring(), "stress", again, err));
  CHECK(again.size() == 3);
}

TEST_CASE("a header written with CRLF line ends is kept whole") {
  const fs::path p = scratchDir("crlf_header") / "pt-letters.tsv";
  writeAll(p, "# Portuguese letter names (#123).\r\n#\r\nç\tcê cedilha\r\n");
  std::vector<DictEntryData> entries;
  std::string err;
  REQUIRE(tgsb_editor::loadDictTsv(p.wstring(), "character", entries, err));
  REQUIRE(tgsb_editor::saveDictTsv(p.wstring(), "character", entries, err));
  const std::string out = readAll(p);
  CHECK(out.rfind("# Portuguese letter names (#123).\n#\n", 0) == 0);
  CHECK(out.find("ç\tcê cedilha\n") != std::string::npos);
}

TEST_CASE("a new file gets a header naming its format") {
  const fs::path p = scratchDir("new_file") / "hu-user.tsv";
  std::vector<DictEntryData> entries = {{"Arató", "aratoo", "", "", "names"}};
  std::string err;
  REQUIRE(tgsb_editor::saveDictTsv(p.wstring(), "user", entries, err));
  const std::string out = readAll(p);
  CHECK(out.rfind("# ", 0) == 0);
  CHECK(out.find("Arató\taratoo\t\t\tnames\n") != std::string::npos);
}

TEST_CASE("user entries keep all five columns") {
  const fs::path p = scratchDir("user_cols") / "en-us-user.tsv";
  std::vector<DictEntryData> entries = {{"GIF", "jif", "", "dʒˈɪf", "tech"}};
  std::string err;
  REQUIRE(tgsb_editor::saveDictTsv(p.wstring(), "user", entries, err));
  std::vector<DictEntryData> back;
  REQUIRE(tgsb_editor::loadDictTsv(p.wstring(), "user", back, err));
  REQUIRE(back.size() == 1);
  CHECK(back[0].fromText == "GIF");
  CHECK(back[0].toText == "jif");
  CHECK(back[0].toIpa == "dʒˈɪf");
  CHECK(back[0].category == "tech");
}

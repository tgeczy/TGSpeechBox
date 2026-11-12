/*
TGSpeechBox — Dictionary TSV files for the Win32 phoneme editor.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#include "dict_tsv.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace tgsb_editor {

namespace {

std::vector<std::string> splitTabs(const std::string& line) {
  std::vector<std::string> cols;
  std::string cur;
  for (char c : line) {
    if (c == '\t') {
      cols.push_back(cur);
      cur.clear();
    } else {
      cur += c;
    }
  }
  cols.push_back(cur);
  return cols;
}

bool isPronounceLike(const std::string& dictType) {
  return dictType == "pronounce" || dictType == "user";
}

std::string pathForMessage(const fs::path& p) {
  return p.u8string();
}

// The file's lines, without a UTF-8 BOM, split on \r\n, \r or \n.
bool readLines(const fs::path& p, std::vector<std::string>& lines) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  std::string data = ss.str();
  if (data.size() >= 3 && static_cast<unsigned char>(data[0]) == 0xEF &&
      static_cast<unsigned char>(data[1]) == 0xBB && static_cast<unsigned char>(data[2]) == 0xBF) {
    data = data.substr(3);
  }
  std::string line;
  for (size_t i = 0; i < data.size(); ++i) {
    if (data[i] == '\r') {
      lines.push_back(line);
      line.clear();
      if (i + 1 < data.size() && data[i + 1] == '\n') ++i;
    } else if (data[i] == '\n') {
      lines.push_back(line);
      line.clear();
    } else {
      line += data[i];
    }
  }
  if (!line.empty()) lines.push_back(line);
  return true;
}

}  // namespace

std::wstring dictTsvPath(const std::wstring& packDir, const std::string& langTag,
                         const std::string& dictType) {
  fs::path base = fs::path(packDir) / "dict";
  const std::string& tag = langTag;
  if (dictType == "user") {
    // The listener's own entries, kept apart from the shipped dictionary
    // (Dictionary-editing.md; the NVDA, Android and iOS editors use it too).
    return (base / (tag + "-user.tsv")).wstring();
  } else if (dictType == "stress") {
    return (base / (tag + "-stress.tsv")).wstring();
  } else if (dictType == "compound") {
    return (base / (tag + "-compounds.tsv")).wstring();
  } else if (dictType == "character") {
    return (base / (tag + "-letters.tsv")).wstring();
  }
  return (base / (tag + "-dict.tsv")).wstring();  // "pronounce"
}

// Compound splits, letter names and pronunciations may be shared by every
// regional variety of a language: en-us reads en-compounds.tsv, es-mx reads
// es-letters.tsv.  The frontend (loadPackSet in pack.cpp) reads the base
// language's file whenever the regional one has no entries, so a regional
// file made for one new entry would hide the whole shared file from that
// variety (#216).  The file to edit is therefore the one the frontend reads:
// the regional file once it has entries, else the base language's if there
// is one.  Stress dictionaries have no such fallback, and a language's user
// file is read next to the base one, not instead of it.
std::wstring dictTsvPathToEdit(const std::wstring& packDir, const std::string& langTag,
                               const std::string& dictType) {
  const std::wstring own = dictTsvPath(packDir, langTag, dictType);
  if (dictType != "compound" && dictType != "character" && dictType != "pronounce") return own;
  const size_t dash = langTag.find('-');
  if (dash == std::string::npos) return own;

  std::vector<DictEntryData> entries;
  std::string err;
  if (loadDictTsv(own, dictType, entries, err) && !entries.empty()) return own;

  const std::wstring shared = dictTsvPath(packDir, langTag.substr(0, dash), dictType);
  std::error_code ec;
  if (fs::exists(fs::path(shared), ec)) return shared;
  return own;
}

bool loadDictTsv(const std::wstring& path, const std::string& dictType,
                 std::vector<DictEntryData>& out, std::string& outError) {
  out.clear();
  std::vector<std::string> lines;
  if (!readLines(fs::path(path), lines)) {
    outError = "Could not open file: " + pathForMessage(fs::path(path));
    return false;
  }

  for (const auto& ln : lines) {
    if (ln.empty() || ln[0] == '#') continue;

    auto cols = splitTabs(ln);
    DictEntryData e;
    if (isPronounceLike(dictType)) {
      e.fromText = cols.size() > 0 ? cols[0] : "";
      e.toText   = cols.size() > 1 ? cols[1] : "";
      e.fromIpa  = cols.size() > 2 ? cols[2] : "";
      e.toIpa    = cols.size() > 3 ? cols[3] : "";
      e.category = cols.size() > 4 ? cols[4] : "";
    } else if (dictType == "stress" || dictType == "compound") {
      e.fromText = cols[0];
      // Join remaining columns with tab (stress patterns may have tabs).
      for (size_t c = 1; c < cols.size(); ++c) {
        if (c > 1) e.toText += '\t';
        e.toText += cols[c];
      }
    } else {
      e.fromText = cols.size() > 0 ? cols[0] : "";
      e.toText   = cols.size() > 1 ? cols[1] : "";
    }

    if (!e.fromText.empty()) out.push_back(std::move(e));
  }
  return true;
}

bool saveDictTsv(const std::wstring& path, const std::string& dictType,
                 const std::vector<DictEntryData>& entries, std::string& outError) {
  const fs::path p(path);
  const fs::path parent = p.parent_path();
  if (!parent.empty() && !fs::exists(parent)) {
    std::error_code ec;
    fs::create_directories(parent, ec);
    if (ec) {
      outError = "Could not create directory: " + pathForMessage(parent);
      return false;
    }
  }

  // Keep the comments at the top of the file: they say where its entries
  // come from and under which licence (the CMU and Reece Dunn notices in the
  // English stress dictionaries must stay with them).  A new file gets a
  // header naming its format.
  std::vector<std::string> old;
  std::vector<std::string> header;
  if (readLines(p, old)) {
    for (const auto& ln : old) {
      if (!ln.empty() && ln[0] != '#') break;
      header.push_back(ln);
    }
    while (!header.empty() && header.back().empty()) header.pop_back();
  }
  if (header.empty()) {
    header.push_back("# TGSpeechBox dictionary");
    if (isPronounceLike(dictType)) header.push_back("# Format: from_text\tto_text\tfrom_ipa\tto_ipa\tcategory");
  }

  std::ostringstream body;
  for (const auto& ln : header) body << ln << '\n';

  for (const auto& e : entries) {
    if (isPronounceLike(dictType)) {
      // Write all columns, trim trailing empty ones.
      body << e.fromText << '\t' << e.toText;
      if (!e.category.empty()) {
        body << '\t' << e.fromIpa << '\t' << e.toIpa << '\t' << e.category;
      } else if (!e.toIpa.empty()) {
        body << '\t' << e.fromIpa << '\t' << e.toIpa;
      } else if (!e.fromIpa.empty()) {
        body << '\t' << e.fromIpa;
      }
      body << '\n';
    } else {
      // stress, compound, character: fromText\ttoText
      body << e.fromText << '\t' << e.toText << '\n';
    }
  }

  std::ofstream f(p, std::ios::binary | std::ios::trunc);
  if (!f) {
    outError = "Could not open file for writing: " + pathForMessage(p);
    return false;
  }
  f << body.str();
  return static_cast<bool>(f);
}

} // namespace tgsb_editor

/*
TGSpeechBox — Dictionary TSV files for the Win32 phoneme editor.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

// Which file each dictionary type edits, and reading and writing it.  No
// Win32 here, so tests/cpp can check it on every platform.

#pragma once

#include <string>
#include <vector>

namespace tgsb_editor {

struct DictEntryData {
  std::string fromText;
  std::string toText;
  std::string fromIpa;
  std::string toIpa;
  std::string category;
};

// dictType: "pronounce", "stress", "compound", "character" or "user".
bool loadDictTsv(const std::wstring& path, const std::string& dictType,
                 std::vector<DictEntryData>& out, std::string& outError);
bool saveDictTsv(const std::wstring& path, const std::string& dictType,
                 const std::vector<DictEntryData>& entries, std::string& outError);

// packs/dict/<lang>-<file>.tsv for a dictionary type.
std::wstring dictTsvPath(const std::wstring& packDir, const std::string& langTag,
                         const std::string& dictType);

// The file the dictionary dialog opens and saves for a language and type:
// the one the frontend reads that language's entries from.
std::wstring dictTsvPathToEdit(const std::wstring& packDir, const std::string& langTag,
                               const std::string& dictType);

} // namespace tgsb_editor

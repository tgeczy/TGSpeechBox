/*
TGSpeechBox — Dictionary TSV files for the Win32 phoneme editor.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

// Which file each dictionary type edits, and reading and writing it.  No
// Win32 here, so tests/cpp can check it on every platform.

#pragma once

#include <cstddef>
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

// The row holding a word (fromText, compared exactly, as the frontend's
// loaders do), or kNoDictEntry.
constexpr size_t kNoDictEntry = static_cast<size_t>(-1);
size_t findDictEntry(const std::vector<DictEntryData>& entries, const std::string& fromText);

// "Add" in the dictionary dialog.  Returns true if the word was already
// there.
bool putDictEntry(std::vector<DictEntryData>& entries, const DictEntryData& entry);

// packs/dict/<lang>-<file>.tsv for a dictionary type.
std::wstring dictTsvPath(const std::wstring& packDir, const std::string& langTag,
                         const std::string& dictType);

// The file the dictionary dialog opens and saves for a language and type:
// the one the frontend reads that language's entries from.
std::wstring dictTsvPathToEdit(const std::wstring& packDir, const std::string& langTag,
                               const std::string& dictType);

} // namespace tgsb_editor

/*
TGSpeechBox — Dictionary editor for Win32 phoneme editor.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#pragma once

#define UNICODE
#define _UNICODE

#include <string>
#include <vector>
#include <functional>
#include <windows.h>

#include "dict_tsv.h"

namespace tgsb_editor {

struct PhonemeKeyInfo {
  std::string key;
  std::string cls;  // "vowel", "stop", etc.
};

struct DictionaryEditorState {
  // Input
  std::wstring packDir;                    // packs root (for dict/ subdir)
  std::vector<std::string> availableLangs; // discovered from packs/lang/*.yaml
  std::string initialLang;                 // pre-selected language

  // IPA conversion callback (wraps phonemizer CLI)
  std::function<bool(const std::wstring& text, std::string& outIpa, std::string& outErr)> convertToIpa;

  // Phoneme key list callback (for "Insert phoneme..." picker)
  std::function<std::vector<PhonemeKeyInfo>()> getPhonemeKeys;

  // Preview callback: synthesize a single phoneme key and play it.
  // Matches mobile "preview phoneme" in insert-phoneme picker (Android/iOS).
  std::function<void(const std::string& phonemeKey)> previewPhoneme;

  // Working state (managed by dialog)
  std::string currentLang;
  std::string currentType;  // "pronounce", "stress", "compound", "character"
  std::vector<DictEntryData> entries;
  std::vector<size_t> filteredIndices;  // indices into entries matching search
  bool modified = false;

  HINSTANCE hInst = nullptr;  // for sub-dialogs
};

struct DictEntryDialogState {
  DictEntryData entry;
  std::string dictType;  // controls which fields are visible
  std::function<bool(const std::wstring&, std::string&, std::string&)> convertToIpa;
  std::function<std::vector<PhonemeKeyInfo>()> getPhonemeKeys;
  std::function<void(const std::string& phonemeKey)> previewPhoneme;
  bool isEdit = false;    // true = editing existing, false = adding new
  bool ok = false;
};

bool ShowDictionaryEditorDialog(HINSTANCE hInst, HWND parent, DictionaryEditorState& st);

} // namespace tgsb_editor

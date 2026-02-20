/*
TGSpeechBox — Phoneme editor application controller interface.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#pragma once

#define UNICODE
#define _UNICODE

#include <windows.h>
#include <commctrl.h>

#include <string>
#include <vector>

#include "tgsb_runtime.h"
#include "yaml_edit.h"

class AppController {
public:
  bool Initialize(HINSTANCE hInstance, int nCmdShow);
  int RunMessageLoop();

  // Process instance handle (needed for dialogs/resources).
  HINSTANCE hInst = nullptr;

  // Main window handle.
  HWND wnd = nullptr;

  // Static labels (for screen-reader friendly names on controls).
  HWND lblFilter = nullptr;
  HWND lblAllPhonemes = nullptr;

  HWND lblLanguage = nullptr;
  HWND lblLangPhonemes = nullptr;
  HWND lblMappings = nullptr;
  HWND lblSkip = nullptr;

  HWND lblText = nullptr;
  HWND lblIpaOut = nullptr;

  HWND editFilter = nullptr;
  HWND listPhonemes = nullptr;
  HWND btnPlay = nullptr;
  HWND btnClone = nullptr;
  HWND btnEdit = nullptr;
  HWND btnAddToLang = nullptr;

  HWND comboLang = nullptr;
  HWND listLangPhonemes = nullptr;
  HWND listMappings = nullptr;
  HWND btnAddMap = nullptr;
  HWND btnEditMap = nullptr;
  HWND btnRemoveMap = nullptr;
  HWND listSkip = nullptr;
  HWND btnAddSkip = nullptr;
  HWND btnRemoveSkip = nullptr;

  HWND btnLangEdit = nullptr;
  HWND btnLangPlay = nullptr;
  HWND btnLangSettings = nullptr;

  HWND editText = nullptr;
  HWND chkInputIsIpa = nullptr;
  HWND btnConvertIpa = nullptr;
  HWND btnSpeak = nullptr;
  HWND btnSaveWav = nullptr;
  HWND editIpaOut = nullptr;

  HWND status = nullptr;

  std::wstring packRoot;
  std::wstring packsDir;
  std::wstring phonemesPath;
  std::wstring langDir;
  std::wstring espeakDir;
  std::wstring dllDir;

  std::vector<std::wstring> languageFiles; // full paths

  tgsb_editor::PhonemesYaml phonemes;
  tgsb_editor::LanguageYaml language;
  std::vector<tgsb_editor::ReplacementRule> repls;
  std::vector<tgsb_editor::SkipRule> skipRepls;
  std::vector<std::string> classNames;

  std::vector<std::string> phonemeKeys;
  std::vector<std::string> filteredPhonemeKeys;
  std::vector<std::u32string> phonemeKeysU32Sorted;

  std::vector<std::string> usedPhonemeKeys;

  tgsb_editor::TgsbRuntime runtime;

  // Track last focused child control for focus restoration on WM_ACTIVATE.
  HWND lastFocus = nullptr;

  // Track unsaved changes.
  bool phonemesDirty = false;
  bool languageDirty = false;

  void setStatus(const std::wstring& text) {
    if (status) SendMessageW(status, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(text.c_str()));
  }

private:
  HACCEL accel = nullptr;

  static LRESULT CALLBACK StaticWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
  LRESULT HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

  void layout(int w, int h);
};

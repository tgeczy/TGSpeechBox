/*
TGSpeechBox — Dictionary editor implementation.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#define UNICODE
#define _UNICODE

#include "DictionaryEditor.h"
#include "WinUtils.h"
#include "resource.h"

#include <commctrl.h>
#include <commdlg.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

namespace tgsb_editor {

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
static INT_PTR CALLBACK DictEditorDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
static INT_PTR CALLBACK DictEntryDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);

static void setupListColumns(HWND hList, const std::string& dictType);
static void loadEntries(HWND hDlg, DictionaryEditorState* st);
static void applyFilter(HWND hDlg, DictionaryEditorState* st);
static void updateStatusText(HWND hDlg, DictionaryEditorState* st);

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Case-insensitive ASCII tolower for prefix matching.
static char asciiLower(char c) {
  if (c >= 'A' && c <= 'Z') return static_cast<char>(c + ('a' - 'A'));
  return c;
}

// Case-insensitive prefix match.
static bool prefixMatchCI(const std::string& text, const std::string& prefix) {
  if (prefix.size() > text.size()) return false;
  for (size_t i = 0; i < prefix.size(); ++i) {
    if (asciiLower(text[i]) != asciiLower(prefix[i])) return false;
  }
  return true;
}

// Display name for dict type combo.
static const char* typeDisplayNames[] = {
  "Pronunciation", "Stress", "Compound", "Character", "User"
};
static const char* typeInternalNames[] = {
  "pronounce", "stress", "compound", "character", "user"
};
static constexpr int kNumTypes = 5;

// Get wstring from edit control.
static std::wstring getDlgItemText(HWND hDlg, int id) {
  int len = GetWindowTextLengthW(GetDlgItem(hDlg, id));
  if (len <= 0) return {};
  std::wstring buf(static_cast<size_t>(len + 1), L'\0');
  GetDlgItemTextW(hDlg, id, &buf[0], len + 1);
  buf.resize(static_cast<size_t>(len));
  return buf;
}

// ---------------------------------------------------------------------------
// List column setup
// ---------------------------------------------------------------------------
static void setupListColumns(HWND hList, const std::string& dictType) {
  // Remove all existing columns.
  while (true) {
    LVCOLUMNW col{};
    col.mask = LVCF_WIDTH;
    if (!ListView_GetColumn(hList, 0, &col)) break;
    ListView_DeleteColumn(hList, 0);
  }

  auto addCol = [&](const wchar_t* text, int width, int idx) {
    LVCOLUMNW col{};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    col.fmt = LVCFMT_LEFT;
    col.cx = width;
    col.pszText = const_cast<wchar_t*>(text);
    ListView_InsertColumn(hList, idx, &col);
  };

  if (dictType == "pronounce" || dictType == "user") {
    addCol(L"From",     120, 0);
    addCol(L"To",       120, 1);
    addCol(L"From IPA", 100, 2);
    addCol(L"To IPA",   100, 3);
    addCol(L"Category",  80, 4);
  } else if (dictType == "stress") {
    addCol(L"Word",    150, 0);
    addCol(L"Pattern", 300, 1);
  } else if (dictType == "compound") {
    addCol(L"Word",  150, 0);
    addCol(L"Parts", 300, 1);
  } else if (dictType == "character") {
    addCol(L"Symbol",      100, 0);
    addCol(L"Description", 350, 1);
  }
}

// ---------------------------------------------------------------------------
// Load entries from TSV
// ---------------------------------------------------------------------------
static void loadEntries(HWND hDlg, DictionaryEditorState* st) {
  st->entries.clear();
  st->filteredIndices.clear();
  st->modified = false;

  // The file this language reads the type from; every save goes back to it.
  st->currentPath = dictTsvPathToEdit(st->packDir, st->currentLang, st->currentType);
  const std::wstring& path = st->currentPath;

  if (fs::exists(path)) {
    std::string err;
    if (!loadDictTsv(path, st->currentType, st->entries, err)) {
      msgBox(hDlg, utf8ToWide(err), L"Dictionary Editor", MB_ICONERROR);
    }
  }

  // Setup list columns for current type.
  HWND hList = GetDlgItem(hDlg, IDC_DICT_LIST);
  setupListColumns(hList, st->currentType);

  applyFilter(hDlg, st);
}

// ---------------------------------------------------------------------------
// Apply filter and populate listview
// ---------------------------------------------------------------------------
static void applyFilter(HWND hDlg, DictionaryEditorState* st) {
  HWND hList = GetDlgItem(hDlg, IDC_DICT_LIST);
  std::wstring searchW = getDlgItemText(hDlg, IDC_DICT_SEARCH);
  std::string search = wideToUtf8(searchW);

  st->filteredIndices.clear();

  static constexpr size_t kMaxDisplay = 5000;

  if (search.empty()) {
    // Show all, but cap at kMaxDisplay when no search is active.
    size_t limit = (st->entries.size() > kMaxDisplay) ? kMaxDisplay : st->entries.size();
    for (size_t i = 0; i < limit; ++i) {
      st->filteredIndices.push_back(i);
    }
  } else {
    // Case-insensitive prefix match on fromText.
    for (size_t i = 0; i < st->entries.size(); ++i) {
      if (prefixMatchCI(st->entries[i].fromText, search)) {
        st->filteredIndices.push_back(i);
      }
    }
  }

  // Populate listview.
  SendMessageW(hList, WM_SETREDRAW, FALSE, 0);
  ListView_DeleteAllItems(hList);

  for (size_t fi = 0; fi < st->filteredIndices.size(); ++fi) {
    const auto& e = st->entries[st->filteredIndices[fi]];

    LVITEMW lvi{};
    lvi.mask = LVIF_TEXT;
    lvi.iItem = static_cast<int>(fi);
    lvi.iSubItem = 0;
    std::wstring col0 = utf8ToWide(e.fromText);
    lvi.pszText = const_cast<wchar_t*>(col0.c_str());
    ListView_InsertItem(hList, &lvi);

    // Set sub-items based on dict type.
    if (st->currentType == "pronounce" || st->currentType == "user") {
      std::wstring s1 = utf8ToWide(e.toText);
      ListView_SetItemText(hList, static_cast<int>(fi), 1, const_cast<wchar_t*>(s1.c_str()));
      std::wstring s2 = utf8ToWide(e.fromIpa);
      ListView_SetItemText(hList, static_cast<int>(fi), 2, const_cast<wchar_t*>(s2.c_str()));
      std::wstring s3 = utf8ToWide(e.toIpa);
      ListView_SetItemText(hList, static_cast<int>(fi), 3, const_cast<wchar_t*>(s3.c_str()));
      std::wstring s4 = utf8ToWide(e.category);
      ListView_SetItemText(hList, static_cast<int>(fi), 4, const_cast<wchar_t*>(s4.c_str()));
    } else {
      // stress, compound, character: 2 columns
      std::wstring s1 = utf8ToWide(e.toText);
      ListView_SetItemText(hList, static_cast<int>(fi), 1, const_cast<wchar_t*>(s1.c_str()));
    }
  }

  SendMessageW(hList, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(hList, nullptr, TRUE);

  updateStatusText(hDlg, st);
}

// ---------------------------------------------------------------------------
// Status text
// ---------------------------------------------------------------------------
static void updateStatusText(HWND hDlg, DictionaryEditorState* st) {
  // Build status string in the dialog title.
  std::wstring title = L"Dictionary Editor";
  if (!st->currentLang.empty()) {
    title += L" - " + utf8ToWide(st->currentLang);
  }
  // Name the file: a regional language may be editing the one its whole
  // language shares (en-us and en-compounds.tsv, #216).
  if (!st->currentPath.empty()) {
    title += L" - " + fs::path(st->currentPath).filename().wstring();
  }

  size_t total = st->entries.size();
  size_t shown = st->filteredIndices.size();

  if (shown < total) {
    title += L" (showing " + std::to_wstring(shown) + L" of " + std::to_wstring(total) + L")";
  } else {
    title += L" (" + std::to_wstring(total) + L" entries)";
  }

  if (st->modified) {
    title += L" *";
  }

  SetWindowTextW(hDlg, title.c_str());
}

// ---------------------------------------------------------------------------
// Prompt to save if modified
// ---------------------------------------------------------------------------
static bool promptSaveIfModified(HWND hDlg, DictionaryEditorState* st) {
  if (!st->modified) return true;

  int ret = MessageBoxW(hDlg, L"Save changes to current dictionary?",
                        L"Dictionary Editor", MB_YESNOCANCEL | MB_ICONQUESTION);
  if (ret == IDCANCEL) return false;
  if (ret == IDYES) {
    const std::wstring& path = st->currentPath;
    std::string err;
    if (!saveDictTsv(path, st->currentType, st->entries, err)) {
      msgBox(hDlg, utf8ToWide(err), L"Dictionary Editor", MB_ICONERROR);
      return false;
    }
    st->modified = false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Get selected listview index
// ---------------------------------------------------------------------------
static int getSelectedListIndex(HWND hList) {
  return ListView_GetNextItem(hList, -1, LVNI_SELECTED);
}

// ---------------------------------------------------------------------------
// Open/Save file dialog helpers
// ---------------------------------------------------------------------------
static bool openTsvFileDialog(HWND hDlg, std::wstring& outPath) {
  wchar_t buf[MAX_PATH] = {};
  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = hDlg;
  ofn.lpstrFilter = L"TSV Files (*.tsv)\0*.tsv\0All Files (*.*)\0*.*\0";
  ofn.lpstrFile = buf;
  ofn.nMaxFile = MAX_PATH;
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
  ofn.lpstrTitle = L"Import Dictionary TSV";
  if (GetOpenFileNameW(&ofn)) {
    outPath = buf;
    return true;
  }
  return false;
}

static bool saveTsvFileDialog(HWND hDlg, std::wstring& outPath) {
  wchar_t buf[MAX_PATH] = {};
  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = hDlg;
  ofn.lpstrFilter = L"TSV Files (*.tsv)\0*.tsv\0All Files (*.*)\0*.*\0";
  ofn.lpstrFile = buf;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrDefExt = L"tsv";
  ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
  ofn.lpstrTitle = L"Export Dictionary TSV";
  if (GetSaveFileNameW(&ofn)) {
    outPath = buf;
    return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Dict entry sub-dialog
// ---------------------------------------------------------------------------
static INT_PTR CALLBACK DictEntryDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
  auto* st = reinterpret_cast<DictEntryDialogState*>(GetWindowLongPtrW(hDlg, DWLP_USER));

  switch (msg) {
    case WM_INITDIALOG: {
      st = reinterpret_cast<DictEntryDialogState*>(lParam);
      SetWindowLongPtrW(hDlg, DWLP_USER, reinterpret_cast<LONG_PTR>(st));

      // Set caption.
      SetWindowTextW(hDlg, st->isEdit ? L"Edit Dictionary Entry" : L"Add Dictionary Entry");

      // Adjust visibility and labels based on dict type.
      bool isPronounce = (st->dictType == "pronounce" || st->dictType == "user");

      // Hide IPA buttons and fields for non-pronounce types.
      auto showCtrl = [&](int id, bool show) {
        ShowWindow(GetDlgItem(hDlg, id), show ? SW_SHOW : SW_HIDE);
        EnableWindow(GetDlgItem(hDlg, id), show ? TRUE : FALSE);
      };

      showCtrl(IDC_DENTRY_FROM_IPA_BTN, isPronounce);
      showCtrl(IDC_DENTRY_FROM_IPA_PICK, isPronounce);
      showCtrl(IDC_DENTRY_FROM_IPA, isPronounce);
      showCtrl(IDC_DENTRY_LBL_FROM_IPA, isPronounce);
      showCtrl(IDC_DENTRY_TO_IPA_BTN, isPronounce);
      showCtrl(IDC_DENTRY_TO_IPA_PICK, isPronounce);
      showCtrl(IDC_DENTRY_TO_IPA, isPronounce);
      showCtrl(IDC_DENTRY_LBL_TO_IPA, isPronounce);
      showCtrl(IDC_DENTRY_CATEGORY, isPronounce);
      showCtrl(IDC_DENTRY_LBL_CATEGORY, isPronounce);

      // Disable IPA buttons if callbacks are null.
      if (!st->convertToIpa) {
        EnableWindow(GetDlgItem(hDlg, IDC_DENTRY_FROM_IPA_BTN), FALSE);
        EnableWindow(GetDlgItem(hDlg, IDC_DENTRY_TO_IPA_BTN), FALSE);
      }
      if (!st->getPhonemeKeys) {
        EnableWindow(GetDlgItem(hDlg, IDC_DENTRY_FROM_IPA_PICK), FALSE);
        EnableWindow(GetDlgItem(hDlg, IDC_DENTRY_TO_IPA_PICK), FALSE);
      }

      // Relabel for specific types.
      if (st->dictType == "stress") {
        SetDlgItemTextW(hDlg, IDC_DENTRY_LBL_FROM, L"Word:");
        SetDlgItemTextW(hDlg, IDC_DENTRY_LBL_TO, L"Pattern:");
      } else if (st->dictType == "compound") {
        SetDlgItemTextW(hDlg, IDC_DENTRY_LBL_FROM, L"Word:");
        SetDlgItemTextW(hDlg, IDC_DENTRY_LBL_TO, L"Parts:");
      } else if (st->dictType == "character") {
        SetDlgItemTextW(hDlg, IDC_DENTRY_LBL_FROM, L"Symbol:");
        SetDlgItemTextW(hDlg, IDC_DENTRY_LBL_TO, L"Description:");
      }

      // Pre-fill fields if editing.
      if (st->isEdit) {
        SetDlgItemTextW(hDlg, IDC_DENTRY_FROM, utf8ToWide(st->entry.fromText).c_str());
        SetDlgItemTextW(hDlg, IDC_DENTRY_TO, utf8ToWide(st->entry.toText).c_str());
        if (isPronounce) {
          SetDlgItemTextW(hDlg, IDC_DENTRY_FROM_IPA, utf8ToWide(st->entry.fromIpa).c_str());
          SetDlgItemTextW(hDlg, IDC_DENTRY_TO_IPA, utf8ToWide(st->entry.toIpa).c_str());
          SetDlgItemTextW(hDlg, IDC_DENTRY_CATEGORY, utf8ToWide(st->entry.category).c_str());
        }
      }

      // Set focus to From field.
      SetFocus(GetDlgItem(hDlg, IDC_DENTRY_FROM));
      return FALSE;  // We set focus manually.
    }

    case WM_COMMAND: {
      int id = LOWORD(wParam);

      if (id == IDC_DENTRY_FROM_IPA_BTN) {
        if (st->convertToIpa) {
          std::wstring text = getDlgItemText(hDlg, IDC_DENTRY_FROM);
          std::string ipa, err;
          if (st->convertToIpa(text, ipa, err)) {
            st->entry.fromIpa = ipa;
            SetDlgItemTextW(hDlg, IDC_DENTRY_FROM_IPA, utf8ToWide(ipa).c_str());
          } else {
            msgBox(hDlg, L"IPA conversion failed:\n" + utf8ToWide(err),
                   L"Dictionary Editor", MB_ICONERROR);
          }
        }
        return TRUE;
      }

      if (id == IDC_DENTRY_TO_IPA_BTN) {
        if (st->convertToIpa) {
          std::wstring text = getDlgItemText(hDlg, IDC_DENTRY_TO);
          std::string ipa, err;
          if (st->convertToIpa(text, ipa, err)) {
            st->entry.toIpa = ipa;
            SetDlgItemTextW(hDlg, IDC_DENTRY_TO_IPA, utf8ToWide(ipa).c_str());
          } else {
            msgBox(hDlg, L"IPA conversion failed:\n" + utf8ToWide(err),
                   L"Dictionary Editor", MB_ICONERROR);
          }
        }
        return TRUE;
      }

      // Insert phoneme picker for From IPA or To IPA.
      if (id == IDC_DENTRY_FROM_IPA_PICK || id == IDC_DENTRY_TO_IPA_PICK) {
        if (st->getPhonemeKeys) {
          auto keys = st->getPhonemeKeys();
          if (keys.empty()) {
            msgBox(hDlg, L"No phonemes available for the current language.",
                   L"Insert Phoneme", MB_ICONINFORMATION);
          } else {
            // Build a list of display strings for the picker.
            std::vector<std::wstring> items;
            items.reserve(keys.size());
            for (const auto& pk : keys) {
              items.push_back(utf8ToWide(pk.key + " (" + pk.cls + ")"));
            }
            // Show a simple list picker using a message-loop-driven dialog.
            // We create it dynamically with a LISTBOX.
            int sel = -1;
            struct PickCtx {
              std::vector<std::wstring>* items;
              int* sel;
              std::vector<PhonemeKeyInfo>* keys;
              std::function<void(const std::string&)>* previewPhoneme;
            };
            PickCtx ctx = { &items, &sel, &keys, &st->previewPhoneme };
            DialogBoxIndirectParamW(
              st->convertToIpa ? GetModuleHandleW(nullptr) : nullptr,
              // Build dialog template in memory.
              [&]() -> DLGTEMPLATE* {
                // In-memory dialog template: list box + OK + Cancel.
                static BYTE buf[4096];
                memset(buf, 0, sizeof(buf));
                auto* dlg = reinterpret_cast<DLGTEMPLATE*>(buf);
                dlg->style = DS_SETFONT | DS_MODALFRAME | WS_POPUP | WS_CAPTION | WS_SYSMENU;
                dlg->cdit = 0;
                dlg->cx = 180; dlg->cy = 200;
                // We won't use a template — just create controls in WM_INITDIALOG.
                return dlg;
              }(),
              hDlg,
              [](HWND hPick, UINT msg, WPARAM wp, LPARAM lp) -> INT_PTR {
                auto* pc = reinterpret_cast<PickCtx*>(GetWindowLongPtrW(hPick, GWLP_USERDATA));
                switch (msg) {
                  case WM_INITDIALOG: {
                    pc = reinterpret_cast<PickCtx*>(lp);
                    SetWindowLongPtrW(hPick, GWLP_USERDATA, (LONG_PTR)pc);
                    SetWindowTextW(hPick, L"Insert Phoneme");
                    // Create listbox.
                    HWND lb = CreateWindowExW(0, L"LISTBOX", nullptr,
                        WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY,
                        7, 7, 260, 250, hPick, (HMENU)4000, nullptr, nullptr);
                    SendMessageW(lb, WM_SETFONT,
                        (WPARAM)SendMessageW(hPick, WM_GETFONT, 0, 0), TRUE);
                    for (const auto& s : *pc->items)
                      SendMessageW(lb, LB_ADDSTRING, 0, (LPARAM)s.c_str());
                    // Auto-select first phoneme so Tab→Preview works immediately.
                    if (!pc->items->empty())
                      SendMessageW(lb, LB_SETCURSEL, 0, 0);
                    // Preview + OK + Cancel buttons.
                    auto makeBtn = [&](const wchar_t* text, int id, int x, int w = 60) {
                      HWND b = CreateWindowExW(0, L"BUTTON", text,
                          WS_CHILD | WS_VISIBLE | WS_TABSTOP | (id == IDOK ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON),
                          x, 262, w, 22, hPick, (HMENU)(intptr_t)id, nullptr, nullptr);
                      SendMessageW(b, WM_SETFONT,
                          (WPARAM)SendMessageW(hPick, WM_GETFONT, 0, 0), TRUE);
                    };
                    // 4001 = Preview button (Alt+P)
                    if (pc->previewPhoneme && *pc->previewPhoneme)
                      makeBtn(L"&Preview", 4001, 7, 65);
                    makeBtn(L"OK", IDOK, 144);
                    makeBtn(L"Cancel", IDCANCEL, 210);
                    // Resize dialog to fit.
                    RECT r = {0, 0, 280, 295};
                    AdjustWindowRectEx(&r, GetWindowLongW(hPick, GWL_STYLE), FALSE,
                                       GetWindowLongW(hPick, GWL_EXSTYLE));
                    SetWindowPos(hPick, nullptr, 0, 0, r.right - r.left, r.bottom - r.top,
                                 SWP_NOMOVE | SWP_NOZORDER);
                    // Center on parent.
                    RECT pr; GetWindowRect(GetParent(hPick), &pr);
                    int cx = (pr.left + pr.right) / 2 - (r.right - r.left) / 2;
                    int cy = (pr.top + pr.bottom) / 2 - (r.bottom - r.top) / 2;
                    SetWindowPos(hPick, nullptr, cx, cy, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
                    SetFocus(lb);
                    return FALSE;
                  }
                  case WM_COMMAND:
                    if (LOWORD(wp) == IDOK || (LOWORD(wp) == 4000 && HIWORD(wp) == LBN_DBLCLK)) {
                      HWND lb = GetDlgItem(hPick, 4000);
                      int idx = (int)SendMessageW(lb, LB_GETCURSEL, 0, 0);
                      if (idx >= 0) { *pc->sel = idx; EndDialog(hPick, IDOK); }
                      return TRUE;
                    }
                    if (LOWORD(wp) == IDCANCEL) { EndDialog(hPick, IDCANCEL); return TRUE; }
                    // Preview button (Alt+P).
                    if (LOWORD(wp) == 4001) {
                      if (pc->previewPhoneme && *pc->previewPhoneme) {
                        HWND lb = GetDlgItem(hPick, 4000);
                        int idx = (int)SendMessageW(lb, LB_GETCURSEL, 0, 0);
                        if (idx >= 0 && idx < (int)pc->keys->size())
                          (*pc->previewPhoneme)((*pc->keys)[idx].key);
                      }
                      return TRUE;
                    }
                    break;
                }
                return FALSE;
              },
              (LPARAM)&ctx
            );
            if (sel >= 0 && sel < (int)keys.size()) {
              int ipaCtrl = (id == IDC_DENTRY_FROM_IPA_PICK)
                  ? IDC_DENTRY_FROM_IPA : IDC_DENTRY_TO_IPA;
              std::string current = wideToUtf8(getDlgItemText(hDlg, ipaCtrl));
              if (!current.empty()) current += ' ';
              current += keys[sel].key;
              SetDlgItemTextW(hDlg, ipaCtrl, utf8ToWide(current).c_str());
            }
          }
        }
        return TRUE;
      }

      if (id == IDOK) {
        // Read all fields.
        st->entry.fromText = wideToUtf8(getDlgItemText(hDlg, IDC_DENTRY_FROM));
        st->entry.toText = wideToUtf8(getDlgItemText(hDlg, IDC_DENTRY_TO));

        if (st->dictType == "pronounce" || st->dictType == "user") {
          st->entry.fromIpa = wideToUtf8(getDlgItemText(hDlg, IDC_DENTRY_FROM_IPA));
          st->entry.toIpa = wideToUtf8(getDlgItemText(hDlg, IDC_DENTRY_TO_IPA));
          st->entry.category = wideToUtf8(getDlgItemText(hDlg, IDC_DENTRY_CATEGORY));
        }

        // Validate.
        if (st->entry.fromText.empty()) {
          msgBox(hDlg, L"The 'From' field cannot be empty.", L"Dictionary Editor", MB_ICONWARNING);
          SetFocus(GetDlgItem(hDlg, IDC_DENTRY_FROM));
          return TRUE;
        }
        if (st->entry.toText.empty()) {
          msgBox(hDlg, L"The 'To' field cannot be empty.", L"Dictionary Editor", MB_ICONWARNING);
          SetFocus(GetDlgItem(hDlg, IDC_DENTRY_TO));
          return TRUE;
        }

        st->ok = true;
        EndDialog(hDlg, IDOK);
        return TRUE;
      }

      if (id == IDCANCEL) {
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
      }
      break;
    }

    case WM_CLOSE:
      EndDialog(hDlg, IDCANCEL);
      return TRUE;
  }

  return FALSE;
}

// ---------------------------------------------------------------------------
// Main dictionary editor dialog
// ---------------------------------------------------------------------------
static INT_PTR CALLBACK DictEditorDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
  auto* st = reinterpret_cast<DictionaryEditorState*>(GetWindowLongPtrW(hDlg, DWLP_USER));

  switch (msg) {
    case WM_INITDIALOG: {
      st = reinterpret_cast<DictionaryEditorState*>(lParam);
      SetWindowLongPtrW(hDlg, DWLP_USER, reinterpret_cast<LONG_PTR>(st));

      // Populate language combo.
      HWND hLang = GetDlgItem(hDlg, IDC_DICT_LANG);
      int selIdx = 0;
      for (size_t i = 0; i < st->availableLangs.size(); ++i) {
        std::wstring w = utf8ToWide(st->availableLangs[i]);
        SendMessageW(hLang, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(w.c_str()));
        if (st->availableLangs[i] == st->initialLang) {
          selIdx = static_cast<int>(i);
        }
      }
      SendMessageW(hLang, CB_SETCURSEL, static_cast<WPARAM>(selIdx), 0);

      // Populate type combo.
      HWND hType = GetDlgItem(hDlg, IDC_DICT_TYPE);
      for (int i = 0; i < kNumTypes; ++i) {
        std::wstring w = utf8ToWide(typeDisplayNames[i]);
        SendMessageW(hType, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(w.c_str()));
      }
      SendMessageW(hType, CB_SETCURSEL, 0, 0);

      // Set initial state.
      if (!st->availableLangs.empty()) {
        st->currentLang = st->availableLangs[static_cast<size_t>(selIdx)];
      }
      st->currentType = "pronounce";

      // Init listview extended style.
      HWND hList = GetDlgItem(hDlg, IDC_DICT_LIST);
      ListView_SetExtendedListViewStyle(hList,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

      // Load entries.
      loadEntries(hDlg, st);

      return TRUE;
    }

    case WM_COMMAND: {
      int id = LOWORD(wParam);
      int code = HIWORD(wParam);

      // Language combo changed.
      if (id == IDC_DICT_LANG && code == CBN_SELCHANGE) {
        int sel = static_cast<int>(SendMessageW(GetDlgItem(hDlg, IDC_DICT_LANG), CB_GETCURSEL, 0, 0));
        if (sel >= 0 && sel < static_cast<int>(st->availableLangs.size())) {
          if (!promptSaveIfModified(hDlg, st)) {
            // Revert combo selection.
            for (size_t i = 0; i < st->availableLangs.size(); ++i) {
              if (st->availableLangs[i] == st->currentLang) {
                SendMessageW(GetDlgItem(hDlg, IDC_DICT_LANG), CB_SETCURSEL, static_cast<WPARAM>(i), 0);
                break;
              }
            }
            return TRUE;
          }
          st->currentLang = st->availableLangs[static_cast<size_t>(sel)];
          loadEntries(hDlg, st);
        }
        return TRUE;
      }

      // Type combo changed.
      if (id == IDC_DICT_TYPE && code == CBN_SELCHANGE) {
        int sel = static_cast<int>(SendMessageW(GetDlgItem(hDlg, IDC_DICT_TYPE), CB_GETCURSEL, 0, 0));
        if (sel >= 0 && sel < kNumTypes) {
          if (!promptSaveIfModified(hDlg, st)) {
            // Revert combo selection.
            for (int i = 0; i < kNumTypes; ++i) {
              if (std::string(typeInternalNames[i]) == st->currentType) {
                SendMessageW(GetDlgItem(hDlg, IDC_DICT_TYPE), CB_SETCURSEL, static_cast<WPARAM>(i), 0);
                break;
              }
            }
            return TRUE;
          }
          st->currentType = typeInternalNames[sel];
          loadEntries(hDlg, st);
        }
        return TRUE;
      }

      // Search text changed.
      if (id == IDC_DICT_SEARCH && code == EN_CHANGE) {
        applyFilter(hDlg, st);
        return TRUE;
      }

      // Add button.
      if (id == IDC_DICT_ADD) {
        DictEntryDialogState des;
        des.dictType = st->currentType;
        des.convertToIpa = st->convertToIpa;
        des.getPhonemeKeys = st->getPhonemeKeys;
        des.previewPhoneme = st->previewPhoneme;
        des.isEdit = false;

        DialogBoxParamW(st->hInst, MAKEINTRESOURCEW(IDD_DICT_ENTRY), hDlg,
                        DictEntryDlgProc, reinterpret_cast<LPARAM>(&des));

        if (des.ok) {
          // A word has one row: a second one would never be used, or would
          // leave the first one dead (#221).  The list may not be showing
          // the row that is already there, so ask.
          if (findDictEntry(st->entries, des.entry.fromText) != kNoDictEntry) {
            const std::wstring q = L"\"" + utf8ToWide(des.entry.fromText) +
                L"\" is already in this dictionary. Replace its entry with this one?";
            if (MessageBoxW(hDlg, q.c_str(), L"Dictionary Editor", MB_YESNO | MB_ICONQUESTION) != IDYES) {
              return TRUE;
            }
          }
          putDictEntry(st->entries, des.entry);
          st->modified = true;
          applyFilter(hDlg, st);
        }
        return TRUE;
      }

      // Edit button.
      if (id == IDC_DICT_EDIT) {
        HWND hList = GetDlgItem(hDlg, IDC_DICT_LIST);
        int sel = getSelectedListIndex(hList);
        if (sel < 0 || sel >= static_cast<int>(st->filteredIndices.size())) {
          msgBox(hDlg, L"Select an entry to edit.", L"Dictionary Editor", MB_ICONINFORMATION);
          return TRUE;
        }

        size_t realIdx = st->filteredIndices[static_cast<size_t>(sel)];
        DictEntryDialogState des;
        des.dictType = st->currentType;
        des.convertToIpa = st->convertToIpa;
        des.getPhonemeKeys = st->getPhonemeKeys;
        des.previewPhoneme = st->previewPhoneme;
        des.isEdit = true;
        des.entry = st->entries[realIdx];

        DialogBoxParamW(st->hInst, MAKEINTRESOURCEW(IDD_DICT_ENTRY), hDlg,
                        DictEntryDlgProc, reinterpret_cast<LPARAM>(&des));

        if (des.ok) {
          st->entries[realIdx] = des.entry;
          st->modified = true;
          applyFilter(hDlg, st);
        }
        return TRUE;
      }

      // Delete button.
      if (id == IDC_DICT_DELETE) {
        HWND hList = GetDlgItem(hDlg, IDC_DICT_LIST);
        int sel = getSelectedListIndex(hList);
        if (sel < 0 || sel >= static_cast<int>(st->filteredIndices.size())) {
          msgBox(hDlg, L"Select an entry to delete.", L"Dictionary Editor", MB_ICONINFORMATION);
          return TRUE;
        }

        size_t realIdx = st->filteredIndices[static_cast<size_t>(sel)];
        std::wstring fromW = utf8ToWide(st->entries[realIdx].fromText);

        int ret = MessageBoxW(hDlg,
          (L"Delete entry \"" + fromW + L"\"?").c_str(),
          L"Dictionary Editor", MB_YESNO | MB_ICONQUESTION);

        if (ret == IDYES) {
          st->entries.erase(st->entries.begin() + static_cast<ptrdiff_t>(realIdx));
          st->modified = true;
          applyFilter(hDlg, st);
        }
        return TRUE;
      }

      // Save button.
      if (id == IDC_DICT_SAVE) {
        const std::wstring& path = st->currentPath;
        std::string err;
        if (saveDictTsv(path, st->currentType, st->entries, err)) {
          st->modified = false;
          updateStatusText(hDlg, st);
          msgBox(hDlg, L"Saved " + fs::path(path).filename().wstring() + L".", L"Dictionary Editor", MB_ICONINFORMATION);
        } else {
          msgBox(hDlg, utf8ToWide(err), L"Dictionary Editor", MB_ICONERROR);
        }
        return TRUE;
      }

      // Import button.
      if (id == IDC_DICT_IMPORT) {
        std::wstring importPath;
        if (openTsvFileDialog(hDlg, importPath)) {
          std::vector<DictEntryData> imported;
          std::string err;
          if (!loadDictTsv(importPath, st->currentType, imported, err)) {
            msgBox(hDlg, utf8ToWide(err), L"Dictionary Editor", MB_ICONERROR);
            return TRUE;
          }

          // Merge: append entries whose fromText is not already present.
          std::unordered_set<std::string> existing;
          for (const auto& e : st->entries) {
            existing.insert(e.fromText);
          }

          int added = 0;
          for (auto& e : imported) {
            if (existing.find(e.fromText) == existing.end()) {
              existing.insert(e.fromText);
              st->entries.push_back(std::move(e));
              ++added;
            }
          }

          if (added > 0) {
            st->modified = true;
            applyFilter(hDlg, st);
          }

          msgBox(hDlg,
            L"Imported " + std::to_wstring(added) + L" new entries (" +
            std::to_wstring(imported.size() - static_cast<size_t>(added)) + L" duplicates skipped).",
            L"Dictionary Editor", MB_ICONINFORMATION);
        }
        return TRUE;
      }

      // Export button.
      if (id == IDC_DICT_EXPORT) {
        std::wstring exportPath;
        if (saveTsvFileDialog(hDlg, exportPath)) {
          std::string err;
          if (saveDictTsv(exportPath, st->currentType, st->entries, err)) {
            msgBox(hDlg, L"Exported " + std::to_wstring(st->entries.size()) + L" entries.",
                   L"Dictionary Editor", MB_ICONINFORMATION);
          } else {
            msgBox(hDlg, utf8ToWide(err), L"Dictionary Editor", MB_ICONERROR);
          }
        }
        return TRUE;
      }

      // Close / Cancel.
      if (id == IDCANCEL) {
        if (st->modified) {
          int ret = MessageBoxW(hDlg, L"Save changes before closing?",
                                L"Dictionary Editor", MB_YESNOCANCEL | MB_ICONQUESTION);
          if (ret == IDCANCEL) return TRUE;
          if (ret == IDYES) {
            const std::wstring& path = st->currentPath;
            std::string err;
            if (!saveDictTsv(path, st->currentType, st->entries, err)) {
              msgBox(hDlg, utf8ToWide(err), L"Dictionary Editor", MB_ICONERROR);
              return TRUE;
            }
            st->modified = false;
          }
        }
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
      }

      break;
    }

    case WM_NOTIFY: {
      NMHDR* nmh = reinterpret_cast<NMHDR*>(lParam);
      if (nmh->idFrom == IDC_DICT_LIST && nmh->code == NM_DBLCLK) {
        // Double-click on listview: same as Edit.
        SendMessageW(hDlg, WM_COMMAND, MAKEWPARAM(IDC_DICT_EDIT, BN_CLICKED), 0);
        return TRUE;
      }
      break;
    }

    case WM_CLOSE:
      // Treat as cancel.
      SendMessageW(hDlg, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0);
      return TRUE;
  }

  return FALSE;
}

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------
bool ShowDictionaryEditorDialog(HINSTANCE hInst, HWND parent, DictionaryEditorState& st) {
  st.hInst = hInst;
  DialogBoxParamW(hInst, MAKEINTRESOURCEW(IDD_DICT_EDITOR), parent,
                  DictEditorDlgProc, reinterpret_cast<LPARAM>(&st));
  return st.modified;
}

} // namespace tgsb_editor

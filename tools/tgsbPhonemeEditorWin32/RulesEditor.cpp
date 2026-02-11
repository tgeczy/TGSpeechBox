#include "RulesEditor.h"
#include "resource.h"
#include "WinUtils.h"

#include <commctrl.h>
#include <sstream>
#include <algorithm>
#include <cstdlib>

namespace tgsb_editor {

// =====================================================================
// Helpers
// =====================================================================

static std::string joinStrVec(const std::vector<std::string>& vec, const char* sep = ", ") {
  std::string out;
  for (size_t i = 0; i < vec.size(); ++i) {
    if (i > 0) out += sep;
    out += vec[i];
  }
  return out;
}

static std::vector<std::string> splitCommaSeparated(const std::string& str) {
  std::vector<std::string> out;
  std::istringstream iss(str);
  std::string token;
  while (std::getline(iss, token, ',')) {
    // Trim whitespace
    size_t s = token.find_first_not_of(" \t");
    size_t e = token.find_last_not_of(" \t");
    if (s != std::string::npos && e != std::string::npos)
      out.push_back(token.substr(s, e - s + 1));
    else if (!token.empty()) {
      std::string t = token;
      while (!t.empty() && (t.front() == ' ' || t.front() == '\t')) t.erase(t.begin());
      while (!t.empty() && (t.back() == ' ' || t.back() == '\t')) t.pop_back();
      if (!t.empty()) out.push_back(t);
    }
  }
  return out;
}

static std::string fieldScalesToText(const std::vector<std::pair<std::string, double>>& scales) {
  std::string out;
  for (const auto& kv : scales) {
    if (!out.empty()) out += "\r\n";
    std::ostringstream os;
    os << kv.first << ": " << kv.second;
    out += os.str();
  }
  return out;
}

static std::vector<std::pair<std::string, double>> textToFieldScales(const std::string& text) {
  std::vector<std::pair<std::string, double>> out;
  std::istringstream iss(text);
  std::string line;
  while (std::getline(iss, line)) {
    // Trim \r
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
    auto colon = line.find(':');
    if (colon == std::string::npos) continue;
    std::string field = line.substr(0, colon);
    std::string val = line.substr(colon + 1);
    // Trim
    while (!field.empty() && field.back() == ' ') field.pop_back();
    while (!val.empty() && val.front() == ' ') val.erase(val.begin());
    if (field.empty() || val.empty()) continue;
    try { out.emplace_back(field, std::stod(val)); } catch (...) {}
  }
  return out;
}

static std::string fieldShiftsToText(const std::vector<AllophoneRuleEntry::ShiftEntry>& shifts) {
  std::string out;
  for (const auto& se : shifts) {
    if (!out.empty()) out += "\r\n";
    std::ostringstream os;
    os << se.field << ": ";
    if (se.targetHz != 0.0) {
      os << "target=" << se.targetHz;
      if (se.blend != 1.0) os << ", blend=" << se.blend;
    } else {
      os << "delta=" << se.deltaHz;
    }
    out += os.str();
  }
  return out;
}

static std::vector<AllophoneRuleEntry::ShiftEntry> textToFieldShifts(const std::string& text) {
  std::vector<AllophoneRuleEntry::ShiftEntry> out;
  std::istringstream iss(text);
  std::string line;
  while (std::getline(iss, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
    auto colon = line.find(':');
    if (colon == std::string::npos) continue;
    AllophoneRuleEntry::ShiftEntry se;
    se.field = line.substr(0, colon);
    while (!se.field.empty() && se.field.back() == ' ') se.field.pop_back();
    std::string rest = line.substr(colon + 1);
    // Parse key=value pairs
    if (rest.find("target=") != std::string::npos) {
      auto pos = rest.find("target=");
      auto end = rest.find(',', pos);
      std::string val = (end != std::string::npos) ? rest.substr(pos + 7, end - pos - 7) : rest.substr(pos + 7);
      try { se.targetHz = std::stod(val); } catch (...) {}
      auto bpos = rest.find("blend=");
      if (bpos != std::string::npos) {
        try { se.blend = std::stod(rest.substr(bpos + 6)); } catch (...) {}
      }
    } else if (rest.find("delta=") != std::string::npos) {
      auto pos = rest.find("delta=");
      try { se.deltaHz = std::stod(rest.substr(pos + 6)); } catch (...) {}
    }
    if (!se.field.empty()) out.push_back(std::move(se));
  }
  return out;
}

static std::string getDlgItemUtf8(HWND hDlg, int id) {
  wchar_t buf[2048] = {};
  GetDlgItemTextW(hDlg, id, buf, 2048);
  return wideToUtf8(buf);
}

static void setDlgItemUtf8(HWND hDlg, int id, const std::string& s) {
  SetDlgItemTextW(hDlg, id, utf8ToWide(s).c_str());
}

static std::string getDlgItemMultiline(HWND hDlg, int id) {
  HWND ctrl = GetDlgItem(hDlg, id);
  int len = GetWindowTextLengthW(ctrl);
  if (len <= 0) return {};
  std::wstring buf(len + 1, L'\0');
  GetWindowTextW(ctrl, &buf[0], len + 1);
  buf.resize(len);
  return wideToUtf8(buf);
}

static int getComboSel(HWND hDlg, int id) {
  return (int)SendDlgItemMessageW(hDlg, id, CB_GETCURSEL, 0, 0);
}

static std::string getComboSelText(HWND hDlg, int id) {
  int sel = getComboSel(hDlg, id);
  if (sel < 0) return {};
  wchar_t buf[256] = {};
  SendDlgItemMessageW(hDlg, id, CB_GETLBTEXT, sel, (LPARAM)buf);
  return wideToUtf8(buf);
}

static void addComboStrings(HWND hDlg, int id, const char* const* items, int count) {
  for (int i = 0; i < count; ++i)
    SendDlgItemMessageW(hDlg, id, CB_ADDSTRING, 0, (LPARAM)utf8ToWide(items[i]).c_str());
}

static void selectComboByText(HWND hDlg, int id, const std::string& text) {
  int idx = (int)SendDlgItemMessageW(hDlg, id, CB_FINDSTRINGEXACT, -1, (LPARAM)utf8ToWide(text).c_str());
  SendDlgItemMessageW(hDlg, id, CB_SETCURSEL, (idx >= 0) ? idx : 0, 0);
}

static int lvGetSel(HWND lv) {
  return ListView_GetNextItem(lv, -1, LVNI_SELECTED);
}

static void lvSelectItem(HWND lv, int idx) {
  ListView_SetItemState(lv, idx, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
  ListView_EnsureVisible(lv, idx, FALSE);
}

// Show/hide a window and all controls whose IDs fall in [idStart, idEnd].
static void showControlRange(HWND hDlg, int idStart, int idEnd, bool show) {
  int sw = show ? SW_SHOW : SW_HIDE;
  for (int id = idStart; id <= idEnd; ++id) {
    HWND ctrl = GetDlgItem(hDlg, id);
    if (ctrl) ShowWindow(ctrl, sw);
  }
}

// =====================================================================
// Allophone Rule Edit Dialog
// =====================================================================

struct AREditState {
  AllophoneRuleEntry rule;
  bool ok = false;
};

static const char* kTokenTypes[] = {"phoneme", "aspiration", "closure"};
static const char* kPositions[] = {"any", "word-initial", "word-final", "intervocalic", "pre-vocalic", "post-vocalic", "syllabic"};
static const char* kStresses[] = {"any", "stressed", "unstressed", "next-unstressed", "prev-stressed"};
static const char* kActions[] = {"replace", "scale", "shift", "insert-before", "insert-after"};

static void showActionSection(HWND hDlg, const std::string& action) {
  // Show the groupbox and its labeled children for the active action, hide others.
  // We use the groupbox ID to show/hide, and the control ranges.
  HWND grpReplace = GetDlgItem(hDlg, IDC_AR_GRP_REPLACE);
  HWND grpScale   = GetDlgItem(hDlg, IDC_AR_GRP_SCALE);
  HWND grpShift   = GetDlgItem(hDlg, IDC_AR_GRP_SHIFT);
  HWND grpInsert  = GetDlgItem(hDlg, IDC_AR_GRP_INSERT);

  bool isReplace = (action == "replace");
  bool isScale   = (action == "scale");
  bool isShift   = (action == "shift");
  bool isInsert  = (action == "insert-before" || action == "insert-after");

  if (grpReplace) ShowWindow(grpReplace, isReplace ? SW_SHOW : SW_HIDE);
  showControlRange(hDlg, IDC_AR_REPLACE_TO, IDC_AR_REPLACE_ASPSCALE, isReplace);

  if (grpScale) ShowWindow(grpScale, isScale ? SW_SHOW : SW_HIDE);
  showControlRange(hDlg, IDC_AR_SCALE_DUR, IDC_AR_SCALE_FIELDS, isScale);

  if (grpShift) ShowWindow(grpShift, isShift ? SW_SHOW : SW_HIDE);
  showControlRange(hDlg, IDC_AR_SHIFT_FIELDS, IDC_AR_SHIFT_FIELDS, isShift);

  if (grpInsert) ShowWindow(grpInsert, isInsert ? SW_SHOW : SW_HIDE);
  showControlRange(hDlg, IDC_AR_INSERT_PHONEME, IDC_AR_INSERT_CONTEXTS, isInsert);

  // Show/hide the static labels inside each groupbox.
  // The labels are anonymous (-1), so we track them by position via the groupbox approach.
  // Actually, since all controls are defined inside groupboxes in the .rc and we show/hide
  // the groupbox, the child controls inside need explicit show/hide (which we do above).
  // The anonymous labels remain — we'll hide them by enabling/disabling the groupbox visibility.
}

static INT_PTR CALLBACK AllophoneRuleEditDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
  AREditState* st = reinterpret_cast<AREditState*>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));

  switch (msg) {
  case WM_INITDIALOG: {
    st = reinterpret_cast<AREditState*>(lParam);
    SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)st);
    const auto& r = st->rule;

    setDlgItemUtf8(hDlg, IDC_AR_NAME, r.name);
    setDlgItemUtf8(hDlg, IDC_AR_PHONEMES, joinStrVec(r.phonemes));
    setDlgItemUtf8(hDlg, IDC_AR_FLAGS, joinStrVec(r.flags));
    setDlgItemUtf8(hDlg, IDC_AR_NOTFLAGS, joinStrVec(r.notFlags));
    setDlgItemUtf8(hDlg, IDC_AR_AFTER, joinStrVec(r.after));
    setDlgItemUtf8(hDlg, IDC_AR_BEFORE, joinStrVec(r.before));
    setDlgItemUtf8(hDlg, IDC_AR_AFTERFLAGS, joinStrVec(r.afterFlags));
    setDlgItemUtf8(hDlg, IDC_AR_NOTAFTERFLAGS, joinStrVec(r.notAfterFlags));
    setDlgItemUtf8(hDlg, IDC_AR_BEFOREFLAGS, joinStrVec(r.beforeFlags));
    setDlgItemUtf8(hDlg, IDC_AR_NOTBEFOREFLAGS, joinStrVec(r.notBeforeFlags));

    addComboStrings(hDlg, IDC_AR_TOKENTYPE, kTokenTypes, 3);
    selectComboByText(hDlg, IDC_AR_TOKENTYPE, r.tokenType);

    addComboStrings(hDlg, IDC_AR_POSITION, kPositions, 7);
    selectComboByText(hDlg, IDC_AR_POSITION, r.position);

    addComboStrings(hDlg, IDC_AR_STRESS, kStresses, 5);
    selectComboByText(hDlg, IDC_AR_STRESS, r.stress);

    addComboStrings(hDlg, IDC_AR_ACTION, kActions, 5);
    selectComboByText(hDlg, IDC_AR_ACTION, r.action.empty() ? "replace" : r.action);

    // Replace params
    setDlgItemUtf8(hDlg, IDC_AR_REPLACE_TO, r.replaceTo);
    if (r.replaceDurationMs != 0.0) {
      std::ostringstream os; os << r.replaceDurationMs;
      setDlgItemUtf8(hDlg, IDC_AR_REPLACE_DURMS, os.str());
    }
    CheckDlgButton(hDlg, IDC_AR_REPLACE_RMCLOSURE, r.replaceRemovesClosure ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hDlg, IDC_AR_REPLACE_RMASP, r.replaceRemovesAspiration ? BST_CHECKED : BST_UNCHECKED);
    if (r.replaceClosureScale != 0.0) {
      std::ostringstream os; os << r.replaceClosureScale;
      setDlgItemUtf8(hDlg, IDC_AR_REPLACE_CLOSCALE, os.str());
    }
    if (r.replaceAspirationScale != 0.0) {
      std::ostringstream os; os << r.replaceAspirationScale;
      setDlgItemUtf8(hDlg, IDC_AR_REPLACE_ASPSCALE, os.str());
    }

    // Scale params
    { std::ostringstream os; os << r.durationScale; setDlgItemUtf8(hDlg, IDC_AR_SCALE_DUR, os.str()); }
    { std::ostringstream os; os << r.fadeScale; setDlgItemUtf8(hDlg, IDC_AR_SCALE_FADE, os.str()); }
    setDlgItemUtf8(hDlg, IDC_AR_SCALE_FIELDS, fieldScalesToText(r.fieldScales));

    // Shift params
    setDlgItemUtf8(hDlg, IDC_AR_SHIFT_FIELDS, fieldShiftsToText(r.fieldShifts));

    // Insert params
    setDlgItemUtf8(hDlg, IDC_AR_INSERT_PHONEME, r.insertPhoneme);
    { std::ostringstream os; os << r.insertDurationMs; setDlgItemUtf8(hDlg, IDC_AR_INSERT_DURMS, os.str()); }
    { std::ostringstream os; os << r.insertFadeMs; setDlgItemUtf8(hDlg, IDC_AR_INSERT_FADEMS, os.str()); }
    setDlgItemUtf8(hDlg, IDC_AR_INSERT_CONTEXTS, joinStrVec(r.insertContexts));

    showActionSection(hDlg, r.action.empty() ? "replace" : r.action);
    return TRUE;
  }

  case WM_COMMAND: {
    int id = LOWORD(wParam);
    int code = HIWORD(wParam);

    if (id == IDC_AR_ACTION && code == CBN_SELCHANGE) {
      showActionSection(hDlg, getComboSelText(hDlg, IDC_AR_ACTION));
      return TRUE;
    }

    if (id == IDOK) {
      auto& r = st->rule;
      r.name = getDlgItemUtf8(hDlg, IDC_AR_NAME);
      r.phonemes = splitCommaSeparated(getDlgItemUtf8(hDlg, IDC_AR_PHONEMES));
      r.flags = splitCommaSeparated(getDlgItemUtf8(hDlg, IDC_AR_FLAGS));
      r.notFlags = splitCommaSeparated(getDlgItemUtf8(hDlg, IDC_AR_NOTFLAGS));
      r.tokenType = getComboSelText(hDlg, IDC_AR_TOKENTYPE);
      r.position = getComboSelText(hDlg, IDC_AR_POSITION);
      r.stress = getComboSelText(hDlg, IDC_AR_STRESS);
      r.after = splitCommaSeparated(getDlgItemUtf8(hDlg, IDC_AR_AFTER));
      r.before = splitCommaSeparated(getDlgItemUtf8(hDlg, IDC_AR_BEFORE));
      r.afterFlags = splitCommaSeparated(getDlgItemUtf8(hDlg, IDC_AR_AFTERFLAGS));
      r.notAfterFlags = splitCommaSeparated(getDlgItemUtf8(hDlg, IDC_AR_NOTAFTERFLAGS));
      r.beforeFlags = splitCommaSeparated(getDlgItemUtf8(hDlg, IDC_AR_BEFOREFLAGS));
      r.notBeforeFlags = splitCommaSeparated(getDlgItemUtf8(hDlg, IDC_AR_NOTBEFOREFLAGS));
      r.action = getComboSelText(hDlg, IDC_AR_ACTION);

      // Replace
      r.replaceTo = getDlgItemUtf8(hDlg, IDC_AR_REPLACE_TO);
      { std::string s = getDlgItemUtf8(hDlg, IDC_AR_REPLACE_DURMS);
        r.replaceDurationMs = s.empty() ? 0.0 : std::atof(s.c_str()); }
      r.replaceRemovesClosure = (IsDlgButtonChecked(hDlg, IDC_AR_REPLACE_RMCLOSURE) == BST_CHECKED);
      r.replaceRemovesAspiration = (IsDlgButtonChecked(hDlg, IDC_AR_REPLACE_RMASP) == BST_CHECKED);
      { std::string s = getDlgItemUtf8(hDlg, IDC_AR_REPLACE_CLOSCALE);
        r.replaceClosureScale = s.empty() ? 0.0 : std::atof(s.c_str()); }
      { std::string s = getDlgItemUtf8(hDlg, IDC_AR_REPLACE_ASPSCALE);
        r.replaceAspirationScale = s.empty() ? 0.0 : std::atof(s.c_str()); }

      // Scale
      { std::string s = getDlgItemUtf8(hDlg, IDC_AR_SCALE_DUR);
        r.durationScale = s.empty() ? 1.0 : std::atof(s.c_str()); }
      { std::string s = getDlgItemUtf8(hDlg, IDC_AR_SCALE_FADE);
        r.fadeScale = s.empty() ? 1.0 : std::atof(s.c_str()); }
      r.fieldScales = textToFieldScales(getDlgItemMultiline(hDlg, IDC_AR_SCALE_FIELDS));

      // Shift
      r.fieldShifts = textToFieldShifts(getDlgItemMultiline(hDlg, IDC_AR_SHIFT_FIELDS));

      // Insert
      r.insertPhoneme = getDlgItemUtf8(hDlg, IDC_AR_INSERT_PHONEME);
      { std::string s = getDlgItemUtf8(hDlg, IDC_AR_INSERT_DURMS);
        r.insertDurationMs = s.empty() ? 18.0 : std::atof(s.c_str()); }
      { std::string s = getDlgItemUtf8(hDlg, IDC_AR_INSERT_FADEMS);
        r.insertFadeMs = s.empty() ? 3.0 : std::atof(s.c_str()); }
      r.insertContexts = splitCommaSeparated(getDlgItemUtf8(hDlg, IDC_AR_INSERT_CONTEXTS));

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
  }
  return FALSE;
}

// =====================================================================
// Allophone Rules List Dialog
// =====================================================================

static std::wstring arSummaryCol2(const AllophoneRuleEntry& r) {
  // Show phonemes if any, else flags.
  if (!r.phonemes.empty()) return utf8ToWide(joinStrVec(r.phonemes));
  if (!r.flags.empty()) return utf8ToWide("[" + joinStrVec(r.flags) + "]");
  return L"(any)";
}

static void arPopulateList(HWND lv, const std::vector<AllophoneRuleEntry>& rules) {
  ListView_DeleteAllItems(lv);
  for (int i = 0; i < (int)rules.size(); ++i) {
    const auto& r = rules[i];
    LVITEMW item = {};
    item.mask = LVIF_TEXT;
    item.iItem = i;
    std::wstring name = utf8ToWide(r.name);
    item.pszText = const_cast<wchar_t*>(name.c_str());
    ListView_InsertItem(lv, &item);

    std::wstring col2 = arSummaryCol2(r);
    ListView_SetItemText(lv, i, 1, const_cast<wchar_t*>(col2.c_str()));

    std::wstring pos = utf8ToWide(r.position);
    ListView_SetItemText(lv, i, 2, const_cast<wchar_t*>(pos.c_str()));

    std::wstring act = utf8ToWide(r.action);
    ListView_SetItemText(lv, i, 3, const_cast<wchar_t*>(act.c_str()));
  }
}

static INT_PTR CALLBACK AllophoneRulesListDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
  AllophoneRulesDialogState* st = reinterpret_cast<AllophoneRulesDialogState*>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));

  switch (msg) {
  case WM_INITDIALOG: {
    st = reinterpret_cast<AllophoneRulesDialogState*>(lParam);
    SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)st);

    HWND lv = GetDlgItem(hDlg, IDC_AR_LIST);
    ListView_SetExtendedListViewStyle(lv, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    LVCOLUMNW col = {};
    col.mask = LVCF_TEXT | LVCF_WIDTH;
    col.cx = 120; col.pszText = const_cast<wchar_t*>(L"Name");
    ListView_InsertColumn(lv, 0, &col);
    col.cx = 80; col.pszText = const_cast<wchar_t*>(L"Phonemes/Flags");
    ListView_InsertColumn(lv, 1, &col);
    col.cx = 70; col.pszText = const_cast<wchar_t*>(L"Position");
    ListView_InsertColumn(lv, 2, &col);
    col.cx = 60; col.pszText = const_cast<wchar_t*>(L"Action");
    ListView_InsertColumn(lv, 3, &col);

    arPopulateList(lv, st->rules);
    return TRUE;
  }

  case WM_NOTIFY: {
    NMHDR* nmh = reinterpret_cast<NMHDR*>(lParam);
    if (nmh->idFrom == IDC_AR_LIST && nmh->code == NM_DBLCLK) {
      SendMessageW(hDlg, WM_COMMAND, MAKEWPARAM(IDC_AR_EDIT, BN_CLICKED), 0);
      return TRUE;
    }
    break;
  }

  case WM_COMMAND: {
    int id = LOWORD(wParam);
    HWND lv = GetDlgItem(hDlg, IDC_AR_LIST);

    if (id == IDC_AR_ADD) {
      AREditState es;
      es.rule.action = "replace";
      DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_ALLOPHONE_RULE_EDIT), hDlg, AllophoneRuleEditDlgProc, (LPARAM)&es);
      if (es.ok) {
        st->rules.push_back(std::move(es.rule));
        st->modified = true;
        arPopulateList(lv, st->rules);
        lvSelectItem(lv, (int)st->rules.size() - 1);
      }
      return TRUE;
    }

    if (id == IDC_AR_EDIT) {
      int sel = lvGetSel(lv);
      if (sel < 0 || sel >= (int)st->rules.size()) return TRUE;
      AREditState es;
      es.rule = st->rules[sel];
      DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_ALLOPHONE_RULE_EDIT), hDlg, AllophoneRuleEditDlgProc, (LPARAM)&es);
      if (es.ok) {
        st->rules[sel] = std::move(es.rule);
        st->modified = true;
        arPopulateList(lv, st->rules);
        lvSelectItem(lv, sel);
      }
      return TRUE;
    }

    if (id == IDC_AR_REMOVE) {
      int sel = lvGetSel(lv);
      if (sel < 0 || sel >= (int)st->rules.size()) return TRUE;
      st->rules.erase(st->rules.begin() + sel);
      st->modified = true;
      arPopulateList(lv, st->rules);
      if (sel >= (int)st->rules.size()) sel = (int)st->rules.size() - 1;
      if (sel >= 0) lvSelectItem(lv, sel);
      return TRUE;
    }

    if (id == IDC_AR_MOVEUP) {
      int sel = lvGetSel(lv);
      if (sel <= 0 || sel >= (int)st->rules.size()) return TRUE;
      std::swap(st->rules[sel], st->rules[sel - 1]);
      st->modified = true;
      arPopulateList(lv, st->rules);
      lvSelectItem(lv, sel - 1);
      return TRUE;
    }

    if (id == IDC_AR_MOVEDOWN) {
      int sel = lvGetSel(lv);
      if (sel < 0 || sel >= (int)st->rules.size() - 1) return TRUE;
      std::swap(st->rules[sel], st->rules[sel + 1]);
      st->modified = true;
      arPopulateList(lv, st->rules);
      lvSelectItem(lv, sel + 1);
      return TRUE;
    }

    if (id == IDOK) {
      if (st->modified && st->language) {
        st->language->setAllophoneRules(st->rules);
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
  }
  return FALSE;
}

bool ShowAllophoneRulesDialog(HINSTANCE hInst, HWND parent, AllophoneRulesDialogState& st) {
  st.ok = false;
  DialogBoxParamW(hInst, MAKEINTRESOURCEW(IDD_ALLOPHONE_RULES_LIST), parent, AllophoneRulesListDlgProc, (LPARAM)&st);
  return st.ok;
}

// =====================================================================
// Special Coarticulation Rule Edit Dialog
// =====================================================================

struct SCEditState {
  SpecialCoarticRuleEntry rule;
  bool ok = false;
};

static const char* kVowelFilters[] = {"all", "front", "back"};
static const char* kFormants[] = {"f2", "f3"};
static const char* kSides[] = {"left", "right", "both"};

static INT_PTR CALLBACK SpecialCoarticEditDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
  SCEditState* st = reinterpret_cast<SCEditState*>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));

  switch (msg) {
  case WM_INITDIALOG: {
    st = reinterpret_cast<SCEditState*>(lParam);
    SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)st);
    const auto& r = st->rule;

    setDlgItemUtf8(hDlg, IDC_SC_NAME, r.name);
    setDlgItemUtf8(hDlg, IDC_SC_TRIGGERS, joinStrVec(r.triggers));

    // Vowel filter: CBS_DROPDOWN allows typing a custom value
    addComboStrings(hDlg, IDC_SC_VOWELFILTER, kVowelFilters, 3);
    // If it's a custom value (not all/front/back), set it as text
    SetDlgItemTextW(hDlg, IDC_SC_VOWELFILTER, utf8ToWide(r.vowelFilter).c_str());

    addComboStrings(hDlg, IDC_SC_FORMANT, kFormants, 2);
    selectComboByText(hDlg, IDC_SC_FORMANT, r.formant);

    { std::ostringstream os; os << r.deltaHz; setDlgItemUtf8(hDlg, IDC_SC_DELTAHZ, os.str()); }

    addComboStrings(hDlg, IDC_SC_SIDE, kSides, 3);
    selectComboByText(hDlg, IDC_SC_SIDE, r.side);

    CheckDlgButton(hDlg, IDC_SC_CUMULATIVE, r.cumulative ? BST_CHECKED : BST_UNCHECKED);

    { std::ostringstream os; os << r.unstressedScale; setDlgItemUtf8(hDlg, IDC_SC_UNSTRESSED, os.str()); }
    { std::ostringstream os; os << r.phraseFinalStressedScale; setDlgItemUtf8(hDlg, IDC_SC_PHRASEFINAL, os.str()); }

    return TRUE;
  }

  case WM_COMMAND: {
    int id = LOWORD(wParam);

    if (id == IDOK) {
      auto& r = st->rule;
      r.name = getDlgItemUtf8(hDlg, IDC_SC_NAME);
      r.triggers = splitCommaSeparated(getDlgItemUtf8(hDlg, IDC_SC_TRIGGERS));
      r.vowelFilter = getDlgItemUtf8(hDlg, IDC_SC_VOWELFILTER);
      r.formant = getComboSelText(hDlg, IDC_SC_FORMANT);
      { std::string s = getDlgItemUtf8(hDlg, IDC_SC_DELTAHZ);
        r.deltaHz = s.empty() ? 0.0 : std::atof(s.c_str()); }
      r.side = getComboSelText(hDlg, IDC_SC_SIDE);
      r.cumulative = (IsDlgButtonChecked(hDlg, IDC_SC_CUMULATIVE) == BST_CHECKED);
      { std::string s = getDlgItemUtf8(hDlg, IDC_SC_UNSTRESSED);
        r.unstressedScale = s.empty() ? 1.0 : std::atof(s.c_str()); }
      { std::string s = getDlgItemUtf8(hDlg, IDC_SC_PHRASEFINAL);
        r.phraseFinalStressedScale = s.empty() ? 1.0 : std::atof(s.c_str()); }
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
  }
  return FALSE;
}

// =====================================================================
// Special Coarticulation Rules List Dialog
// =====================================================================

static void scPopulateList(HWND lv, const std::vector<SpecialCoarticRuleEntry>& rules) {
  ListView_DeleteAllItems(lv);
  for (int i = 0; i < (int)rules.size(); ++i) {
    const auto& r = rules[i];
    LVITEMW item = {};
    item.mask = LVIF_TEXT;
    item.iItem = i;
    std::wstring name = utf8ToWide(r.name);
    item.pszText = const_cast<wchar_t*>(name.c_str());
    ListView_InsertItem(lv, &item);

    std::wstring triggers = utf8ToWide(joinStrVec(r.triggers));
    ListView_SetItemText(lv, i, 1, const_cast<wchar_t*>(triggers.c_str()));

    std::wstring formant = utf8ToWide(r.formant);
    ListView_SetItemText(lv, i, 2, const_cast<wchar_t*>(formant.c_str()));

    std::wstring delta;
    { std::ostringstream os; os << r.deltaHz; delta = utf8ToWide(os.str()); }
    ListView_SetItemText(lv, i, 3, const_cast<wchar_t*>(delta.c_str()));
  }
}

static INT_PTR CALLBACK SpecialCoarticListDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
  SpecialCoarticDialogState* st = reinterpret_cast<SpecialCoarticDialogState*>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));

  switch (msg) {
  case WM_INITDIALOG: {
    st = reinterpret_cast<SpecialCoarticDialogState*>(lParam);
    SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)st);

    HWND lv = GetDlgItem(hDlg, IDC_SC_LIST);
    ListView_SetExtendedListViewStyle(lv, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    LVCOLUMNW col = {};
    col.mask = LVCF_TEXT | LVCF_WIDTH;
    col.cx = 120; col.pszText = const_cast<wchar_t*>(L"Name");
    ListView_InsertColumn(lv, 0, &col);
    col.cx = 80; col.pszText = const_cast<wchar_t*>(L"Triggers");
    ListView_InsertColumn(lv, 1, &col);
    col.cx = 50; col.pszText = const_cast<wchar_t*>(L"Formant");
    ListView_InsertColumn(lv, 2, &col);
    col.cx = 60; col.pszText = const_cast<wchar_t*>(L"Delta Hz");
    ListView_InsertColumn(lv, 3, &col);

    scPopulateList(lv, st->rules);
    return TRUE;
  }

  case WM_NOTIFY: {
    NMHDR* nmh = reinterpret_cast<NMHDR*>(lParam);
    if (nmh->idFrom == IDC_SC_LIST && nmh->code == NM_DBLCLK) {
      SendMessageW(hDlg, WM_COMMAND, MAKEWPARAM(IDC_SC_EDIT, BN_CLICKED), 0);
      return TRUE;
    }
    break;
  }

  case WM_COMMAND: {
    int id = LOWORD(wParam);
    HWND lv = GetDlgItem(hDlg, IDC_SC_LIST);

    if (id == IDC_SC_ADD) {
      SCEditState es;
      DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_SPECIAL_COARTIC_EDIT), hDlg, SpecialCoarticEditDlgProc, (LPARAM)&es);
      if (es.ok) {
        st->rules.push_back(std::move(es.rule));
        st->modified = true;
        scPopulateList(lv, st->rules);
        lvSelectItem(lv, (int)st->rules.size() - 1);
      }
      return TRUE;
    }

    if (id == IDC_SC_EDIT) {
      int sel = lvGetSel(lv);
      if (sel < 0 || sel >= (int)st->rules.size()) return TRUE;
      SCEditState es;
      es.rule = st->rules[sel];
      DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_SPECIAL_COARTIC_EDIT), hDlg, SpecialCoarticEditDlgProc, (LPARAM)&es);
      if (es.ok) {
        st->rules[sel] = std::move(es.rule);
        st->modified = true;
        scPopulateList(lv, st->rules);
        lvSelectItem(lv, sel);
      }
      return TRUE;
    }

    if (id == IDC_SC_REMOVE) {
      int sel = lvGetSel(lv);
      if (sel < 0 || sel >= (int)st->rules.size()) return TRUE;
      st->rules.erase(st->rules.begin() + sel);
      st->modified = true;
      scPopulateList(lv, st->rules);
      if (sel >= (int)st->rules.size()) sel = (int)st->rules.size() - 1;
      if (sel >= 0) lvSelectItem(lv, sel);
      return TRUE;
    }

    if (id == IDC_SC_MOVEUP) {
      int sel = lvGetSel(lv);
      if (sel <= 0 || sel >= (int)st->rules.size()) return TRUE;
      std::swap(st->rules[sel], st->rules[sel - 1]);
      st->modified = true;
      scPopulateList(lv, st->rules);
      lvSelectItem(lv, sel - 1);
      return TRUE;
    }

    if (id == IDC_SC_MOVEDOWN) {
      int sel = lvGetSel(lv);
      if (sel < 0 || sel >= (int)st->rules.size() - 1) return TRUE;
      std::swap(st->rules[sel], st->rules[sel + 1]);
      st->modified = true;
      scPopulateList(lv, st->rules);
      lvSelectItem(lv, sel + 1);
      return TRUE;
    }

    if (id == IDOK) {
      if (st->modified && st->language) {
        st->language->setSpecialCoarticRules(st->rules);
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
  }
  return FALSE;
}

bool ShowSpecialCoarticDialog(HINSTANCE hInst, HWND parent, SpecialCoarticDialogState& st) {
  st.ok = false;
  DialogBoxParamW(hInst, MAKEINTRESOURCEW(IDD_SPECIAL_COARTIC_LIST), parent, SpecialCoarticListDlgProc, (LPARAM)&st);
  return st.ok;
}

} // namespace tgsb_editor

#define UNICODE
#define _UNICODE

#include <windows.h>
#include <commctrl.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <mmsystem.h>
#include <oleacc.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "resource.h"
#include "yaml_edit.h"
#include "nvsp_runtime.h"
#include "process_util.h"
#include "wav_writer.h"
#include "utf8.h"

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Winmm.lib")

namespace fs = std::filesystem;

using nvsp_editor::LanguageYaml;
using nvsp_editor::PhonemesYaml;
using nvsp_editor::ReplacementRule;
using nvsp_editor::ReplacementWhen;
using nvsp_editor::NvspRuntime;

static constexpr int kSampleRate = 22050;

// -------------------------
// UTF helpers
// -------------------------
static std::wstring utf8ToWide(const std::string& s) {
  if (s.empty()) return {};
  int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
  if (len <= 1) return {};
  std::wstring out(static_cast<size_t>(len), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), len);
  out.resize(static_cast<size_t>(len - 1));
  return out;
}

static std::string wideToUtf8(const std::wstring& w) {
  if (w.empty()) return {};
  int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
  if (len <= 1) return {};
  std::string out(static_cast<size_t>(len), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, out.data(), len, nullptr, nullptr);
  out.resize(static_cast<size_t>(len - 1));
  return out;
}

static std::wstring exeDir() {
  wchar_t buf[MAX_PATH] = {0};
  GetModuleFileNameW(nullptr, buf, MAX_PATH);
  PathRemoveFileSpecW(buf);
  return buf;
}

static std::wstring iniPath() {
  std::wstring p = exeDir();
  if (!p.empty() && p.back() != L'\\') p += L'\\';
  p += L"nvspPhonemeEditor.ini";
  return p;
}

static std::wstring readIni(const wchar_t* section, const wchar_t* key, const wchar_t* def = L"") {
  wchar_t buf[2048] = {0};
  GetPrivateProfileStringW(section, key, def, buf, 2048, iniPath().c_str());
  return buf;
}

static void writeIni(const wchar_t* section, const wchar_t* key, const std::wstring& value) {
  WritePrivateProfileStringW(section, key, value.c_str(), iniPath().c_str());
}

static int readIniInt(const wchar_t* section, const wchar_t* key, int defVal) {
  return static_cast<int>(GetPrivateProfileIntW(section, key, defVal, iniPath().c_str()));
}

static void writeIniInt(const wchar_t* section, const wchar_t* key, int value) {
  wchar_t buf[64];
  _itow_s(value, buf, 10);
  WritePrivateProfileStringW(section, key, buf, iniPath().c_str());
}

static void msgBox(HWND owner, const std::wstring& text, const std::wstring& title = L"NV Speech Player Phoneme Editor", UINT flags = MB_OK) {
  // Preserve keyboard focus across modal message boxes.
  HWND prevFocus = GetFocus();
  MessageBoxW(owner, text.c_str(), title.c_str(), flags);
  if (prevFocus && IsWindow(prevFocus) && IsWindowEnabled(prevFocus) && IsWindowVisible(prevFocus)) {
    SetFocus(prevFocus);
  }
}

// Forward declaration (used by dialog procs before list helpers are defined).
static void ensureListViewHasSelection(HWND lv);

// -------------------------
// Accessibility: force stable names for certain controls (ListView)
//
// NVDA sometimes announces a SysListView32 as just "list" if we don't provide
// a robust accName. Dialog-label association isn't reliable in a normal Win32
// top-level window, so we override accName for CHILDID_SELF via WM_GETOBJECT.
//
// This keeps the UI readable with screen readers while still using plain
// Win32 controls.

class AccNameWrapper : public IAccessible {
public:
  AccNameWrapper(IAccessible* inner, std::wstring name)
      : m_ref(1), m_inner(inner), m_name(std::move(name)) {
    if (m_inner) m_inner->AddRef();
  }
  ~AccNameWrapper() {
    if (m_inner) m_inner->Release();
  }

  // IUnknown
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override {
    if (!ppvObject) return E_INVALIDARG;
    *ppvObject = nullptr;
    if (riid == IID_IUnknown || riid == IID_IDispatch || riid == IID_IAccessible) {
      *ppvObject = static_cast<IAccessible*>(this);
      AddRef();
      return S_OK;
    }
    return m_inner ? m_inner->QueryInterface(riid, ppvObject) : E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&m_ref)); }
  ULONG STDMETHODCALLTYPE Release() override {
    LONG r = InterlockedDecrement(&m_ref);
    if (r == 0) delete this;
    return static_cast<ULONG>(r);
  }

  // IDispatch (forward)
  HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* pctinfo) override { return m_inner ? m_inner->GetTypeInfoCount(pctinfo) : E_FAIL; }
  HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT iTInfo, LCID lcid, ITypeInfo** ppTInfo) override { return m_inner ? m_inner->GetTypeInfo(iTInfo, lcid, ppTInfo) : E_FAIL; }
  HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID riid, LPOLESTR* rgszNames, UINT cNames, LCID lcid, DISPID* rgDispId) override {
    return m_inner ? m_inner->GetIDsOfNames(riid, rgszNames, cNames, lcid, rgDispId) : E_FAIL;
  }
  HRESULT STDMETHODCALLTYPE Invoke(DISPID dispIdMember, REFIID riid, LCID lcid, WORD wFlags, DISPPARAMS* pDispParams, VARIANT* pVarResult, EXCEPINFO* pExcepInfo, UINT* puArgErr) override {
    return m_inner ? m_inner->Invoke(dispIdMember, riid, lcid, wFlags, pDispParams, pVarResult, pExcepInfo, puArgErr) : E_FAIL;
  }

  // IAccessible: forward everything except get_accName for CHILDID_SELF
  HRESULT STDMETHODCALLTYPE get_accParent(IDispatch** ppdispParent) override { return m_inner ? m_inner->get_accParent(ppdispParent) : E_FAIL; }
  HRESULT STDMETHODCALLTYPE get_accChildCount(long* pcountChildren) override { return m_inner ? m_inner->get_accChildCount(pcountChildren) : E_FAIL; }
  HRESULT STDMETHODCALLTYPE get_accChild(VARIANT varChild, IDispatch** ppdispChild) override { return m_inner ? m_inner->get_accChild(varChild, ppdispChild) : E_FAIL; }
  HRESULT STDMETHODCALLTYPE get_accName(VARIANT varChild, BSTR* pszName) override {
    if (!pszName) return E_INVALIDARG;
    if (varChild.vt == VT_I4 && varChild.lVal == CHILDID_SELF) {
      *pszName = SysAllocString(m_name.c_str());
      return (*pszName) ? S_OK : E_OUTOFMEMORY;
    }
    return m_inner ? m_inner->get_accName(varChild, pszName) : E_FAIL;
  }
  HRESULT STDMETHODCALLTYPE get_accValue(VARIANT varChild, BSTR* pszValue) override { return m_inner ? m_inner->get_accValue(varChild, pszValue) : E_FAIL; }
  HRESULT STDMETHODCALLTYPE get_accDescription(VARIANT varChild, BSTR* pszDescription) override { return m_inner ? m_inner->get_accDescription(varChild, pszDescription) : E_FAIL; }
  HRESULT STDMETHODCALLTYPE get_accRole(VARIANT varChild, VARIANT* pvarRole) override { return m_inner ? m_inner->get_accRole(varChild, pvarRole) : E_FAIL; }
  HRESULT STDMETHODCALLTYPE get_accState(VARIANT varChild, VARIANT* pvarState) override { return m_inner ? m_inner->get_accState(varChild, pvarState) : E_FAIL; }
  HRESULT STDMETHODCALLTYPE get_accHelp(VARIANT varChild, BSTR* pszHelp) override { return m_inner ? m_inner->get_accHelp(varChild, pszHelp) : E_FAIL; }
  HRESULT STDMETHODCALLTYPE get_accHelpTopic(BSTR* pszHelpFile, VARIANT varChild, long* pidTopic) override { return m_inner ? m_inner->get_accHelpTopic(pszHelpFile, varChild, pidTopic) : E_FAIL; }
  HRESULT STDMETHODCALLTYPE get_accKeyboardShortcut(VARIANT varChild, BSTR* pszKeyboardShortcut) override { return m_inner ? m_inner->get_accKeyboardShortcut(varChild, pszKeyboardShortcut) : E_FAIL; }
  HRESULT STDMETHODCALLTYPE get_accFocus(VARIANT* pvarChild) override { return m_inner ? m_inner->get_accFocus(pvarChild) : E_FAIL; }
  HRESULT STDMETHODCALLTYPE get_accSelection(VARIANT* pvarChildren) override { return m_inner ? m_inner->get_accSelection(pvarChildren) : E_FAIL; }
  HRESULT STDMETHODCALLTYPE get_accDefaultAction(VARIANT varChild, BSTR* pszDefaultAction) override { return m_inner ? m_inner->get_accDefaultAction(varChild, pszDefaultAction) : E_FAIL; }
  HRESULT STDMETHODCALLTYPE accSelect(long flagsSelect, VARIANT varChild) override { return m_inner ? m_inner->accSelect(flagsSelect, varChild) : E_FAIL; }
  HRESULT STDMETHODCALLTYPE accLocation(long* pxLeft, long* pyTop, long* pcxWidth, long* pcyHeight, VARIANT varChild) override { return m_inner ? m_inner->accLocation(pxLeft, pyTop, pcxWidth, pcyHeight, varChild) : E_FAIL; }
  HRESULT STDMETHODCALLTYPE accNavigate(long navDir, VARIANT varStart, VARIANT* pvarEndUpAt) override { return m_inner ? m_inner->accNavigate(navDir, varStart, pvarEndUpAt) : E_FAIL; }
  HRESULT STDMETHODCALLTYPE accHitTest(long xLeft, long yTop, VARIANT* pvarChild) override { return m_inner ? m_inner->accHitTest(xLeft, yTop, pvarChild) : E_FAIL; }
  HRESULT STDMETHODCALLTYPE accDoDefaultAction(VARIANT varChild) override { return m_inner ? m_inner->accDoDefaultAction(varChild) : E_FAIL; }
  HRESULT STDMETHODCALLTYPE put_accName(VARIANT varChild, BSTR szName) override { return m_inner ? m_inner->put_accName(varChild, szName) : E_FAIL; }
  HRESULT STDMETHODCALLTYPE put_accValue(VARIANT varChild, BSTR szValue) override { return m_inner ? m_inner->put_accValue(varChild, szValue) : E_FAIL; }

private:
  LONG m_ref;
  IAccessible* m_inner;
  std::wstring m_name;
};

struct AccSubclassData {
  std::wstring name;
  AccNameWrapper* wrapper = nullptr;
};

static LRESULT CALLBACK accListViewSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
  AccSubclassData* data = reinterpret_cast<AccSubclassData*>(dwRefData);
  if (msg == WM_GETOBJECT && static_cast<long>(lParam) == OBJID_CLIENT) {
    // Cache wrapper the first time we're asked.
    if (data && !data->wrapper) {
      IAccessible* inner = nullptr;
      if (SUCCEEDED(CreateStdAccessibleObject(hwnd, OBJID_CLIENT, IID_IAccessible, reinterpret_cast<void**>(&inner))) && inner) {
        data->wrapper = new AccNameWrapper(inner, data->name);
        inner->Release();
      }
    }
    if (data && data->wrapper) {
      return LresultFromObject(IID_IAccessible, wParam, data->wrapper);
    }
  }
  if (msg == WM_SETFOCUS) {
    // When tabbing into a list view, make sure an actual item is focused
    // so keyboard users and screen readers land somewhere meaningful.
    ensureListViewHasSelection(hwnd);
  }

  if (msg == WM_NCDESTROY) {
    if (data) {
      if (data->wrapper) {
        data->wrapper->Release();
        data->wrapper = nullptr;
      }
      delete data;
    }
    RemoveWindowSubclass(hwnd, accListViewSubclassProc, uIdSubclass);
  }
  return DefSubclassProc(hwnd, msg, wParam, lParam);
}

static void installAccessibleNameForListView(HWND lv, const std::wstring& name) {
  if (!lv) return;
  // Keep window text set too; some AT uses it.
  SetWindowTextW(lv, name.c_str());
  auto* data = new AccSubclassData();
  data->name = name;
  SetWindowSubclass(lv, accListViewSubclassProc, 1, reinterpret_cast<DWORD_PTR>(data));
}

// -------------------------
// Folder picker (IFileDialog)
// -------------------------
static bool pickFolder(HWND owner, const wchar_t* title, std::wstring& outFolder) {
  outFolder.clear();

  // Preserve keyboard focus across modal dialogs.
  HWND prevFocus = GetFocus();
  auto restoreFocus = [&]() {
    if (prevFocus && IsWindow(prevFocus) && IsWindowEnabled(prevFocus) && IsWindowVisible(prevFocus)) {
      SetFocus(prevFocus);
    }
  };

  IFileDialog* pDlg = nullptr;
  HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pDlg));
  if (FAILED(hr) || !pDlg) {
    restoreFocus();
    return false;
  }

  DWORD opts = 0;
  pDlg->GetOptions(&opts);
  pDlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
  pDlg->SetTitle(title);

  hr = pDlg->Show(owner);
  if (FAILED(hr)) {
    pDlg->Release();
    restoreFocus();
    return false;
  }

  IShellItem* pItem = nullptr;
  hr = pDlg->GetResult(&pItem);
  if (SUCCEEDED(hr) && pItem) {
    PWSTR pszPath = nullptr;
    hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszPath);
    if (SUCCEEDED(hr) && pszPath) {
      outFolder = pszPath;
      CoTaskMemFree(pszPath);
    }
    pItem->Release();
  }

  pDlg->Release();
  restoreFocus();
  return !outFolder.empty();
}

static bool pickSaveWav(HWND owner, std::wstring& outPath) {
  outPath.clear();

  // Preserve keyboard focus across modal dialogs.
  HWND prevFocus = GetFocus();
  auto restoreFocus = [&]() {
    if (prevFocus && IsWindow(prevFocus) && IsWindowEnabled(prevFocus) && IsWindowVisible(prevFocus)) {
      SetFocus(prevFocus);
    }
  };

  wchar_t fileBuf[MAX_PATH] = {0};
  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = owner;
  ofn.lpstrFile = fileBuf;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrFilter = L"WAV files (*.wav)\0*.wav\0All files\0*.*\0";
  ofn.lpstrDefExt = L"wav";
  ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
  if (!GetSaveFileNameW(&ofn)) {
    restoreFocus();
    return false;
  }
  outPath = fileBuf;
  restoreFocus();
  return true;
}

// -------------------------
// Dialogs
// -------------------------
struct AddMappingDialogState {
  ReplacementRule rule;
  std::vector<std::string> classNames;
  bool ok = false;
};

static void comboAddNone(HWND hCombo) {
  SendMessageW(hCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"(none)"));
  SendMessageW(hCombo, CB_SETITEMDATA, 0, 0);
}

static INT_PTR CALLBACK AddMappingDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
  AddMappingDialogState* st = reinterpret_cast<AddMappingDialogState*>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));

  switch (msg) {
    case WM_INITDIALOG: {
      st = reinterpret_cast<AddMappingDialogState*>(lParam);
      SetWindowLongPtrW(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));

      SetDlgItemTextW(hDlg, IDC_MAP_FROM, utf8ToWide(st->rule.from).c_str());
      SetDlgItemTextW(hDlg, IDC_MAP_TO, utf8ToWide(st->rule.to).c_str());

      CheckDlgButton(hDlg, IDC_MAP_WORDSTART, st->rule.when.atWordStart ? BST_CHECKED : BST_UNCHECKED);
      CheckDlgButton(hDlg, IDC_MAP_WORDEND, st->rule.when.atWordEnd ? BST_CHECKED : BST_UNCHECKED);

      HWND before = GetDlgItem(hDlg, IDC_MAP_BEFORECLASS);
      HWND after = GetDlgItem(hDlg, IDC_MAP_AFTERCLASS);

      comboAddNone(before);
      comboAddNone(after);

      int idxBefore = 0;
      int idxAfter = 0;

      for (size_t i = 0; i < st->classNames.size(); ++i) {
        std::wstring w = utf8ToWide(st->classNames[i]);
        int posB = static_cast<int>(SendMessageW(before, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(w.c_str())));
        int posA = static_cast<int>(SendMessageW(after, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(w.c_str())));
        if (!st->rule.when.beforeClass.empty() && st->classNames[i] == st->rule.when.beforeClass) idxBefore = posB;
        if (!st->rule.when.afterClass.empty() && st->classNames[i] == st->rule.when.afterClass) idxAfter = posA;
      }

      SendMessageW(before, CB_SETCURSEL, idxBefore, 0);
      SendMessageW(after, CB_SETCURSEL, idxAfter, 0);

      return TRUE;
    }

    case WM_COMMAND: {
      if (LOWORD(wParam) == IDOK && st) {
        wchar_t buf[1024];
        GetDlgItemTextW(hDlg, IDC_MAP_FROM, buf, 1024);
        st->rule.from = wideToUtf8(buf);
        GetDlgItemTextW(hDlg, IDC_MAP_TO, buf, 1024);
        st->rule.to = wideToUtf8(buf);

        st->rule.when.atWordStart = (IsDlgButtonChecked(hDlg, IDC_MAP_WORDSTART) == BST_CHECKED);
        st->rule.when.atWordEnd = (IsDlgButtonChecked(hDlg, IDC_MAP_WORDEND) == BST_CHECKED);

        auto readCombo = [&](int id, std::string& out) {
          HWND h = GetDlgItem(hDlg, id);
          int sel = static_cast<int>(SendMessageW(h, CB_GETCURSEL, 0, 0));
          if (sel <= 0) { out.clear(); return; }
          wchar_t item[512];
          SendMessageW(h, CB_GETLBTEXT, sel, reinterpret_cast<LPARAM>(item));
          out = wideToUtf8(item);
        };

        readCombo(IDC_MAP_BEFORECLASS, st->rule.when.beforeClass);
        readCombo(IDC_MAP_AFTERCLASS, st->rule.when.afterClass);

        if (st->rule.from.empty() || st->rule.to.empty()) {
          msgBox(hDlg, L"Both 'From' and 'To' are required.", L"Add mapping", MB_ICONERROR);
          return TRUE;
        }

        st->ok = true;
        EndDialog(hDlg, IDOK);
        return TRUE;
      }

      if (LOWORD(wParam) == IDCANCEL) {
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
      }
      break;
    }

  }

  return FALSE;
}

struct ClonePhonemeDialogState {
  std::vector<std::string> keys;
  std::string fromKey;
  std::string newKey;
  bool ok = false;
};

static INT_PTR CALLBACK ClonePhonemeDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
  ClonePhonemeDialogState* st = reinterpret_cast<ClonePhonemeDialogState*>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));

  switch (msg) {
    case WM_INITDIALOG: {
      st = reinterpret_cast<ClonePhonemeDialogState*>(lParam);
      SetWindowLongPtrW(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));

      HWND combo = GetDlgItem(hDlg, IDC_CLONE_FROM);
      int selIndex = 0;
      for (size_t i = 0; i < st->keys.size(); ++i) {
        std::wstring w = utf8ToWide(st->keys[i]);
        int pos = static_cast<int>(SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(w.c_str())));
        if (!st->fromKey.empty() && st->keys[i] == st->fromKey) selIndex = pos;
      }
      SendMessageW(combo, CB_SETCURSEL, selIndex, 0);
      SetDlgItemTextW(hDlg, IDC_CLONE_NEWKEY, L"");
      return TRUE;
    }

    case WM_COMMAND: {
      if (LOWORD(wParam) == IDOK && st) {
        wchar_t buf[512];
        GetDlgItemTextW(hDlg, IDC_CLONE_NEWKEY, buf, 512);
        st->newKey = wideToUtf8(buf);

        HWND combo = GetDlgItem(hDlg, IDC_CLONE_FROM);
        int sel = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
        if (sel < 0 || sel >= static_cast<int>(st->keys.size())) {
          msgBox(hDlg, L"Choose a source phoneme.", L"Clone phoneme", MB_ICONERROR);
          return TRUE;
        }
        st->fromKey = st->keys[static_cast<size_t>(sel)];

        if (st->newKey.empty()) {
          msgBox(hDlg, L"New phoneme key is required.", L"Clone phoneme", MB_ICONERROR);
          return TRUE;
        }

        st->ok = true;
        EndDialog(hDlg, IDOK);
        return TRUE;
      }
      if (LOWORD(wParam) == IDCANCEL) {
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
      }
      break;
    }
  }

  return FALSE;
}

struct EditValueDialogState {
  std::string field;
  std::string value;
  nvsp_editor::Node baseMap;
  NvspRuntime* runtime = nullptr;
  bool livePreview = true;
  bool armed = false;
  UINT_PTR previewTimer = 0;
  bool ok = false;
};

static bool tryParseDoubleStrict(const std::wstring& s, double& out) {
  const wchar_t* p = s.c_str();
  wchar_t* end = nullptr;
  out = wcstod(p, &end);
  if (end == p) return false;
  // Allow trailing whitespace only.
  while (end && (*end == L' ' || *end == L'\t' || *end == L'\r' || *end == L'\n')) ++end;
  return end && *end == 0;
}

static std::wstring formatDoubleSmart(double v) {
  // Prefer integer formatting when the value is very close to an integer.
  const double r = std::round(v);
  if (std::fabs(v - r) < 1e-9) {
    wchar_t buf[64];
    swprintf_s(buf, L"%.0f", r);
    return buf;
  }

  // Otherwise format with a few decimals and trim trailing zeros.
  wchar_t buf[64];
  swprintf_s(buf, L"%.6f", v);
  std::wstring out = buf;
  // Trim trailing zeros
  while (!out.empty() && out.back() == L'0') out.pop_back();
  // Trim trailing dot
  if (!out.empty() && out.back() == L'.') out.pop_back();
  return out;
}

static LRESULT CALLBACK numericSpinEditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                                   UINT_PTR uIdSubclass, DWORD_PTR) {
  if (msg == WM_NCDESTROY) {
    RemoveWindowSubclass(hwnd, numericSpinEditSubclassProc, uIdSubclass);
    return DefSubclassProc(hwnd, msg, wParam, lParam);
  }
  if (msg == WM_KEYDOWN) {
    if (wParam == VK_UP || wParam == VK_DOWN || wParam == VK_PRIOR || wParam == VK_NEXT) {
      wchar_t buf[256];
      GetWindowTextW(hwnd, buf, 256);
      double v = 0.0;
      if (!tryParseDoubleStrict(buf, v)) {
        return DefSubclassProc(hwnd, msg, wParam, lParam);
      }
      const double step = (wParam == VK_PRIOR || wParam == VK_NEXT) ? 50.0 : 1.0;
      v += (wParam == VK_UP || wParam == VK_PRIOR) ? step : -step;
      std::wstring out = formatDoubleSmart(v);
      SetWindowTextW(hwnd, out.c_str());
      SendMessageW(hwnd, EM_SETSEL, static_cast<WPARAM>(out.size()), static_cast<LPARAM>(out.size()));
      return 0;
    }
  }
  return DefSubclassProc(hwnd, msg, wParam, lParam);
}

static INT_PTR CALLBACK EditValueDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
  EditValueDialogState* st = reinterpret_cast<EditValueDialogState*>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));

  auto schedulePreview = [&]() {
    if (!st || !st->livePreview || !st->armed) return;
    if (st->previewTimer) KillTimer(hDlg, st->previewTimer);
    st->previewTimer = SetTimer(hDlg, 1, 250, nullptr);
  };

  auto doPreview = [&]() {
    if (!st || !st->livePreview || !st->armed) return;
    if (!st->runtime) return;
    if (!st->runtime->dllsLoaded()) return;
    if (!st->baseMap.isMap()) return;

    // Grab current text from the edit control.
    wchar_t buf[1024];
    GetDlgItemTextW(hDlg, IDC_VAL_VALUE, buf, 1024);
    st->value = wideToUtf8(buf);

    nvsp_editor::Node tmp = st->baseMap;
    auto it = tmp.map.find(st->field);
    if (it == tmp.map.end()) {
      // If missing, create it.
      tmp.map[st->field] = nvsp_editor::Node{};
      it = tmp.map.find(st->field);
    }
    it->second.type = nvsp_editor::Node::Type::Scalar;
    it->second.scalar = st->value;

    std::vector<sample> samples;
    std::string err;
    if (!st->runtime->synthPreviewPhoneme(tmp, kSampleRate, samples, err)) {
      return; // silent on preview errors
    }
    if (samples.empty()) return;

    std::wstring wavPath = nvsp_editor::makeTempWavPath(L"nvpe");
    if (!nvsp_editor::writeWav16Mono(wavPath, kSampleRate, samples, err)) {
      return;
    }
    PlaySoundW(nullptr, NULL, SND_ASYNC);
    PlaySoundW(wavPath.c_str(), NULL, SND_FILENAME | SND_ASYNC);
  };

  switch (msg) {
    case WM_INITDIALOG: {
      st = reinterpret_cast<EditValueDialogState*>(lParam);
      SetWindowLongPtrW(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
      SetDlgItemTextW(hDlg, IDC_VAL_FIELD, utf8ToWide(st->field).c_str());
      SetDlgItemTextW(hDlg, IDC_VAL_VALUE, utf8ToWide(st->value).c_str());
      CheckDlgButton(hDlg, IDC_VAL_LIVE_PREVIEW, st->livePreview ? BST_CHECKED : BST_UNCHECKED);
      // Make the numeric field behave like a spinbox: Up/Down adjusts by 1, typing still works.
      if (HWND valEdit = GetDlgItem(hDlg, IDC_VAL_VALUE)) {
        SetWindowSubclass(valEdit, numericSpinEditSubclassProc, 1, 0);
        // Select all so numeric edits are quick.
        SendMessageW(valEdit, EM_SETSEL, 0, -1);
      }
      st->armed = true;
      return TRUE;
    }

    case WM_TIMER: {
      if (!st) break;
      if (wParam == 1) {
        KillTimer(hDlg, 1);
        st->previewTimer = 0;
        doPreview();
        return TRUE;
      }
      break;
    }

    case WM_COMMAND: {
      if (!st) break;

      if (LOWORD(wParam) == IDC_VAL_LIVE_PREVIEW) {
        st->livePreview = (IsDlgButtonChecked(hDlg, IDC_VAL_LIVE_PREVIEW) == BST_CHECKED);
        if (st->livePreview) schedulePreview();
        return TRUE;
      }

      if (LOWORD(wParam) == IDC_VAL_VALUE && HIWORD(wParam) == EN_CHANGE) {
        schedulePreview();
        return TRUE;
      }

      if (LOWORD(wParam) == IDOK) {
        if (st->previewTimer) {
          KillTimer(hDlg, st->previewTimer);
          st->previewTimer = 0;
        }
        wchar_t buf[1024];
        GetDlgItemTextW(hDlg, IDC_VAL_VALUE, buf, 1024);
        st->value = wideToUtf8(buf);
        st->ok = true;
        EndDialog(hDlg, IDOK);
        return TRUE;
      }
      if (LOWORD(wParam) == IDCANCEL) {
        if (st && st->previewTimer) {
          KillTimer(hDlg, st->previewTimer);
          st->previewTimer = 0;
        }
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
      }
      break;
    }
  }

  return FALSE;
}



// -------------------------
// Dialogs: Language settings
// -------------------------

struct EditSettingDialogState {
  std::string key;
  std::string value;
  std::vector<std::string> knownKeys;
  bool ok = false;
};

static void comboFillKnownKeys(HWND combo, const std::vector<std::string>& keys) {
  SendMessageW(combo, CB_RESETCONTENT, 0, 0);
  for (const auto& k : keys) {
    std::wstring wk = utf8ToWide(k);
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wk.c_str()));
  }
}

static INT_PTR CALLBACK EditSettingDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
  EditSettingDialogState* st = reinterpret_cast<EditSettingDialogState*>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));

  switch (msg) {
    case WM_INITDIALOG: {
      st = reinterpret_cast<EditSettingDialogState*>(lParam);
      SetWindowLongPtrW(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));

      HWND combo = GetDlgItem(hDlg, IDC_SETTING_KEY);
      if (combo) {
        comboFillKnownKeys(combo, st->knownKeys);
        SetWindowTextW(combo, utf8ToWide(st->key).c_str());
      }
      SetDlgItemTextW(hDlg, IDC_SETTING_VALUE, utf8ToWide(st->value).c_str());

      HWND valEdit = GetDlgItem(hDlg, IDC_SETTING_VALUE);
      if (valEdit) {
        SetWindowSubclass(valEdit, numericSpinEditSubclassProc, 1, 0);
      }
      return TRUE;
    }

    case WM_COMMAND: {
      if (!st) break;

      if (LOWORD(wParam) == IDOK) {
        wchar_t keyBuf[512];
        wchar_t valBuf[1024];
        GetDlgItemTextW(hDlg, IDC_SETTING_KEY, keyBuf, 512);
        GetDlgItemTextW(hDlg, IDC_SETTING_VALUE, valBuf, 1024);

        st->key = wideToUtf8(keyBuf);
        st->value = wideToUtf8(valBuf);

        // Basic trimming of surrounding whitespace for key.
        while (!st->key.empty() && (st->key.front() == ' ' || st->key.front() == '\t')) st->key.erase(st->key.begin());
        while (!st->key.empty() && (st->key.back() == ' ' || st->key.back() == '\t')) st->key.pop_back();

        if (st->key.empty()) {
          msgBox(hDlg, L"Key is required.", L"Edit setting", MB_ICONERROR);
          return TRUE;
        }

        st->ok = true;
        EndDialog(hDlg, IDOK);
        return TRUE;
      }

      if (LOWORD(wParam) == IDCANCEL) {
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
      }
      break;
    }
  }

  return FALSE;
}

struct EditSettingsDialogState {
  std::vector<std::pair<std::string, std::string>> settings; // key,value
  std::vector<std::string> knownKeys;
  bool ok = false;
};

static void settingsListAddColumns(HWND lv) {
  LVCOLUMNW col{};
  col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

  col.pszText = const_cast<wchar_t*>(L"Key");
  col.cx = 140;
  col.iSubItem = 0;
  ListView_InsertColumn(lv, 0, &col);

  col.pszText = const_cast<wchar_t*>(L"Value");
  col.cx = 120;
  col.iSubItem = 1;
  ListView_InsertColumn(lv, 1, &col);
}

static void settingsListPopulate(HWND lv, const std::vector<std::pair<std::string, std::string>>& settings) {
  ListView_DeleteAllItems(lv);
  int row = 0;
  for (const auto& kv : settings) {
    std::wstring k = utf8ToWide(kv.first);
    std::wstring v = utf8ToWide(kv.second);

    LVITEMW it{};
    it.mask = LVIF_TEXT;
    it.iItem = row;
    it.iSubItem = 0;
    it.pszText = const_cast<wchar_t*>(k.c_str());
    ListView_InsertItem(lv, &it);
    ListView_SetItemText(lv, row, 1, const_cast<wchar_t*>(v.c_str()));
    row++;
  }
}

static int settingsListSelectedIndex(HWND lv) {
  return ListView_GetNextItem(lv, -1, LVNI_SELECTED);
}

static void upsertSetting(std::vector<std::pair<std::string, std::string>>& vec, const std::string& key, const std::string& value) {
  // If key exists, update. Else insert.
  for (auto& kv : vec) {
    if (kv.first == key) {
      kv.second = value;
      return;
    }
  }
  vec.emplace_back(key, value);
}

static void sortSettings(std::vector<std::pair<std::string, std::string>>& vec) {
  std::sort(vec.begin(), vec.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
}

static INT_PTR CALLBACK EditSettingsDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
  EditSettingsDialogState* st = reinterpret_cast<EditSettingsDialogState*>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));

  auto refresh = [&]() {
    HWND lv = GetDlgItem(hDlg, IDC_SETTINGS_LIST);
    if (!lv || !st) return;
    sortSettings(st->settings);
    settingsListPopulate(lv, st->settings);
    ensureListViewHasSelection(lv);
  };

  switch (msg) {
    case WM_INITDIALOG: {
      st = reinterpret_cast<EditSettingsDialogState*>(lParam);
      SetWindowLongPtrW(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));

      HWND lv = GetDlgItem(hDlg, IDC_SETTINGS_LIST);
      if (lv) {
        installAccessibleNameForListView(lv, L"Language settings list");
        ListView_SetExtendedListViewStyle(lv, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        settingsListAddColumns(lv);
      }

      refresh();
      return TRUE;
    }

    case WM_NOTIFY: {
      NMHDR* hdr = reinterpret_cast<NMHDR*>(lParam);
      if (hdr && hdr->code == NM_SETFOCUS && hdr->idFrom == IDC_SETTINGS_LIST) {
        ensureListViewHasSelection(hdr->hwndFrom);
        return TRUE;
      }
      break;
    }

    case WM_COMMAND: {
      if (!st) break;

      if (LOWORD(wParam) == IDC_SETTINGS_ADD) {
        EditSettingDialogState ed;
        ed.key.clear();
        ed.value.clear();
        ed.knownKeys = st->knownKeys;
        DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_EDIT_SETTING), hDlg, EditSettingDlgProc, reinterpret_cast<LPARAM>(&ed));
        if (ed.ok) {
          upsertSetting(st->settings, ed.key, ed.value);
          refresh();
        }
        return TRUE;
      }

      if (LOWORD(wParam) == IDC_SETTINGS_EDIT) {
        HWND lv = GetDlgItem(hDlg, IDC_SETTINGS_LIST);
        int sel = lv ? settingsListSelectedIndex(lv) : -1;
        if (sel < 0 || sel >= static_cast<int>(st->settings.size())) {
          msgBox(hDlg, L"Select a setting first.", L"Language settings", MB_ICONINFORMATION);
          return TRUE;
        }

        EditSettingDialogState ed;
        ed.key = st->settings[static_cast<size_t>(sel)].first;
        ed.value = st->settings[static_cast<size_t>(sel)].second;
        ed.knownKeys = st->knownKeys;

        DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_EDIT_SETTING), hDlg, EditSettingDlgProc, reinterpret_cast<LPARAM>(&ed));
        if (ed.ok) {
          // Remove old entry (even if key changed), then upsert.
          st->settings.erase(st->settings.begin() + sel);
          upsertSetting(st->settings, ed.key, ed.value);
          refresh();
        }
        return TRUE;
      }

      if (LOWORD(wParam) == IDC_SETTINGS_REMOVE) {
        HWND lv = GetDlgItem(hDlg, IDC_SETTINGS_LIST);
        int sel = lv ? settingsListSelectedIndex(lv) : -1;
        if (sel < 0 || sel >= static_cast<int>(st->settings.size())) {
          msgBox(hDlg, L"Select a setting first.", L"Language settings", MB_ICONINFORMATION);
          return TRUE;
        }
        st->settings.erase(st->settings.begin() + sel);
        refresh();
        return TRUE;
      }

      if (LOWORD(wParam) == IDOK) {
        st->ok = true;
        EndDialog(hDlg, IDOK);
        return TRUE;
      }
      if (LOWORD(wParam) == IDCANCEL) {
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
      }
      break;
    }
  }

  return FALSE;
}
struct EditPhonemeDialogState {
  std::string phonemeKey;
  nvsp_editor::Node original;
  nvsp_editor::Node working;
  NvspRuntime* runtime = nullptr;
  bool ok = false;
};

static void listviewAddColumns(HWND lv) {
  LVCOLUMNW col{};
  col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

  col.pszText = const_cast<wchar_t*>(L"Field");
  col.cx = 140;
  col.iSubItem = 0;
  ListView_InsertColumn(lv, 0, &col);

  col.pszText = const_cast<wchar_t*>(L"Value");
  col.cx = 120;
  col.iSubItem = 1;
  ListView_InsertColumn(lv, 1, &col);
}

static std::vector<std::string> sortedNodeKeys(const nvsp_editor::Node& n) {
  std::vector<std::string> keys;
  if (!n.isMap()) return keys;
  keys.reserve(n.map.size());
  for (const auto& kv : n.map) keys.push_back(kv.first);
  std::sort(keys.begin(), keys.end());
  return keys;
}

static void populatePhonemeFieldsList(HWND lv, const nvsp_editor::Node& phonemeMap) {
  ListView_DeleteAllItems(lv);
  auto keys = sortedNodeKeys(phonemeMap);

  int row = 0;
  for (const auto& k : keys) {
    const auto& v = phonemeMap.map.at(k);
    if (!v.isScalar()) continue;

    LVITEMW item{};
    item.mask = LVIF_TEXT;
    item.iItem = row;
    item.iSubItem = 0;
    std::wstring wk = utf8ToWide(k);
    item.pszText = wk.data();
    ListView_InsertItem(lv, &item);

    std::wstring wv = utf8ToWide(v.scalar);
    ListView_SetItemText(lv, row, 1, const_cast<wchar_t*>(wv.c_str()));

    row++;
  }
}

static std::string getSelectedField(HWND lv, const nvsp_editor::Node& phonemeMap) {
  int sel = ListView_GetNextItem(lv, -1, LVNI_SELECTED);
  if (sel < 0) return {};

  wchar_t buf[512];
  ListView_GetItemText(lv, sel, 0, buf, 512);
  return wideToUtf8(buf);
}

static INT_PTR CALLBACK EditPhonemeDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
  EditPhonemeDialogState* st = reinterpret_cast<EditPhonemeDialogState*>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));

  switch (msg) {
    case WM_INITDIALOG: {
      st = reinterpret_cast<EditPhonemeDialogState*>(lParam);
      SetWindowLongPtrW(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));

      SetDlgItemTextW(hDlg, IDC_PHONEME_KEY_LABEL, (L"Phoneme: " + utf8ToWide(st->phonemeKey)).c_str());

      HWND lv = GetDlgItem(hDlg, IDC_PHONEME_FIELDS);
      if (lv) installAccessibleNameForListView(lv, L"Phoneme fields list");
      ListView_SetExtendedListViewStyle(lv, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
      listviewAddColumns(lv);
      populatePhonemeFieldsList(lv, st->working);
      ensureListViewHasSelection(lv);

      return TRUE;
    }

    case WM_COMMAND: {
      if (!st) break;

      if (LOWORD(wParam) == IDC_PHONEME_EDIT_VALUE) {
        HWND lv = GetDlgItem(hDlg, IDC_PHONEME_FIELDS);
        std::string field = getSelectedField(lv, st->working);
        if (field.empty()) {
          msgBox(hDlg, L"Select a field first.", L"Edit phoneme", MB_ICONINFORMATION);
          return TRUE;
        }

        auto it = st->working.map.find(field);
        if (it == st->working.map.end() || !it->second.isScalar()) {
          msgBox(hDlg, L"That field isn't a scalar value.", L"Edit phoneme", MB_ICONERROR);
          return TRUE;
        }

        EditValueDialogState vs;
        vs.field = field;
        vs.value = it->second.scalar;
        vs.baseMap = st->working;
        vs.runtime = st->runtime;
        vs.livePreview = true;

        DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_EDIT_VALUE), hDlg, EditValueDlgProc, reinterpret_cast<LPARAM>(&vs));
        if (vs.ok) {
          it->second.type = nvsp_editor::Node::Type::Scalar;
          it->second.scalar = vs.value;
          populatePhonemeFieldsList(lv, st->working);
          ensureListViewHasSelection(lv);
        }
        return TRUE;
      }

      if (LOWORD(wParam) == IDOK) {
        st->ok = true;
        EndDialog(hDlg, IDOK);
        return TRUE;
      }
      if (LOWORD(wParam) == IDCANCEL) {
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
      }
      break;
    }
  }

  return FALSE;
}

// -------------------------
// App state
// -------------------------
struct App {
  HINSTANCE hInst = nullptr;
  HWND wnd = nullptr;

  // Static labels (for screen-reader friendly names on controls).
  HWND lblFilter = nullptr;
  HWND lblAllPhonemes = nullptr;

  HWND lblLanguage = nullptr;
  HWND lblLangPhonemes = nullptr;
  HWND lblMappings = nullptr;

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

  PhonemesYaml phonemes;
  LanguageYaml language;
  std::vector<ReplacementRule> repls;
  std::vector<std::string> classNames;

  std::vector<std::string> phonemeKeys;
  std::vector<std::string> filteredPhonemeKeys;
  std::vector<std::u32string> phonemeKeysU32Sorted;

  std::vector<std::string> usedPhonemeKeys;

  NvspRuntime runtime;

  void setStatus(const std::wstring& text) {
    if (status) SendMessageW(status, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(text.c_str()));
  }
};

static App* g_app = nullptr;

// -------------------------
// UI list helpers
// -------------------------
static void lvClear(HWND lv) {
  ListView_DeleteAllItems(lv);
}

static void lvAddColumn(HWND lv, int idx, const wchar_t* text, int width) {
  LVCOLUMNW col{};
  col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
  col.pszText = const_cast<wchar_t*>(text);
  col.cx = width;
  col.iSubItem = idx;
  ListView_InsertColumn(lv, idx, &col);
}

static void lvAddRow2(HWND lv, int row, const std::wstring& c1, const std::wstring& c2) {
  LVITEMW it{};
  it.mask = LVIF_TEXT;
  it.iItem = row;
  it.iSubItem = 0;
  it.pszText = const_cast<wchar_t*>(c1.c_str());
  ListView_InsertItem(lv, &it);
  ListView_SetItemText(lv, row, 1, const_cast<wchar_t*>(c2.c_str()));
}

static void lvAddRow3(HWND lv, int row, const std::wstring& c1, const std::wstring& c2, const std::wstring& c3) {
  LVITEMW it{};
  it.mask = LVIF_TEXT;
  it.iItem = row;
  it.iSubItem = 0;
  it.pszText = const_cast<wchar_t*>(c1.c_str());
  ListView_InsertItem(lv, &it);
  ListView_SetItemText(lv, row, 1, const_cast<wchar_t*>(c2.c_str()));
  ListView_SetItemText(lv, row, 2, const_cast<wchar_t*>(c3.c_str()));
}

static int lvSelectedIndex(HWND lv) {
  return ListView_GetNextItem(lv, -1, LVNI_SELECTED);
}

static void ensureListViewHasSelection(HWND lv) {
  if (!lv) return;
  int count = ListView_GetItemCount(lv);
  if (count <= 0) return;

  int sel = lvSelectedIndex(lv);
  if (sel < 0) sel = 0;

  // Ensure something is both selected and focused so users don't tab into a
  // list that appears empty to assistive tech.
  ListView_SetItemState(lv, sel, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
  ListView_EnsureVisible(lv, sel, FALSE);
}

static std::string lvGetTextUtf8(HWND lv, int row, int col) {
  wchar_t buf[512];
  ListView_GetItemText(lv, row, col, buf, 512);
  return wideToUtf8(buf);
}

static std::string getSelectedPhonemeKey(HWND lv) {
  int sel = lvSelectedIndex(lv);
  if (sel < 0) return {};
  return lvGetTextUtf8(lv, sel, 0);
}

// -------------------------
// Data -> UI
// -------------------------
static void rebuildPhonemeKeysU32(App& app) {
  app.phonemeKeysU32Sorted.clear();
  app.phonemeKeysU32Sorted.reserve(app.phonemeKeys.size());
  for (const auto& k : app.phonemeKeys) {
    app.phonemeKeysU32Sorted.push_back(nvsp_frontend::utf8ToU32(k));
  }
  std::sort(app.phonemeKeysU32Sorted.begin(), app.phonemeKeysU32Sorted.end(), [](const auto& a, const auto& b) {
    if (a.size() != b.size()) return a.size() > b.size();
    return a < b;
  });
}

static std::vector<std::string> extractUsedPhonemes(const App& app, const std::vector<ReplacementRule>& repls) {
  std::unordered_set<std::string> used;

  for (const auto& r : repls) {
    std::u32string text = nvsp_frontend::utf8ToU32(r.to);
    size_t i = 0;
    while (i < text.size()) {
      char32_t c = text[i];
      if (c == U' ' || c == U'\t' || c == U'\n' || c == U'\r') {
        i++;
        continue;
      }

      bool matched = false;
      for (const auto& key : app.phonemeKeysU32Sorted) {
        if (key.empty()) continue;
        if (i + key.size() <= text.size() && std::equal(key.begin(), key.end(), text.begin() + static_cast<long long>(i))) {
          used.insert(nvsp_frontend::u32ToUtf8(key));
          i += key.size();
          matched = true;
          break;
        }
      }
      if (!matched) i++;
    }
  }

  std::vector<std::string> out(used.begin(), used.end());
  std::sort(out.begin(), out.end());
  return out;
}

static std::wstring whenToText(const ReplacementWhen& w) {
  std::wstring out;
  auto add = [&](const std::wstring& s) {
    if (!out.empty()) out += L", ";
    out += s;
  };
  if (w.atWordStart) add(L"wordStart");
  if (w.atWordEnd) add(L"wordEnd");
  if (!w.beforeClass.empty()) add(L"before=" + utf8ToWide(w.beforeClass));
  if (!w.afterClass.empty()) add(L"after=" + utf8ToWide(w.afterClass));
  return out;
}

static void populatePhonemeList(App& app, const std::wstring& filter) {
  app.filteredPhonemeKeys.clear();

  std::string filterUtf8 = wideToUtf8(filter);
  std::string filterLower;
  filterLower.reserve(filterUtf8.size());
  for (unsigned char c : filterUtf8) filterLower.push_back(static_cast<char>(std::tolower(c)));

  for (const auto& k : app.phonemeKeys) {
    if (filterLower.empty()) {
      app.filteredPhonemeKeys.push_back(k);
      continue;
    }
    std::string kl = k;
    for (auto& ch : kl) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (kl.find(filterLower) != std::string::npos) {
      app.filteredPhonemeKeys.push_back(k);
    }
  }

  lvClear(app.listPhonemes);
  int row = 0;
  for (const auto& k : app.filteredPhonemeKeys) {
    LVITEMW it{};
    it.mask = LVIF_TEXT;
    it.iItem = row;
    it.iSubItem = 0;
    std::wstring wk = utf8ToWide(k);
    it.pszText = wk.data();
    ListView_InsertItem(app.listPhonemes, &it);
    row++;
  }

  ensureListViewHasSelection(app.listPhonemes);
}

static void populateMappingsList(App& app) {
  lvClear(app.listMappings);
  int row = 0;
  for (const auto& r : app.repls) {
    lvAddRow3(app.listMappings, row, utf8ToWide(r.from), utf8ToWide(r.to), whenToText(r.when));
    row++;
  }

  ensureListViewHasSelection(app.listMappings);
}

static void populateLanguagePhonemesList(App& app) {
  lvClear(app.listLangPhonemes);
  int row = 0;
  for (const auto& k : app.usedPhonemeKeys) {
    LVITEMW it{};
    it.mask = LVIF_TEXT;
    it.iItem = row;
    it.iSubItem = 0;
    std::wstring wk = utf8ToWide(k);
    it.pszText = wk.data();
    ListView_InsertItem(app.listLangPhonemes, &it);
    row++;
  }

  ensureListViewHasSelection(app.listLangPhonemes);
}

static void refreshLanguageDerivedLists(App& app) {
  app.usedPhonemeKeys = extractUsedPhonemes(app, app.repls);
  populateMappingsList(app);
  populateLanguagePhonemesList(app);
}

// -------------------------
// Load packs
// -------------------------
static bool maybeCopyGoodPhonemesToExpected(HWND owner, const std::wstring& packsDir) {
  fs::path phonemes = fs::path(packsDir) / "phonemes.yaml";
  if (fs::exists(phonemes)) return true;

  fs::path good = fs::path(packsDir) / "phonemes-good.yaml";
  if (!fs::exists(good)) return false;

  int res = MessageBoxW(
    owner,
    L"packs/phonemes.yaml was not found, but packs/phonemes-good.yaml exists.\n\n"
    L"nvspFrontend.dll expects packs/phonemes.yaml.\n\n"
    L"Create a copy now?",
    L"NVSP Phoneme Editor",
    MB_YESNO | MB_ICONQUESTION
  );

  if (res != IDYES) return true; // allow editor to still work

  try {
    fs::copy_file(good, phonemes, fs::copy_options::overwrite_existing);
    return true;
  } catch (...) {
    msgBox(owner, L"Failed to copy phonemes-good.yaml to phonemes.yaml.", L"NVSP Phoneme Editor", MB_ICONERROR);
    return false;
  }
}

static bool loadPhonemes(App& app, const std::wstring& packsDir) {
  // Prefer packs/phonemes.yaml; fallback to packs/phonemes-good.yaml.
  fs::path p1 = fs::path(packsDir) / "phonemes.yaml";
  fs::path p2 = fs::path(packsDir) / "phonemes-good.yaml";

  fs::path use;
  if (fs::exists(p1)) use = p1;
  else if (fs::exists(p2)) use = p2;
  else return false;

  std::string err;
  if (!app.phonemes.load(use.u8string(), err)) {
    msgBox(app.wnd, L"Failed to load phonemes YAML:\n" + utf8ToWide(err), L"NVSP Phoneme Editor", MB_ICONERROR);
    return false;
  }

  app.phonemesPath = use.wstring();
  app.phonemeKeys = app.phonemes.phonemeKeysSorted();
  rebuildPhonemeKeysU32(app);

  std::wstring filter;
  wchar_t buf[512];
  GetWindowTextW(app.editFilter, buf, 512);
  filter = buf;
  populatePhonemeList(app, filter);

  return true;
}

static void populateLanguageCombo(App& app) {
  SendMessageW(app.comboLang, CB_RESETCONTENT, 0, 0);
  app.languageFiles.clear();

  fs::path dir(app.langDir);
  if (!fs::exists(dir)) return;

  std::vector<fs::path> files;
  for (auto& e : fs::directory_iterator(dir)) {
    if (!e.is_regular_file()) continue;
    auto p = e.path();
    if (p.extension() == ".yaml" || p.extension() == ".yml") {
      files.push_back(p);
    }
  }
  std::sort(files.begin(), files.end());

  int i = 0;
  for (const auto& p : files) {
    std::wstring name = p.filename().wstring();
    SendMessageW(app.comboLang, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
    app.languageFiles.push_back(p.wstring());
    i++;
  }

  // Try to restore previous.
  std::wstring last = readIni(L"state", L"lastLanguage", L"");
  int sel = 0;
  if (!last.empty()) {
    for (size_t idx = 0; idx < app.languageFiles.size(); ++idx) {
      if (fs::path(app.languageFiles[idx]).filename().wstring() == fs::path(last).filename().wstring()) {
        sel = static_cast<int>(idx);
        break;
      }
    }
  }

  SendMessageW(app.comboLang, CB_SETCURSEL, sel, 0);
}

static std::string selectedLangTagUtf8(const App& app) {
  int sel = static_cast<int>(SendMessageW(app.comboLang, CB_GETCURSEL, 0, 0));
  if (sel < 0 || sel >= static_cast<int>(app.languageFiles.size())) return {};
  fs::path p(app.languageFiles[static_cast<size_t>(sel)]);
  std::string stem = p.stem().u8string();
  return stem; // keep as-is; nvspFrontend normalizes internally
}

static bool loadLanguage(App& app, const std::wstring& langPath) {
  std::string err;
  if (!app.language.load(fs::path(langPath).u8string(), err)) {
    msgBox(app.wnd, L"Failed to load language YAML:\n" + utf8ToWide(err), L"NVSP Phoneme Editor", MB_ICONERROR);
    return false;
  }

  app.repls = app.language.replacements();
  app.classNames = app.language.classNamesSorted();

  refreshLanguageDerivedLists(app);

  // Update runtime language for TTS.
  std::string langTag = selectedLangTagUtf8(app);
  if (!langTag.empty() && app.runtime.dllsLoaded() && !app.packRoot.empty()) {
    std::string rtErr;
    app.runtime.setLanguage(langTag, rtErr);
    if (!rtErr.empty()) {
      // Soft error; don't block editing.
      app.setStatus(L"TTS warning: " + utf8ToWide(rtErr));
    }
  }

  writeIni(L"state", L"lastLanguage", fs::path(langPath).filename().wstring());

  return true;
}

static std::wstring runtimePackDir(const App& app) {
  if (!app.packsDir.empty()) return app.packsDir;
  if (!app.packRoot.empty()) {
    fs::path p(app.packRoot);
    p /= "packs";
    return p.wstring();
  }
  return {};
}

static bool loadPackRoot(App& app, const std::wstring& root) {
  if (root.empty()) return false;

  fs::path rootPath(root);
  fs::path packs = rootPath / "packs";
  if (!fs::exists(packs) || !fs::is_directory(packs)) {
    msgBox(app.wnd, L"That folder doesn't contain a 'packs' subfolder.", L"NVSP Phoneme Editor", MB_ICONERROR);
    return false;
  }

  app.packRoot = root;
  app.packsDir = packs.wstring();
  app.langDir = (packs / "lang").wstring();

  maybeCopyGoodPhonemesToExpected(app.wnd, app.packsDir);

  if (!loadPhonemes(app, app.packsDir)) {
    msgBox(app.wnd, L"Couldn't find phonemes.yaml or phonemes-good.yaml under packs/.", L"NVSP Phoneme Editor", MB_ICONERROR);
    return false;
  }

  populateLanguageCombo(app);

  // Load selected language.
  int sel = static_cast<int>(SendMessageW(app.comboLang, CB_GETCURSEL, 0, 0));
  if (sel >= 0 && sel < static_cast<int>(app.languageFiles.size())) {
    loadLanguage(app, app.languageFiles[static_cast<size_t>(sel)]);
  }

  // Point runtime at pack root.
  if (app.runtime.dllsLoaded()) {
    std::string rtErr;
    app.runtime.setPackRoot(runtimePackDir(app), rtErr);
  }

  writeIni(L"state", L"packRoot", app.packRoot);

  app.setStatus(L"Loaded packs from: " + app.packRoot);
  return true;
}

// -------------------------
// Audio actions
// -------------------------
static bool ensureDllDir(App& app) {
  if (app.runtime.dllsLoaded()) return true;

  std::wstring dllDir = app.dllDir;
  if (dllDir.empty()) {
    dllDir = readIni(L"paths", L"dllDir", L"");
  }
  if (dllDir.empty()) {
    // Try exe directory.
    dllDir = exeDir();
  }

  std::string err;
  if (!app.runtime.setDllDirectory(dllDir, err)) {
    msgBox(app.wnd, L"DLL load failed:\n" + utf8ToWide(err) + L"\n\nUse Settings > Set DLL directory...", L"NVSP Phoneme Editor", MB_ICONERROR);
    return false;
  }

  app.dllDir = dllDir;
  writeIni(L"paths", L"dllDir", app.dllDir);

  // Also set pack root on runtime.
  if (!app.packRoot.empty()) {
    std::string tmp;
    app.runtime.setPackRoot(runtimePackDir(app), tmp);
    std::string tmp2;
    std::string langTag = selectedLangTagUtf8(app);
    if (!langTag.empty()) app.runtime.setLanguage(langTag, tmp2);
  }

  return true;
}

static void playSamplesTemp(App& app, const std::vector<sample>& samples) {
  if (samples.empty()) {
    msgBox(app.wnd, L"No audio samples were generated.", L"NVSP Phoneme Editor", MB_ICONINFORMATION);
    return;
  }

  std::wstring wavPath = nvsp_editor::makeTempWavPath(L"nvp");
  std::string err;
  if (!nvsp_editor::writeWav16Mono(wavPath, kSampleRate, samples, err)) {
    msgBox(app.wnd, L"WAV write failed:\n" + utf8ToWide(err), L"NVSP Phoneme Editor", MB_ICONERROR);
    return;
  }

  PlaySoundW(wavPath.c_str(), NULL, SND_FILENAME | SND_ASYNC);
}

static void onPlaySelectedPhoneme(App& app, bool fromLanguageList) {
  if (!ensureDllDir(app)) return;

  std::string key = fromLanguageList ? getSelectedPhonemeKey(app.listLangPhonemes) : getSelectedPhonemeKey(app.listPhonemes);
  if (key.empty()) {
    msgBox(app.wnd, L"Select a phoneme first.", L"NVSP Phoneme Editor", MB_ICONINFORMATION);
    return;
  }

  nvsp_editor::Node* node = app.phonemes.getPhonemeNode(key);
  if (!node || !node->isMap()) {
    msgBox(app.wnd, L"Phoneme not found in phonemes.yaml.", L"NVSP Phoneme Editor", MB_ICONERROR);
    return;
  }

  std::vector<sample> samples;
  std::string err;
  if (!app.runtime.synthPreviewPhoneme(*node, kSampleRate, samples, err)) {
    msgBox(app.wnd, L"Preview failed:\n" + utf8ToWide(err), L"NVSP Phoneme Editor", MB_ICONERROR);
    return;
  }

  playSamplesTemp(app, samples);
}

// -------------------------
// Mapping operations
// -------------------------
static void onAddMapping(App& app, const std::string& defaultTo = {}) {
  if (!app.language.isLoaded()) {
    msgBox(app.wnd, L"Load a language first.", L"NVSP Phoneme Editor", MB_ICONINFORMATION);
    return;
  }

  AddMappingDialogState st;
  st.rule.to = defaultTo;
  st.classNames = app.classNames;

  DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_ADD_MAPPING), app.wnd, AddMappingDlgProc, reinterpret_cast<LPARAM>(&st));
  if (!st.ok) return;

  app.repls.push_back(st.rule);
  app.language.setReplacements(app.repls);
  refreshLanguageDerivedLists(app);
}

static void onEditSelectedMapping(App& app) {
  int sel = lvSelectedIndex(app.listMappings);
  if (sel < 0 || sel >= static_cast<int>(app.repls.size())) {
    msgBox(app.wnd, L"Select a mapping first.", L"NVSP Phoneme Editor", MB_ICONINFORMATION);
    return;
  }

  AddMappingDialogState st;
  st.rule = app.repls[static_cast<size_t>(sel)];
  st.classNames = app.classNames;

  DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_ADD_MAPPING), app.wnd, AddMappingDlgProc, reinterpret_cast<LPARAM>(&st));
  if (!st.ok) return;

  app.repls[static_cast<size_t>(sel)] = st.rule;
  app.language.setReplacements(app.repls);
  refreshLanguageDerivedLists(app);
}

static void onRemoveSelectedMapping(App& app) {
  int sel = lvSelectedIndex(app.listMappings);
  if (sel < 0 || sel >= static_cast<int>(app.repls.size())) {
    msgBox(app.wnd, L"Select a mapping first.", L"NVSP Phoneme Editor", MB_ICONINFORMATION);
    return;
  }

  app.repls.erase(app.repls.begin() + sel);
  app.language.setReplacements(app.repls);
  refreshLanguageDerivedLists(app);
}

// -------------------------
// Language settings
// -------------------------
static std::vector<std::string> knownLanguageSettingKeys() {
  static const char* kKeys[] = {
    "primaryStressDiv",
    "secondaryStressDiv",
    "postStopAspirationEnabled",
    "postStopAspirationPhoneme",
    "stopClosureMode",
    "stopClosureClusterGapsEnabled",
    "stopClosureAfterNasalsEnabled",
    "stopClosureVowelGapMs",
    "stopClosureVowelFadeMs",
    "stopClosureClusterGapMs",
    "stopClosureClusterFadeMs",
    "stopClosureWordBoundaryClusterGapMs",
    "stopClosureWordBoundaryClusterFadeMs",
    "lengthenedScale",
    "lengthenedScaleHu",
    "applyLengthenedScaleToVowelsOnly",
    "huShortAVowelEnabled",
    "huShortAVowelKey",
    "huShortAVowelScale",
    "englishLongUShortenEnabled",
    "englishLongUKey",
    "englishLongUWordFinalScale",
    "defaultPreFormantGain",
    "defaultOutputGain",
    "defaultVibratoPitchOffset",
    "defaultVibratoSpeed",
    "defaultVoiceTurbulenceAmplitude",
    "defaultGlottalOpenQuotient",
    "stripAllophoneDigits",
    "stripHyphen",
    "tonal",
    "toneDigitsEnabled",
    "toneContoursMode",
    "toneContoursAbsolute",
    "segmentBoundaryGapMs",
    "segmentBoundaryFadeMs",
    "segmentBoundarySkipVowelToVowel",
    "autoTieDiphthongs",
    "autoDiphthongOffglideToSemivowel"
  };

  std::vector<std::string> keys;
  keys.reserve(sizeof(kKeys) / sizeof(kKeys[0]));
  for (const char* k : kKeys) keys.emplace_back(k);
  std::sort(keys.begin(), keys.end());
  keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
  return keys;
}

// -------------------------
// Speech settings (voice + sliders)
// -------------------------

static nvsp_editor::SpeechSettings loadSpeechSettingsFromIni() {
  nvsp_editor::SpeechSettings s;
  s.voiceName = wideToUtf8(readIni(L"speech", L"voice", L"Adam"));
  s.rate = readIniInt(L"speech", L"rate", s.rate);
  s.pitch = readIniInt(L"speech", L"pitch", s.pitch);
  s.volume = readIniInt(L"speech", L"volume", s.volume);
  s.inflection = readIniInt(L"speech", L"inflection", s.inflection);

  const auto& names = NvspRuntime::frameParamNames();
  s.frameParams.assign(names.size(), 50);
  for (size_t i = 0; i < names.size(); ++i) {
    std::wstring key = L"frame_" + utf8ToWide(names[i]);
    s.frameParams[i] = readIniInt(L"speech", key.c_str(), 50);
  }
  return s;
}

static void saveSpeechSettingsToIni(const nvsp_editor::SpeechSettings& s) {
  writeIni(L"speech", L"voice", utf8ToWide(s.voiceName));
  writeIniInt(L"speech", L"rate", s.rate);
  writeIniInt(L"speech", L"pitch", s.pitch);
  writeIniInt(L"speech", L"volume", s.volume);
  writeIniInt(L"speech", L"inflection", s.inflection);

  const auto& names = NvspRuntime::frameParamNames();
  for (size_t i = 0; i < names.size() && i < s.frameParams.size(); ++i) {
    std::wstring key = L"frame_" + utf8ToWide(names[i]);
    writeIniInt(L"speech", key.c_str(), s.frameParams[i]);
  }
}

struct SpeechSettingsDialogState {
  nvsp_editor::SpeechSettings settings;
  std::vector<std::string> paramNames;
  bool ok = false;
};

static void setTrackbarRangeAndPos(HWND tb, int pos) {
  if (!tb) return;
  SendMessageW(tb, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
  SendMessageW(tb, TBM_SETTICFREQ, 10, 0);
  SendMessageW(tb, TBM_SETPOS, TRUE, pos);
}

static int getTrackbarPos(HWND tb) {
  if (!tb) return 0;
  return static_cast<int>(SendMessageW(tb, TBM_GETPOS, 0, 0));
}

static void setDlgIntText(HWND hDlg, int id, int value) {
  wchar_t buf[64];
  _itow_s(value, buf, 10);
  SetDlgItemTextW(hDlg, id, buf);
}

static void fillVoices(HWND combo, const std::string& selected) {
  if (!combo) return;
  SendMessageW(combo, CB_RESETCONTENT, 0, 0);
  const char* voices[] = {"Adam", "Benjamin", "Caleb", "David"};
  int sel = 0;
  for (int i = 0; i < 4; ++i) {
    std::wstring w = utf8ToWide(voices[i]);
    int idx = static_cast<int>(SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(w.c_str())));
    if (selected == voices[i]) sel = idx;
  }
  SendMessageW(combo, CB_SETCURSEL, sel, 0);
}

static void populateParamList(HWND list, const std::vector<std::string>& names, const std::vector<int>& values) {
  if (!list) return;
  SendMessageW(list, LB_RESETCONTENT, 0, 0);
  for (size_t i = 0; i < names.size(); ++i) {
    std::wstring text = utf8ToWide(names[i]) + L" (" + std::to_wstring((i < values.size()) ? values[i] : 50) + L")";
    SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
  }
  SendMessageW(list, LB_SETCURSEL, 0, 0);
}

static void refreshParamListRow(HWND list, size_t idx, const std::string& name, int value) {
  if (!list) return;
  std::wstring text = utf8ToWide(name) + L" (" + std::to_wstring(value) + L")";
  SendMessageW(list, LB_DELETESTRING, static_cast<WPARAM>(idx), 0);
  SendMessageW(list, LB_INSERTSTRING, static_cast<WPARAM>(idx), reinterpret_cast<LPARAM>(text.c_str()));
}

static INT_PTR CALLBACK SpeechSettingsDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
  SpeechSettingsDialogState* st = reinterpret_cast<SpeechSettingsDialogState*>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));

  auto syncSelectedParamToUi = [&]() {
    if (!st) return;
    HWND lb = GetDlgItem(hDlg, IDC_SPEECH_PARAM_LIST);
    int sel = lb ? static_cast<int>(SendMessageW(lb, LB_GETCURSEL, 0, 0)) : -1;
    if (sel < 0) sel = 0;
    if (sel >= static_cast<int>(st->paramNames.size())) return;
    int v = (sel < static_cast<int>(st->settings.frameParams.size())) ? st->settings.frameParams[static_cast<size_t>(sel)] : 50;
    HWND tb = GetDlgItem(hDlg, IDC_SPEECH_PARAM_SLIDER);
    setTrackbarRangeAndPos(tb, v);
    setDlgIntText(hDlg, IDC_SPEECH_PARAM_VAL, v);
  };

  switch (msg) {
    case WM_INITDIALOG: {
      st = reinterpret_cast<SpeechSettingsDialogState*>(lParam);
      SetWindowLongPtrW(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));

      // Accessible names for any ListView controls (none here), and predictable defaults.
      HWND combo = GetDlgItem(hDlg, IDC_SPEECH_VOICE);
      fillVoices(combo, st->settings.voiceName);

      setTrackbarRangeAndPos(GetDlgItem(hDlg, IDC_SPEECH_RATE_SLIDER), st->settings.rate);
      setDlgIntText(hDlg, IDC_SPEECH_RATE_VAL, st->settings.rate);

      setTrackbarRangeAndPos(GetDlgItem(hDlg, IDC_SPEECH_PITCH_SLIDER), st->settings.pitch);
      setDlgIntText(hDlg, IDC_SPEECH_PITCH_VAL, st->settings.pitch);

      setTrackbarRangeAndPos(GetDlgItem(hDlg, IDC_SPEECH_VOLUME_SLIDER), st->settings.volume);
      setDlgIntText(hDlg, IDC_SPEECH_VOLUME_VAL, st->settings.volume);

      setTrackbarRangeAndPos(GetDlgItem(hDlg, IDC_SPEECH_INFLECTION_SLIDER), st->settings.inflection);
      setDlgIntText(hDlg, IDC_SPEECH_INFLECTION_VAL, st->settings.inflection);

      // Param list
      HWND lb = GetDlgItem(hDlg, IDC_SPEECH_PARAM_LIST);
      populateParamList(lb, st->paramNames, st->settings.frameParams);
      syncSelectedParamToUi();
      return TRUE;
    }

    case WM_HSCROLL: {
      if (!st) break;
      HWND src = reinterpret_cast<HWND>(lParam);
      if (!src) break;

      const int id = GetDlgCtrlID(src);
      if (id == IDC_SPEECH_RATE_SLIDER) {
        st->settings.rate = getTrackbarPos(src);
        setDlgIntText(hDlg, IDC_SPEECH_RATE_VAL, st->settings.rate);
        return TRUE;
      }
      if (id == IDC_SPEECH_PITCH_SLIDER) {
        st->settings.pitch = getTrackbarPos(src);
        setDlgIntText(hDlg, IDC_SPEECH_PITCH_VAL, st->settings.pitch);
        return TRUE;
      }
      if (id == IDC_SPEECH_VOLUME_SLIDER) {
        st->settings.volume = getTrackbarPos(src);
        setDlgIntText(hDlg, IDC_SPEECH_VOLUME_VAL, st->settings.volume);
        return TRUE;
      }
      if (id == IDC_SPEECH_INFLECTION_SLIDER) {
        st->settings.inflection = getTrackbarPos(src);
        setDlgIntText(hDlg, IDC_SPEECH_INFLECTION_VAL, st->settings.inflection);
        return TRUE;
      }
      if (id == IDC_SPEECH_PARAM_SLIDER) {
        int v = getTrackbarPos(src);
        HWND lb = GetDlgItem(hDlg, IDC_SPEECH_PARAM_LIST);
        int sel = lb ? static_cast<int>(SendMessageW(lb, LB_GETCURSEL, 0, 0)) : -1;
        if (sel < 0) sel = 0;
        if (sel >= 0 && sel < static_cast<int>(st->settings.frameParams.size())) {
          st->settings.frameParams[static_cast<size_t>(sel)] = v;
          setDlgIntText(hDlg, IDC_SPEECH_PARAM_VAL, v);
          if (sel < static_cast<int>(st->paramNames.size())) {
            refreshParamListRow(lb, static_cast<size_t>(sel), st->paramNames[static_cast<size_t>(sel)], v);
            SendMessageW(lb, LB_SETCURSEL, sel, 0);
          }
        }
        return TRUE;
      }
      break;
    }

    case WM_COMMAND: {
      if (!st) break;
      const int id = LOWORD(wParam);
      const int code = HIWORD(wParam);

      if (id == IDC_SPEECH_VOICE && code == CBN_SELCHANGE) {
        HWND combo = GetDlgItem(hDlg, IDC_SPEECH_VOICE);
        int sel = combo ? static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0)) : -1;
        if (sel >= 0) {
          wchar_t buf[128];
          SendMessageW(combo, CB_GETLBTEXT, sel, reinterpret_cast<LPARAM>(buf));
          st->settings.voiceName = wideToUtf8(buf);
        }
        return TRUE;
      }

      if (id == IDC_SPEECH_PARAM_LIST && code == LBN_SELCHANGE) {
        syncSelectedParamToUi();
        return TRUE;
      }

      if (id == IDC_SPEECH_PARAM_RESET) {
        HWND lb = GetDlgItem(hDlg, IDC_SPEECH_PARAM_LIST);
        int sel = lb ? static_cast<int>(SendMessageW(lb, LB_GETCURSEL, 0, 0)) : -1;
        if (sel < 0) sel = 0;
        if (sel >= 0 && sel < static_cast<int>(st->settings.frameParams.size())) {
          st->settings.frameParams[static_cast<size_t>(sel)] = 50;
          setTrackbarRangeAndPos(GetDlgItem(hDlg, IDC_SPEECH_PARAM_SLIDER), 50);
          setDlgIntText(hDlg, IDC_SPEECH_PARAM_VAL, 50);
          if (sel < static_cast<int>(st->paramNames.size())) {
            refreshParamListRow(lb, static_cast<size_t>(sel), st->paramNames[static_cast<size_t>(sel)], 50);
            SendMessageW(lb, LB_SETCURSEL, sel, 0);
          }
        }
        return TRUE;
      }

      if (id == IDC_SPEECH_RESET_ALL) {
        st->settings.frameParams.assign(st->paramNames.size(), 50);
        st->settings.voiceName = st->settings.voiceName.empty() ? "Adam" : st->settings.voiceName;
        HWND lb = GetDlgItem(hDlg, IDC_SPEECH_PARAM_LIST);
        populateParamList(lb, st->paramNames, st->settings.frameParams);
        syncSelectedParamToUi();
        return TRUE;
      }

      if (id == IDOK) {
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

static void onEditLanguageSettings(App& app) {
  if (!app.language.isLoaded()) {
    msgBox(app.wnd, L"Load a language first.", L"NVSP Phoneme Editor", MB_ICONINFORMATION);
    return;
  }

  EditSettingsDialogState st;
  st.settings = app.language.settings();
  st.knownKeys = knownLanguageSettingKeys();

  DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_EDIT_SETTINGS), app.wnd, EditSettingsDlgProc, reinterpret_cast<LPARAM>(&st));
  if (!st.ok) return;

  app.language.setSettings(st.settings);
  app.setStatus(L"Edited language settings in memory. Use File > Save language YAML (Ctrl+S) to write it.");
}

// -------------------------
// Phoneme edits
// -------------------------
static void onClonePhoneme(App& app) {
  if (!app.phonemes.isLoaded()) return;

  std::string selected = getSelectedPhonemeKey(app.listPhonemes);

  ClonePhonemeDialogState st;
  st.keys = app.phonemeKeys;
  st.fromKey = selected;

  DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_CLONE_PHONEME), app.wnd, ClonePhonemeDlgProc, reinterpret_cast<LPARAM>(&st));
  if (!st.ok) return;

  std::string err;
  if (!app.phonemes.clonePhoneme(st.fromKey, st.newKey, err)) {
    msgBox(app.wnd, L"Clone failed:\n" + utf8ToWide(err), L"NVSP Phoneme Editor", MB_ICONERROR);
    return;
  }

  // Reload list.
  app.phonemeKeys = app.phonemes.phonemeKeysSorted();
  rebuildPhonemeKeysU32(app);
  populatePhonemeList(app, L"");

  msgBox(app.wnd, L"Cloned phoneme. Remember to save phonemes YAML.", L"NVSP Phoneme Editor", MB_ICONINFORMATION);
}

static void onEditSelectedPhoneme(App& app, bool fromLanguageList) {
  std::string key = fromLanguageList ? getSelectedPhonemeKey(app.listLangPhonemes) : getSelectedPhonemeKey(app.listPhonemes);
  if (key.empty()) {
    msgBox(app.wnd, L"Select a phoneme first.", L"NVSP Phoneme Editor", MB_ICONINFORMATION);
    return;
  }

  nvsp_editor::Node* node = app.phonemes.getPhonemeNode(key);
  if (!node || !node->isMap()) {
    msgBox(app.wnd, L"Phoneme not found in phonemes.yaml.", L"NVSP Phoneme Editor", MB_ICONERROR);
    return;
  }

  EditPhonemeDialogState st;
  st.phonemeKey = key;
  st.original = *node;
  st.working = *node;
  st.runtime = &app.runtime;
  st.runtime = &app.runtime;

  DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_EDIT_PHONEME), app.wnd, EditPhonemeDlgProc, reinterpret_cast<LPARAM>(&st));
  if (!st.ok) return;

  *node = st.working;
  msgBox(app.wnd, L"Phoneme updated. Remember to save phonemes YAML.", L"NVSP Phoneme Editor", MB_ICONINFORMATION);
}

// -------------------------
// Save YAML
// -------------------------
static void onSaveLanguage(App& app) {
  if (!app.language.isLoaded()) {
    msgBox(app.wnd, L"No language YAML loaded.", L"NVSP Phoneme Editor", MB_ICONINFORMATION);
    return;
  }
  std::string err;
  if (!app.language.save(err)) {
    msgBox(app.wnd, L"Save failed:\n" + utf8ToWide(err), L"NVSP Phoneme Editor", MB_ICONERROR);
    return;
  }
  app.setStatus(L"Saved language YAML");
}

static void onSavePhonemes(App& app) {
  if (!app.phonemes.isLoaded()) {
    msgBox(app.wnd, L"No phonemes YAML loaded.", L"NVSP Phoneme Editor", MB_ICONINFORMATION);
    return;
  }
  std::string err;
  if (!app.phonemes.save(err)) {
    msgBox(app.wnd, L"Save failed:\n" + utf8ToWide(err), L"NVSP Phoneme Editor", MB_ICONERROR);
    return;
  }
  app.setStatus(L"Saved phonemes YAML");
}

// -------------------------
// Text test
// -------------------------
static std::wstring getText(HWND hEdit) {
  int len = GetWindowTextLengthW(hEdit);
  std::wstring buf;
  buf.resize(static_cast<size_t>(len));
  GetWindowTextW(hEdit, buf.data(), len + 1);
  return buf;
}

static void setText(HWND hEdit, const std::wstring& text) {
  SetWindowTextW(hEdit, text.c_str());
}

static bool ensureEspeakDir(App& app) {
  if (!app.espeakDir.empty()) return true;
  app.espeakDir = readIni(L"paths", L"espeakDir", L"");
  return !app.espeakDir.empty();
}

static bool convertTextToIpaViaEspeak(App& app, const std::wstring& text, std::string& outIpaUtf8, std::string& outError) {
  outIpaUtf8.clear();
  outError.clear();

  if (!ensureEspeakDir(app)) {
    outError = "eSpeak directory is not set";
    return false;
  }

  std::string langTag = selectedLangTagUtf8(app);

  // Sanitize text for command-line invocation: make it single-line and trim.
  std::wstring safeText = text;
  for (wchar_t& c : safeText) {
    if (c == L'\r' || c == L'\n' || c == L'\t') c = L' ';
  }
  {
    std::wstring collapsed;
    collapsed.reserve(safeText.size());
    bool inSpace = true; // trim leading
    for (wchar_t c : safeText) {
      const bool isSpace = (c == L' ' || c == L'\v' || c == L'\f');
      if (isSpace) {
        if (!inSpace) collapsed.push_back(L' ');
        inSpace = true;
      } else {
        collapsed.push_back(c);
        inSpace = false;
      }
    }
    while (!collapsed.empty() && collapsed.back() == L' ') collapsed.pop_back();
    safeText.swap(collapsed);
  }

  std::wstring dataDir = nvsp_editor::findEspeakDataDir(app.espeakDir);

  // Prefer NVDA-compatible IPA from the eSpeak DLL if present.
  // This matches NVDA's use of espeak_TextToPhonemes() more closely than
  // command-line IPA flags, which can differ for some languages (e.g. Hungarian).
  {
    std::string dllErr;
    if (nvsp_editor::espeakTextToIpaViaDll(app.espeakDir, langTag, safeText, outIpaUtf8, dllErr)) {
      return true;
    }
  }

  // Fall back to spawning espeak-ng.exe / espeak.exe.
  std::wstring espeakExe = nvsp_editor::findEspeakExe(app.espeakDir);
  if (espeakExe.empty()) {
    outError = "Could not find espeak-ng.exe or espeak.exe in the configured directory";
    return false;
  }

  std::wstring wLang = utf8ToWide(langTag);

  // eSpeak args:
  //   -q           quiet
  //   --ipa=3      output IPA phonemes (level 3)
  //   -v <lang>    voice
  //   --path=...   force data directory when a packaged build uses a relative layout
  std::wstring args;
  args += L"-q ";
  if (!dataDir.empty()) {
    args += L"--path=\"" + dataDir + L"\" ";
  }
  args += L"--ipa=3 ";
  if (!wLang.empty()) {
    args += L"-v \"" + wLang + L"\" ";
  }
  args += L"\"" + safeText + L"\"";

  std::string stdoutUtf8;
  if (!nvsp_editor::runProcessCaptureStdout(espeakExe, args, stdoutUtf8, outError)) {
    return false;
  }

  // Trim ASCII whitespace from both ends.
  while (!stdoutUtf8.empty() && (stdoutUtf8.back() == '\r' || stdoutUtf8.back() == '\n' || stdoutUtf8.back() == ' ' || stdoutUtf8.back() == '\t')) {
    stdoutUtf8.pop_back();
  }
  size_t startWs = 0;
  while (startWs < stdoutUtf8.size() && (stdoutUtf8[startWs] == ' ' || stdoutUtf8[startWs] == '\t' || stdoutUtf8[startWs] == '\r' || stdoutUtf8[startWs] == '\n')) {
    startWs++;
  }

  outIpaUtf8 = stdoutUtf8.substr(startWs);
  return true;
}

static void onConvertIpa(App& app) {
  std::wstring text = getText(app.editText);
  if (text.empty()) {
    msgBox(app.wnd, L"Enter some text first.", L"NVSP Phoneme Editor", MB_ICONINFORMATION);
    return;
  }

  std::string ipa;
  std::string err;
  if (!convertTextToIpaViaEspeak(app, text, ipa, err)) {
    msgBox(app.wnd, L"IPA conversion failed:\n" + utf8ToWide(err) + L"\n\nTip: you can also tick 'Input is IPA' and paste IPA directly.", L"NVSP Phoneme Editor", MB_ICONERROR);
    return;
  }

  setText(app.editIpaOut, utf8ToWide(ipa));
  app.setStatus(L"Converted text to IPA via eSpeak");
}

static bool synthIpaFromUi(App& app, std::vector<sample>& outSamples, std::string& outError) {
  outSamples.clear();
  outError.clear();

  if (!ensureDllDir(app)) {
    outError = "DLLs not loaded";
    return false;
  }
  if (app.packRoot.empty()) {
    outError = "Pack root not loaded";
    return false;
  }

  // Ensure runtime pack root and language.
  std::string tmp;
  app.runtime.setPackRoot(runtimePackDir(app), tmp);
  std::string langTag = selectedLangTagUtf8(app);
  if (!langTag.empty()) {
    std::string errLang;
    app.runtime.setLanguage(langTag, errLang);
  }

  bool inputIsIpa = (SendMessageW(app.chkInputIsIpa, BM_GETCHECK, 0, 0) == BST_CHECKED);
  std::wstring text = getText(app.editText);
  if (text.empty()) {
    outError = "Input is empty";
    return false;
  }

  std::string ipa;
  if (inputIsIpa) {
    ipa = wideToUtf8(text);
  } else {
    std::string err;
    if (!convertTextToIpaViaEspeak(app, text, ipa, err)) {
      outError = err;
      return false;
    }
    setText(app.editIpaOut, utf8ToWide(ipa));
  }

  return app.runtime.synthIpa(ipa, kSampleRate, outSamples, outError);
}

static void onSpeak(App& app) {
  std::vector<sample> samples;
  std::string err;
  if (!synthIpaFromUi(app, samples, err)) {
    msgBox(app.wnd, L"Speak failed:\n" + utf8ToWide(err) + L"\n\nIf this mentions phonemes.yaml, make sure packs/phonemes.yaml exists.", L"NVSP Phoneme Editor", MB_ICONERROR);
    return;
  }
  playSamplesTemp(app, samples);
}

static void onSaveWav(App& app) {
  std::vector<sample> samples;
  std::string err;
  if (!synthIpaFromUi(app, samples, err)) {
    msgBox(app.wnd, L"Synthesis failed:\n" + utf8ToWide(err), L"NVSP Phoneme Editor", MB_ICONERROR);
    return;
  }

  std::wstring outPath;
  if (!pickSaveWav(app.wnd, outPath)) return;

  if (!nvsp_editor::writeWav16Mono(outPath, kSampleRate, samples, err)) {
    msgBox(app.wnd, L"WAV write failed:\n" + utf8ToWide(err), L"NVSP Phoneme Editor", MB_ICONERROR);
    return;
  }
  app.setStatus(L"Saved WAV: " + outPath);
}

// -------------------------
// Window proc
// -------------------------
static void layout(App& app, int w, int h) {
  const int margin = 8;
  const int statusH = 20;
  // Bottom panel contains two labeled multi-line edits + a control row.
  // Give it a bit more space so we don't overlap when labels are present.
  const int bottomH = 230;

  int usableH = h - statusH;
  int topH = usableH - bottomH - margin;
  if (topH < 200) topH = 200;

  int leftW = (w - margin * 3) / 2;
  int rightW = w - margin * 3 - leftW;

  // Left panel
  int xL = margin;
  int y = margin;

  const int labelH = 18;
  const int labelGap = 2;

  MoveWindow(app.lblFilter, xL, y, leftW, labelH, TRUE);
  y += labelH + labelGap;
  MoveWindow(app.editFilter, xL, y, leftW, 22, TRUE);
  y += 22 + margin;

  int btnRowH = 26;
  int btnAreaH = btnRowH + margin;

  MoveWindow(app.lblAllPhonemes, xL, y, leftW, labelH, TRUE);
  y += labelH + labelGap;
  MoveWindow(app.listPhonemes, xL, y, leftW, topH - y - btnAreaH + margin, TRUE);

  int btnY = topH - btnRowH + margin;
  int btnW = (leftW - margin * 3) / 4;
  MoveWindow(app.btnPlay, xL, btnY, btnW, btnRowH, TRUE);
  MoveWindow(app.btnClone, xL + (btnW + margin), btnY, btnW, btnRowH, TRUE);
  MoveWindow(app.btnEdit, xL + (btnW + margin) * 2, btnY, btnW, btnRowH, TRUE);
  MoveWindow(app.btnAddToLang, xL + (btnW + margin) * 3, btnY, btnW, btnRowH, TRUE);

  // Right panel
  int xR = xL + leftW + margin;
  int yR = margin;

  MoveWindow(app.lblLanguage, xR, yR, rightW, labelH, TRUE);
  yR += labelH + labelGap;
  MoveWindow(app.comboLang, xR, yR, rightW, 200, TRUE);
  yR += 26 + margin;

  MoveWindow(app.lblLangPhonemes, xR, yR, rightW, labelH, TRUE);
  yR += labelH + labelGap;
  int langPhH = 90;
  MoveWindow(app.listLangPhonemes, xR, yR, rightW, langPhH, TRUE);

  int langBtnW = (rightW - margin * 2) / 3;
  int langBtnY = yR + langPhH + margin;
  MoveWindow(app.btnLangPlay, xR, langBtnY, langBtnW, btnRowH, TRUE);
  MoveWindow(app.btnLangEdit, xR + (langBtnW + margin), langBtnY, langBtnW, btnRowH, TRUE);
  MoveWindow(app.btnLangSettings, xR + (langBtnW + margin) * 2, langBtnY, langBtnW, btnRowH, TRUE);

  int mapY = langBtnY + btnRowH + margin;
  int mapBtnH = btnRowH;
  int mapBtnAreaH = mapBtnH + margin;

  MoveWindow(app.lblMappings, xR, mapY, rightW, labelH, TRUE);
  mapY += labelH + labelGap;
  MoveWindow(app.listMappings, xR, mapY, rightW, topH - mapY - mapBtnAreaH + margin, TRUE);

  int mapBtnY = topH - mapBtnH + margin;
  int mapBtnW = (rightW - margin * 2) / 3;
  MoveWindow(app.btnAddMap, xR, mapBtnY, mapBtnW, mapBtnH, TRUE);
  MoveWindow(app.btnEditMap, xR + mapBtnW + margin, mapBtnY, mapBtnW, mapBtnH, TRUE);
  MoveWindow(app.btnRemoveMap, xR + (mapBtnW + margin) * 2, mapBtnY, mapBtnW, mapBtnH, TRUE);

  // Bottom panel
  int bottomY = topH + margin * 2;
  int bottomW = w - margin * 2;

  MoveWindow(app.lblText, margin, bottomY, bottomW, labelH, TRUE);
  bottomY += labelH + labelGap;
  MoveWindow(app.editText, margin, bottomY, bottomW, 70, TRUE);

  int controlsY = bottomY + 70 + margin;
  MoveWindow(app.chkInputIsIpa, margin, controlsY, 120, 22, TRUE);
  MoveWindow(app.btnConvertIpa, margin + 130, controlsY, 140, 22, TRUE);
  MoveWindow(app.btnSpeak, margin + 280, controlsY, 120, 22, TRUE);
  MoveWindow(app.btnSaveWav, margin + 410, controlsY, 120, 22, TRUE);

  int ipaLabelY = controlsY + 22 + margin;
  MoveWindow(app.lblIpaOut, margin, ipaLabelY, bottomW, labelH, TRUE);
  int ipaY = ipaLabelY + labelH + labelGap;
  MoveWindow(app.editIpaOut, margin, ipaY, bottomW, 70, TRUE);

  // Status
  MoveWindow(app.status, 0, h - statusH, w, statusH, TRUE);
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  App& app = *g_app;

  switch (msg) {
    case WM_CREATE: {
      app.wnd = hWnd;

      // Controls
      app.lblFilter = CreateWindowW(L"STATIC", L"Filter phonemes:", WS_CHILD | WS_VISIBLE,
                                   0, 0, 100, 18, hWnd, nullptr, app.hInst, nullptr);
      app.editFilter = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                       0, 0, 100, 22, hWnd, (HMENU)IDC_EDIT_FILTER, app.hInst, nullptr);
      // Provide a cue banner as a fallback name/description for screen readers
      // that don't associate this edit with the adjacent STATIC label.
      SendMessageW(app.editFilter, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Filter phonemes"));

      app.lblAllPhonemes = CreateWindowW(L"STATIC", L"All phonemes:", WS_CHILD | WS_VISIBLE,
                                        0, 0, 100, 18, hWnd, nullptr, app.hInst, nullptr);

      app.listPhonemes = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"All phonemes", WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL,
                                         0, 0, 100, 100, hWnd, (HMENU)IDC_LIST_PHONEMES, app.hInst, nullptr);
      installAccessibleNameForListView(app.listPhonemes, L"All phonemes list");
      ListView_SetExtendedListViewStyle(app.listPhonemes, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
      lvAddColumn(app.listPhonemes, 0, L"All phonemes", 160);

      app.btnPlay = CreateWindowW(L"BUTTON", L"Play", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                 0, 0, 80, 24, hWnd, (HMENU)IDC_BTN_PLAY_PHONEME, app.hInst, nullptr);
      app.btnClone = CreateWindowW(L"BUTTON", L"Clone...", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                  0, 0, 80, 24, hWnd, (HMENU)IDC_BTN_CLONE_PHONEME, app.hInst, nullptr);
      app.btnEdit = CreateWindowW(L"BUTTON", L"Edit...", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                 0, 0, 80, 24, hWnd, (HMENU)IDC_BTN_EDIT_PHONEME, app.hInst, nullptr);
      app.btnAddToLang = CreateWindowW(L"BUTTON", L"Add to language...", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                      0, 0, 120, 24, hWnd, (HMENU)IDC_BTN_ADD_TO_LANGUAGE, app.hInst, nullptr);

      app.lblLanguage = CreateWindowW(L"STATIC", L"Language:", WS_CHILD | WS_VISIBLE,
                                     0, 0, 100, 18, hWnd, nullptr, app.hInst, nullptr);
      app.comboLang = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                                     0, 0, 100, 200, hWnd, (HMENU)IDC_COMBO_LANGUAGE, app.hInst, nullptr);

      app.lblLangPhonemes = CreateWindowW(L"STATIC", L"Phonemes in language:", WS_CHILD | WS_VISIBLE,
                                        0, 0, 100, 18, hWnd, nullptr, app.hInst, nullptr);

      app.listLangPhonemes = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"Phonemes in language", WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL,
                                            0, 0, 100, 100, hWnd, (HMENU)IDC_LIST_LANG_PHONEMES, app.hInst, nullptr);
      installAccessibleNameForListView(app.listLangPhonemes, L"Phonemes in language list");
      ListView_SetExtendedListViewStyle(app.listLangPhonemes, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
      lvAddColumn(app.listLangPhonemes, 0, L"Language phonemes", 160);

      app.btnLangPlay = CreateWindowW(L"BUTTON", L"Play", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                     0, 0, 80, 24, hWnd, (HMENU)IDC_BTN_LANG_PLAY_PHONEME, app.hInst, nullptr);
      app.btnLangEdit = CreateWindowW(L"BUTTON", L"Edit phoneme...", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                     0, 0, 120, 24, hWnd, (HMENU)IDC_BTN_LANG_EDIT_PHONEME, app.hInst, nullptr);
      app.btnLangSettings = CreateWindowW(L"BUTTON", L"Language settings...", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                          0, 0, 140, 24, hWnd, (HMENU)IDC_BTN_LANG_SETTINGS, app.hInst, nullptr);

      app.lblMappings = CreateWindowW(L"STATIC", L"Normalization mappings:", WS_CHILD | WS_VISIBLE,
                                   0, 0, 160, 18, hWnd, nullptr, app.hInst, nullptr);

      app.listMappings = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"Normalization mappings", WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL,
                                        0, 0, 100, 100, hWnd, (HMENU)IDC_LIST_MAPPINGS, app.hInst, nullptr);
      installAccessibleNameForListView(app.listMappings, L"Normalization mappings list");
      ListView_SetExtendedListViewStyle(app.listMappings, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
      lvAddColumn(app.listMappings, 0, L"From", 120);
      lvAddColumn(app.listMappings, 1, L"To", 120);
      lvAddColumn(app.listMappings, 2, L"When", 180);

      app.btnAddMap = CreateWindowW(L"BUTTON", L"Add mapping...", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                  0, 0, 120, 24, hWnd, (HMENU)IDC_BTN_ADD_MAPPING, app.hInst, nullptr);
      app.btnEditMap = CreateWindowW(L"BUTTON", L"Edit mapping...", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                   0, 0, 120, 24, hWnd, (HMENU)IDC_BTN_EDIT_MAPPING, app.hInst, nullptr);
      app.btnRemoveMap = CreateWindowW(L"BUTTON", L"Remove mapping", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                     0, 0, 120, 24, hWnd, (HMENU)IDC_BTN_REMOVE_MAPPING, app.hInst, nullptr);

      app.lblText = CreateWindowW(L"STATIC", L"Input text:", WS_CHILD | WS_VISIBLE,
                                 0, 0, 100, 18, hWnd, nullptr, app.hInst, nullptr);
      app.editText = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL,
                                    0, 0, 100, 70, hWnd, (HMENU)IDC_EDIT_TEXT, app.hInst, nullptr);
      // Cue banner may not render for multi-line EDIT on all Windows versions,
      // but it helps where supported and is also exposed to some AT.
      SendMessageW(app.editText, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Type text to speak or convert to IPA"));
      SendMessageW(app.editText, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Type text to speak (or IPA if checked)"));

      app.chkInputIsIpa = CreateWindowW(L"BUTTON", L"Input is IPA", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                       0, 0, 120, 22, hWnd, (HMENU)IDC_CHK_INPUT_IS_IPA, app.hInst, nullptr);

      app.btnConvertIpa = CreateWindowW(L"BUTTON", L"Convert to IPA", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                       0, 0, 120, 22, hWnd, (HMENU)IDC_BTN_CONVERT_IPA, app.hInst, nullptr);
      app.btnSpeak = CreateWindowW(L"BUTTON", L"Speak", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                  0, 0, 120, 22, hWnd, (HMENU)IDC_BTN_SPEAK, app.hInst, nullptr);
      app.btnSaveWav = CreateWindowW(L"BUTTON", L"Save WAV...", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                    0, 0, 120, 22, hWnd, (HMENU)IDC_BTN_SAVE_WAV, app.hInst, nullptr);

      app.lblIpaOut = CreateWindowW(L"STATIC", L"IPA output:", WS_CHILD | WS_VISIBLE,
                                   0, 0, 100, 18, hWnd, nullptr, app.hInst, nullptr);
      app.editIpaOut = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_READONLY,
                                      0, 0, 100, 70, hWnd, (HMENU)IDC_EDIT_IPA, app.hInst, nullptr);
      SendMessageW(app.editIpaOut, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"IPA output appears here"));

      app.status = CreateWindowExW(0, STATUSCLASSNAMEW, nullptr, WS_CHILD | WS_VISIBLE,
                                  0, 0, 0, 0, hWnd, nullptr, app.hInst, nullptr);

      // Load persisted paths.
      app.packRoot = readIni(L"state", L"packRoot", L"");
      app.espeakDir = readIni(L"paths", L"espeakDir", L"");
      app.dllDir = readIni(L"paths", L"dllDir", L"");

      // Try to auto-detect a portable layout when paths are missing.
      // This is silent by design: we only show errors when the user attempts
      // to synthesize and something is still misconfigured.
      auto dirHasDlls = [](const std::wstring& dir) -> bool {
        std::error_code ec;
        fs::path p(dir);
        return fs::exists(p / "speechPlayer.dll", ec) && fs::exists(p / "nvspFrontend.dll", ec);
      };
      auto rootHasPacks = [](const std::wstring& root) -> bool {
        std::error_code ec;
        return fs::is_directory(fs::path(root) / "packs", ec);
      };
      auto detectEspeakDir = [](const std::wstring& baseDir) -> std::wstring {
        const std::wstring sep = (!baseDir.empty() && baseDir.back() == L'\\') ? L"" : L"\\";
        const std::wstring cands[] = {
          baseDir,
          baseDir + sep + L"espeak",
          baseDir + sep + L"espeak ng",
          baseDir + sep + L"espeak ng\\bin",
        };
        for (const auto& d : cands) {
          if (d.empty()) continue;
          std::error_code ec;
          fs::path p(d);
          if (fs::exists(p / "espeak-ng.exe", ec) || fs::exists(p / "espeak.exe", ec)) {
            return d;
          }
        }
        return {};
      };

      // Auto-load DLLs if they live next to the EXE.
      if (app.dllDir.empty()) {
        std::wstring base = exeDir();
        if (dirHasDlls(base)) {
          std::string err;
          if (app.runtime.setDllDirectory(base, err)) {
            app.dllDir = base;
            writeIni(L"paths", L"dllDir", app.dllDir);
          }
        }
      } else {
        // Best-effort load (silent).
        std::string err;
        app.runtime.setDllDirectory(app.dllDir, err);
      }

      // Auto-detect a bundled eSpeak directory.
      if (app.espeakDir.empty()) {
        std::wstring es = detectEspeakDir(exeDir());
        if (!es.empty()) {
          app.espeakDir = es;
          writeIni(L"paths", L"espeakDir", app.espeakDir);
        }
      }

      // If packRoot isn't set yet, try the DLL dir (common portable layout)
      // and then the EXE dir.
      if (app.packRoot.empty()) {
        if (!app.dllDir.empty() && rootHasPacks(app.dllDir)) {
          app.packRoot = app.dllDir;
        } else {
          std::wstring base = exeDir();
          if (rootHasPacks(base)) {
            app.packRoot = base;
          }
        }
      }

      // Load speech settings (voice + sliders) and apply to runtime.
      app.runtime.setSpeechSettings(loadSpeechSettingsFromIni());

      // Initial layout.
      RECT rc{};
      GetClientRect(hWnd, &rc);
      layout(app, rc.right - rc.left, rc.bottom - rc.top);

      if (!app.packRoot.empty()) {
        loadPackRoot(app, app.packRoot);
      } else {
        app.setStatus(L"Use File > Open pack root... to begin.");
      }

      return 0;
    }

    case WM_SIZE: {
      int w = LOWORD(lParam);
      int h = HIWORD(lParam);
      layout(app, w, h);
      return 0;
    }

    case WM_COMMAND: {
      const int id = LOWORD(wParam);
      const int code = HIWORD(wParam);

      // Some accessibility actions (e.g., UIA Invoke from a screen reader's
      // object navigation) can activate a control without moving keyboard
      // focus. That makes the UI feel like focus "disappeared" after pressing
      // a button. If the message originated from a control, ensure focus is on
      // that control.
      HWND hwndCtl = reinterpret_cast<HWND>(lParam);
      if (hwndCtl && IsWindow(hwndCtl)) {
        // Only force-focus on explicit *invocation* events (typically button
        // clicks). Many controls (especially EDIT) send WM_COMMAND
        // notifications like EN_CHANGE / EN_KILLFOCUS, and forcing focus for
        // those will trap keyboard navigation (Tab can't escape).
        if (code == 0) {
          wchar_t cls[64] = {0};
          GetClassNameW(hwndCtl, cls, 64);
          if (_wcsicmp(cls, L"Button") == 0) {
            SetFocus(hwndCtl);
          }
        }
      }

      if (id == IDM_FILE_OPEN_PACKROOT) {
        std::wstring folder;
        if (pickFolder(hWnd, L"Select the folder that contains 'packs'", folder)) {
          loadPackRoot(app, folder);
        }
        return 0;
      }
      if (id == IDM_FILE_SAVE_LANGUAGE) {
        onSaveLanguage(app);
        return 0;
      }
      if (id == IDM_FILE_SAVE_PHONEMES) {
        onSavePhonemes(app);
        return 0;
      }
      if (id == IDM_FILE_EXIT) {
        DestroyWindow(hWnd);
        return 0;
      }

      if (id == IDM_SETTINGS_ESPEAK_DIR) {
        std::wstring folder;
        if (pickFolder(hWnd, L"Select eSpeak directory (contains espeak-ng.exe or espeak.exe)", folder)) {
          app.espeakDir = folder;
          writeIni(L"paths", L"espeakDir", app.espeakDir);
          app.setStatus(L"eSpeak directory set.");
        }
        return 0;
      }
      if (id == IDM_SETTINGS_DLL_DIR) {
        std::wstring folder;
        if (pickFolder(hWnd, L"Select DLL directory (contains speechPlayer.dll and nvspFrontend.dll)", folder)) {
          app.dllDir = folder;
          writeIni(L"paths", L"dllDir", app.dllDir);
          // Try loading immediately.
          std::string err;
          if (!app.runtime.setDllDirectory(app.dllDir, err)) {
            msgBox(hWnd, L"DLL load failed:\n" + utf8ToWide(err), L"NVSP Phoneme Editor", MB_ICONERROR);
          } else {
            app.setStatus(L"DLL directory set and loaded.");
            // Convenience: if packs live alongside the DLLs (portable layout),
            // automatically treat this folder as the pack root.
            if (app.packRoot.empty()) {
              std::error_code ec;
              if (fs::is_directory(fs::path(folder) / "packs", ec)) {
                loadPackRoot(app, folder);
              }
            }
            if (!app.packRoot.empty()) {
              std::string tmp;
              app.runtime.setPackRoot(runtimePackDir(app), tmp);
              std::string lt = selectedLangTagUtf8(app);
              if (!lt.empty()) {
                std::string tmp2;
                app.runtime.setLanguage(lt, tmp2);
              }
            }
          }
        }
        return 0;
      }

      if (id == IDM_SETTINGS_SPEECH_SETTINGS) {
        SpeechSettingsDialogState st;
        st.settings = app.runtime.getSpeechSettings();
        st.paramNames = std::vector<std::string>(NvspRuntime::frameParamNames().begin(), NvspRuntime::frameParamNames().end());
        if (st.settings.frameParams.size() != st.paramNames.size()) {
          st.settings.frameParams.assign(st.paramNames.size(), 50);
        }

        DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_SPEECH_SETTINGS), hWnd, SpeechSettingsDlgProc, reinterpret_cast<LPARAM>(&st));
        if (st.ok) {
          app.runtime.setSpeechSettings(st.settings);
          saveSpeechSettingsToIni(st.settings);
          app.setStatus(L"Updated speech settings.");
        }
        return 0;
      }

      if (id == IDM_HELP_ABOUT) {
        msgBox(hWnd,
               L"NV Speech Player Phoneme Editor (Win32)\n\n"
               L"Keyboard shortcuts:\n"
               L"  Ctrl+O  Open pack root\n"
               L"  Ctrl+S  Save language YAML\n\n"
               L"Notes:\n"
               L"  - This editor rewrites YAML (comments are not preserved).\n"
               L"  - Preview audio uses speechPlayer.dll.\n"
               L"  - Text->IPA uses eSpeak if configured.",
               L"About", MB_OK | MB_ICONINFORMATION);
        return 0;
      }

      // Control notifications
      if (id == IDC_EDIT_FILTER && code == EN_CHANGE) {
        wchar_t buf[512];
        GetWindowTextW(app.editFilter, buf, 512);
        populatePhonemeList(app, buf);
        return 0;
      }

      if (id == IDC_COMBO_LANGUAGE && code == CBN_SELCHANGE) {
        int sel = static_cast<int>(SendMessageW(app.comboLang, CB_GETCURSEL, 0, 0));
        if (sel >= 0 && sel < static_cast<int>(app.languageFiles.size())) {
          loadLanguage(app, app.languageFiles[static_cast<size_t>(sel)]);
        }
        return 0;
      }

      switch (id) {
        case IDC_BTN_PLAY_PHONEME:
          onPlaySelectedPhoneme(app, false);
          return 0;
        case IDC_BTN_CLONE_PHONEME:
          onClonePhoneme(app);
          return 0;
        case IDC_BTN_EDIT_PHONEME:
          onEditSelectedPhoneme(app, false);
          return 0;
        case IDC_BTN_ADD_TO_LANGUAGE: {
          std::string key = getSelectedPhonemeKey(app.listPhonemes);
          if (key.empty()) {
            msgBox(hWnd, L"Select a phoneme first.", L"NVSP Phoneme Editor", MB_ICONINFORMATION);
            return 0;
          }
          onAddMapping(app, key);
          return 0;
        }
        case IDC_BTN_LANG_PLAY_PHONEME:
          onPlaySelectedPhoneme(app, true);
          return 0;
        case IDC_BTN_LANG_EDIT_PHONEME:
          onEditSelectedPhoneme(app, true);
          return 0;
        case IDC_BTN_LANG_SETTINGS:
          onEditLanguageSettings(app);
          return 0;
        case IDC_BTN_ADD_MAPPING:
          onAddMapping(app);
          return 0;
        case IDC_BTN_EDIT_MAPPING:
          onEditSelectedMapping(app);
          return 0;
        case IDC_BTN_REMOVE_MAPPING:
          onRemoveSelectedMapping(app);
          return 0;
        case IDC_BTN_CONVERT_IPA:
          onConvertIpa(app);
          return 0;
        case IDC_BTN_SPEAK:
          onSpeak(app);
          return 0;
        case IDC_BTN_SAVE_WAV:
          onSaveWav(app);
          return 0;
        default:
          break;
      }

      break;
    }

    case WM_NOTIFY: {
      NMHDR* hdr = reinterpret_cast<NMHDR*>(lParam);
      if (hdr && hdr->code == NM_SETFOCUS) {
        wchar_t cls[64] = {0};
        GetClassNameW(hdr->hwndFrom, cls, 64);
        if (_wcsicmp(cls, WC_LISTVIEWW) == 0 || _wcsicmp(cls, L"SysListView32") == 0) {
          ensureListViewHasSelection(hdr->hwndFrom);
        }
      }
      return 0;
    }

    case WM_CLOSE:
      DestroyWindow(hWnd);
      return 0;

    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }

  return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// -------------------------
// Keyboard focus / tab order support
//
// This app uses a normal top-level window (not a dialog). In that setup, the
// Win32 dialog manager does NOT automatically move focus between WS_TABSTOP
// controls when the user presses Tab / Shift+Tab.
//
// We implement a small, predictable tab-navigation handler here so all
// controls are reachable by keyboard, which is important for screen readers.
static bool handleTabNavigation(HWND hWnd, const MSG& msg) {
  if (msg.message != WM_KEYDOWN || msg.wParam != VK_TAB) return false;

  // Only handle Tab when the message is destined for our main window or one of
  // its child controls.
  if (!(msg.hwnd == hWnd || IsChild(hWnd, msg.hwnd))) return false;

  const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

  HWND focused = GetFocus();
  if (!(focused && (focused == hWnd || IsChild(hWnd, focused)))) {
    focused = nullptr;
  }

  HWND next = GetNextDlgTabItem(hWnd, focused, shift ? TRUE : FALSE);

  // Defensive fallback: in case the dialog-manager helper doesn't return a
  // control (it is documented for dialogs, though it generally works for
  // any parent window), we enumerate WS_TABSTOP children manually.
  if (!next) {
    std::vector<HWND> tabStops;
    for (HWND child = GetWindow(hWnd, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
      LONG style = GetWindowLongW(child, GWL_STYLE);
      if ((style & WS_TABSTOP) && (style & WS_VISIBLE) && IsWindowEnabled(child)) {
        tabStops.push_back(child);
      }
    }

    // Child enumeration returns windows in Z-order (topmost first). Tab order
    // is generally the reverse (older controls first), so reverse to keep a
    // natural, creation-order traversal.
    std::reverse(tabStops.begin(), tabStops.end());

    if (tabStops.empty()) return false;

    auto it = std::find(tabStops.begin(), tabStops.end(), focused);
    if (it == tabStops.end()) {
      next = shift ? tabStops.back() : tabStops.front();
    } else {
      const ptrdiff_t idx = it - tabStops.begin();
      const ptrdiff_t n = static_cast<ptrdiff_t>(tabStops.size());
      const ptrdiff_t nextIdx = shift ? ((idx - 1 + n) % n) : ((idx + 1) % n);
      next = tabStops[static_cast<size_t>(nextIdx)];
    }
  }

  if (!next) return false;

  SetFocus(next);
  return true;
}

// Enable Ctrl+A (Select All) in EDIT controls.
// The standard Win32 EDIT control does not implement this shortcut by default,
// so we provide it to make text selection predictable.
static bool handleCtrlASelectAll(HWND hWnd, const MSG& msg) {
  if (msg.message != WM_KEYDOWN) return false;
  if ((GetKeyState(VK_CONTROL) & 0x8000) == 0) return false;
  if (msg.wParam != 'A' && msg.wParam != 'a') return false;

  // Only handle when the focused control is one of our children.
  HWND focused = GetFocus();
  if (!(focused && (focused == hWnd || IsChild(hWnd, focused)))) return false;

  wchar_t cls[32] = {0};
  GetClassNameW(focused, cls, 32);
  if (_wcsicmp(cls, L"Edit") != 0) return false;

  SendMessageW(focused, EM_SETSEL, 0, -1);
  return true;
}


// -------------------------
// WinMain
// -------------------------
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

  INITCOMMONCONTROLSEX icc{};
  icc.dwSize = sizeof(icc);
  icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES;
  InitCommonControlsEx(&icc);

  App app;
  app.hInst = hInstance;
  g_app = &app;

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.hInstance = hInstance;
  wc.lpszClassName = L"NVSP_PhonemeEditorWin32";
  wc.lpfnWndProc = WndProc;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  RegisterClassExW(&wc);

  HWND hWnd = CreateWindowExW(
    WS_EX_CONTROLPARENT,
    wc.lpszClassName,
    L"NV Speech Player Phoneme Editor",
    WS_OVERLAPPEDWINDOW,
    CW_USEDEFAULT,
    CW_USEDEFAULT,
    1100,
    720,
    nullptr,
    LoadMenuW(hInstance, MAKEINTRESOURCEW(IDR_MAINMENU)),
    hInstance,
    nullptr
  );

  if (!hWnd) return 0;

  ShowWindow(hWnd, nCmdShow);
  UpdateWindow(hWnd);

  // Keyboard shortcuts.
  ACCEL accels[] = {
    { FVIRTKEY | FCONTROL, 'O', IDM_FILE_OPEN_PACKROOT },
    { FVIRTKEY | FCONTROL, 'S', IDM_FILE_SAVE_LANGUAGE },
  };
  HACCEL hAccel = CreateAcceleratorTableW(accels, static_cast<int>(std::size(accels)));

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0)) {
    if (hAccel && TranslateAcceleratorW(hWnd, hAccel, &msg)) {
      continue;
    }

    // Make Tab / Shift+Tab move focus across WS_TABSTOP controls.
    if (handleTabNavigation(hWnd, msg)) {
      continue;
    }

    if (handleCtrlASelectAll(hWnd, msg)) {
      continue;
    }

    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  if (hAccel) DestroyAcceleratorTable(hAccel);

  CoUninitialize();
  return 0;
}

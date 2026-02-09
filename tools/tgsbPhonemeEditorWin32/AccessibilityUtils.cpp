#define UNICODE
#define _UNICODE

#include "AccessibilityUtils.h"

#include "WinUtils.h"

#include <commctrl.h>
#include <oleacc.h>

#include <string>

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
    EnsureListViewHasSelection(hwnd);
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

void installAccessibleNameForListView(HWND lv, const std::wstring& name) {
  if (!lv) return;
  // Keep window text set too; some AT uses it.
  SetWindowTextW(lv, name.c_str());
  auto* data = new AccSubclassData();
  data->name = name;
  SetWindowSubclass(lv, accListViewSubclassProc, 1, reinterpret_cast<DWORD_PTR>(data));
}

// -------------------------

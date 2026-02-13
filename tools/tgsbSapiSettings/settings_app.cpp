/*
TGSpeechBox — SAPI settings dialog application.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#include <windows.h>
#include <commctrl.h>

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <string>
#include <unordered_set>
#include <vector>

#include "resource.h"

namespace {

// -----------------------------
// Small path helpers
// -----------------------------

bool file_exists(const std::wstring& path)
{
    DWORD attr = GetFileAttributesW(path.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES) && ((attr & FILE_ATTRIBUTE_DIRECTORY) == 0);
}

bool dir_exists(const std::wstring& path)
{
    DWORD attr = GetFileAttributesW(path.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES) && ((attr & FILE_ATTRIBUTE_DIRECTORY) != 0);
}

std::wstring join_path(const std::wstring& left, const std::wstring& right)
{
    if (left.empty()) {
        return right;
    }
    if (right.empty()) {
        return left;
    }

    std::wstring out = left;
    wchar_t last = out.back();
    if (last != L'\\' && last != L'/') {
        out.push_back(L'\\');
    }
    out += right;
    return out;
}

std::wstring strip_filename(const std::wstring& path)
{
    const size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) {
        return L".";
    }
    return path.substr(0, pos);
}

std::wstring parent_dir(const std::wstring& path)
{
    return strip_filename(path);
}

std::wstring get_exe_dir()
{
    wchar_t buf[MAX_PATH] = {};
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return L".";
    }
    return strip_filename(std::wstring(buf));
}

std::wstring detect_base_dir(const std::wstring& module_dir)
{
    // If the module is in ...\x86 or ...\x64, treat the parent as the base dir.
    const std::wstring lower = [&]() {
        std::wstring s = module_dir;
        std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return (wchar_t)std::towlower(c); });
        return s;
    }();

    const std::wstring suffix_x86 = L"\\x86";
    const std::wstring suffix_x64 = L"\\x64";

    if (lower.size() >= suffix_x86.size() && lower.compare(lower.size() - suffix_x86.size(), suffix_x86.size(), suffix_x86) == 0) {
        return parent_dir(module_dir);
    }
    if (lower.size() >= suffix_x64.size() && lower.compare(lower.size() - suffix_x64.size(), suffix_x64.size(), suffix_x64) == 0) {
        return parent_dir(module_dir);
    }

    return module_dir;
}

// -----------------------------
// INI helpers
// -----------------------------

std::wstring trim_copy(const std::wstring& s)
{
    size_t start = 0;
    while (start < s.size() && std::iswspace(s[start])) {
        ++start;
    }
    size_t end = s.size();
    while (end > start && std::iswspace(s[end - 1])) {
        --end;
    }
    return s.substr(start, end - start);
}

std::wstring normalize_lang_tag(std::wstring tag)
{
    tag = trim_copy(tag);
    std::transform(tag.begin(), tag.end(), tag.begin(), [](wchar_t c) {
        if (c == L'_') {
            return L'-';
        }
        return (wchar_t)std::towlower(c);
    });
    return tag;
}

std::vector<std::wstring> split_list(const std::wstring& s)
{
    std::vector<std::wstring> out;
    std::wstring cur;
    cur.reserve(16);

    auto flush = [&]() {
        const std::wstring t = normalize_lang_tag(cur);
        if (!t.empty()) {
            out.push_back(t);
        }
        cur.clear();
    };

    for (wchar_t c : s) {
        const bool sep = (c == L',' || c == L';' || c == L'\n' || c == L'\r' || c == L'\t');
        if (sep) {
            flush();
        } else {
            cur.push_back(c);
        }
    }
    flush();

    // de-dup
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::wstring join_list(const std::vector<std::wstring>& items)
{
    std::wstring out;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i > 0) {
            out += L",";
        }
        out += items[i];
    }
    return out;
}

std::wstring get_user_settings_path()
{
    wchar_t buf[MAX_PATH] = {};
    DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        // Very unlikely, but keep it safe.
        return L"settings.ini";
    }

    std::wstring dir(buf);
    dir = join_path(dir, L"TGSpeechSapi");
    // AppData\Roaming exists; creating one level is enough.
    CreateDirectoryW(dir.c_str(), nullptr);

    return join_path(dir, L"settings.ini");
}

struct Settings {
    // Default OFF. Users can explicitly enable if they want a log.
    bool logging_enabled = false;
    std::unordered_set<std::wstring> excluded; // normalized
};

Settings load_settings(const std::wstring& ini_path)
{
    Settings s;

    s.logging_enabled = GetPrivateProfileIntW(L"General", L"logging", 0, ini_path.c_str()) != 0;

    wchar_t buf[8192] = {};
    GetPrivateProfileStringW(L"Languages", L"excluded", L"", buf, (DWORD)(sizeof(buf) / sizeof(buf[0])), ini_path.c_str());

    const auto list = split_list(buf);
    for (const auto& t : list) {
        s.excluded.insert(t);
    }

    // Never allow excluding the built-in "default" rules from the UI/settings file.
    // The engine relies on default.yaml being present for baseline rules.
    s.excluded.erase(L"default");

    return s;
}

bool save_settings(const std::wstring& ini_path, const Settings& s)
{
    const wchar_t* logging_val = s.logging_enabled ? L"1" : L"0";
    if (!WritePrivateProfileStringW(L"General", L"logging", logging_val, ini_path.c_str())) {
        return false;
    }

    std::vector<std::wstring> excluded_sorted;
    excluded_sorted.reserve(s.excluded.size());
    for (const auto& t : s.excluded) {
        if (t == L"default") {
            continue;
        }
        excluded_sorted.push_back(t);
    }
    std::sort(excluded_sorted.begin(), excluded_sorted.end());

    const std::wstring excluded_val = join_list(excluded_sorted);
    if (!WritePrivateProfileStringW(L"Languages", L"excluded", excluded_val.c_str(), ini_path.c_str())) {
        return false;
    }

    return true;
}

// -----------------------------
// Language list + display names
// -----------------------------

std::wstring to_windows_locale_name(const std::wstring& tag)
{
    // Convert BCP-47-ish to something Windows accepts for GetLocaleInfoEx.
    // Example: "en-us" -> "en-US".
    std::wstring out;
    out.reserve(tag.size());

    bool after_dash = false;
    for (wchar_t c : tag) {
        if (c == L'_') {
            c = L'-';
        }
        if (c == L'-') {
            out.push_back(L'-');
            after_dash = true;
            continue;
        }

        if (!after_dash) {
            out.push_back((wchar_t)std::towlower(c));
        } else {
            // Region is typically uppercase; script casing isn't handled here.
            out.push_back((wchar_t)std::towupper(c));
        }
    }

    return out;
}

std::wstring get_language_display_name(const std::wstring& tag)
{
    const std::wstring locale = to_windows_locale_name(tag);

    wchar_t name[256] = {};
    int rc = GetLocaleInfoEx(locale.c_str(), LOCALE_SLOCALIZEDDISPLAYNAME, name, (int)(sizeof(name) / sizeof(name[0])));
    if (rc <= 0) {
        // Fallback: show the raw tag.
        return tag;
    }

    std::wstring out(name);
    out += L" (";
    out += tag;
    out += L")";
    return out;
}

std::vector<std::wstring> list_installed_language_tags(const std::wstring& base_dir)
{
    const std::wstring lang_dir = join_path(join_path(base_dir, L"packs"), L"lang");

    std::vector<std::wstring> tags;
    if (!dir_exists(lang_dir)) {
        return tags;
    }

    const std::wstring pattern = join_path(lang_dir, L"*.yaml");

    WIN32_FIND_DATAW fd = {};
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) {
        return tags;
    }

    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }

        std::wstring fname(fd.cFileName);
        const size_t dot = fname.find_last_of(L'.');
        if (dot == std::wstring::npos) {
            continue;
        }
        fname = fname.substr(0, dot);

        fname = normalize_lang_tag(fname);
        // default.yaml is a base rule file used by the engine; it is not a user-facing language.
        if (fname != L"default") {
            tags.push_back(fname);
        }

    } while (FindNextFileW(h, &fd));

    FindClose(h);

    std::sort(tags.begin(), tags.end());
    tags.erase(std::unique(tags.begin(), tags.end()), tags.end());

    // Keep a small fallback list if packs are missing.
    if (tags.empty()) {
        tags = {L"en-us", L"en", L"bg", L"de", L"fr-fr", L"es", L"it"};
    }

    return tags;
}

// -----------------------------
// Dialog
// -----------------------------

struct DialogState {
    std::wstring base_dir;
    std::wstring ini_path;

    std::vector<std::wstring> lang_tags;
    Settings settings;
};

void init_list_view(HWND hList)
{
    ListView_SetExtendedListViewStyleEx(
        hList,
        LVS_EX_FULLROWSELECT | LVS_EX_CHECKBOXES,
        LVS_EX_FULLROWSELECT | LVS_EX_CHECKBOXES);

    RECT rc = {};
    GetClientRect(hList, &rc);
    const int width = (rc.right > rc.left) ? (rc.right - rc.left - 4) : 200;

    LVCOLUMNW col = {};
    col.mask = LVCF_TEXT | LVCF_WIDTH;
    col.pszText = const_cast<wchar_t*>(L"Language");
    col.cx = width;
    ListView_InsertColumn(hList, 0, &col);
}

INT_PTR CALLBACK MainDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_INITDIALOG: {
        auto* st = reinterpret_cast<DialogState*>(lParam);
        SetWindowLongPtrW(hDlg, DWLP_USER, reinterpret_cast<LONG_PTR>(st));

        // Logging checkbox.
        CheckDlgButton(hDlg, IDC_ENABLE_LOGGING, st->settings.logging_enabled ? BST_CHECKED : BST_UNCHECKED);

        HWND hList = GetDlgItem(hDlg, IDC_LANG_LIST);
        init_list_view(hList);

        // Populate.
        for (size_t i = 0; i < st->lang_tags.size(); ++i) {
            const std::wstring& tag = st->lang_tags[i];
            const std::wstring text = get_language_display_name(tag);

            LVITEMW item = {};
            item.mask = LVIF_TEXT | LVIF_PARAM;
            item.iItem = (int)i;
            item.pszText = const_cast<wchar_t*>(text.c_str());
            item.lParam = (LPARAM)i;

            ListView_InsertItem(hList, &item);

            const std::wstring norm = normalize_lang_tag(tag);
            const bool enabled = st->settings.excluded.find(norm) == st->settings.excluded.end();
            ListView_SetCheckState(hList, (int)i, enabled ? TRUE : FALSE);
        }

        return TRUE;
    }

    case WM_COMMAND: {
        const WORD id = LOWORD(wParam);

        if (id == IDOK) {
            auto* st = reinterpret_cast<DialogState*>(GetWindowLongPtrW(hDlg, DWLP_USER));
            if (!st) {
                EndDialog(hDlg, IDCANCEL);
                return TRUE;
            }

            st->settings.logging_enabled = (IsDlgButtonChecked(hDlg, IDC_ENABLE_LOGGING) == BST_CHECKED);

            HWND hList = GetDlgItem(hDlg, IDC_LANG_LIST);
            const int count = ListView_GetItemCount(hList);

            std::unordered_set<std::wstring> excluded;
            for (int i = 0; i < count; ++i) {
                const BOOL checked = ListView_GetCheckState(hList, i);
                if (checked) {
                    continue;
                }

                LVITEMW item = {};
                item.mask = LVIF_PARAM;
                item.iItem = i;
                if (ListView_GetItem(hList, &item)) {
                    const size_t idx = (size_t)item.lParam;
                    if (idx < st->lang_tags.size()) {
                        excluded.insert(normalize_lang_tag(st->lang_tags[idx]));
                    }
                }
            }

            st->settings.excluded = std::move(excluded);

            if (!save_settings(st->ini_path, st->settings)) {
                MessageBoxW(hDlg, L"Failed to write settings.ini. Try running this tool as administrator.", L"TGSpeechBox SAPI Settings", MB_OK | MB_ICONERROR);
                return TRUE;
            }

            EndDialog(hDlg, IDOK);
            return TRUE;
        }

        if (id == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }

        break;
    }

    default:
        break;
    }

    return FALSE;
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int)
{
    INITCOMMONCONTROLSEX icc = {};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&icc);

    DialogState st;
    const std::wstring module_dir = get_exe_dir();
    st.base_dir = detect_base_dir(module_dir);
    st.ini_path = get_user_settings_path();

    st.lang_tags = list_installed_language_tags(st.base_dir);
    st.settings = load_settings(st.ini_path);

    (void)DialogBoxParamW(hInstance, MAKEINTRESOURCEW(IDD_MAIN_DIALOG), nullptr, MainDlgProc, reinterpret_cast<LPARAM>(&st));
    return 0;
}

#include "process_util.h"

#include <filesystem>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

namespace nvsp_editor {

static std::wstring quoteArg(const std::wstring& s) {
  // Simple quoting for CreateProcess command lines.
  // This is not a full Windows command-line escaping implementation, but is
  // sufficient for paths and simple arguments for this tool.
  if (s.empty()) return L"\"\"";
  bool needs = false;
  for (wchar_t c : s) {
    if (c == L' ' || c == L'\t' || c == L'\n' || c == L'\v' || c == L'\"') {
      needs = true;
      break;
    }
  }
  if (!needs) return s;

  std::wstring out = L"\"";
  for (wchar_t c : s) {
    if (c == L'\"') out += L"\\\"";
    else out.push_back(c);
  }
  out += L"\"";
  return out;
}

bool runProcessCaptureStdout(
  const std::wstring& exePath,
  const std::wstring& args,
  std::string& outStdoutUtf8,
  std::string& outError
) {
  outStdoutUtf8.clear();
  outError.clear();

  if (exePath.empty()) {
    outError = "Executable path is empty";
    return false;
  }

  HANDLE hRead = NULL;
  HANDLE hWrite = NULL;

  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;

  if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
    outError = "CreatePipe failed";
    return false;
  }

  // Ensure the read handle is not inherited.
  SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

  // GUI apps often don't have valid STDIN. Give the child a readable handle.
  HANDLE hNullIn = CreateFileW(
    L"NUL",
    GENERIC_READ,
    FILE_SHARE_READ | FILE_SHARE_WRITE,
    &sa,
    OPEN_EXISTING,
    FILE_ATTRIBUTE_NORMAL,
    NULL
  );
  if (hNullIn == INVALID_HANDLE_VALUE) {
    hNullIn = GetStdHandle(STD_INPUT_HANDLE);
  }

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = (hNullIn && hNullIn != INVALID_HANDLE_VALUE) ? hNullIn : NULL;
  si.hStdOutput = hWrite;
  si.hStdError = hWrite;

  PROCESS_INFORMATION pi{};

  std::wstring cmd = quoteArg(exePath);
  if (!args.empty()) {
    cmd += L" ";
    cmd += args;
  }

  // CreateProcess wants a writable buffer.
  std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
  cmdBuf.push_back(L'\0');

  // Some eSpeak builds are sensitive to current directory when locating data.
  // Use the executable directory as the working directory.
  std::wstring cwd;
  try {
    cwd = fs::path(exePath).parent_path().wstring();
  } catch (...) {
    cwd.clear();
  }

  BOOL ok = CreateProcessW(
    exePath.c_str(),
    cmdBuf.data(),
    NULL,
    NULL,
    TRUE,
    CREATE_NO_WINDOW,
    NULL,
    cwd.empty() ? NULL : cwd.c_str(),
    &si,
    &pi
  );

  // Parent doesn't write.
  CloseHandle(hWrite);
  hWrite = NULL;

  if (hNullIn && hNullIn != INVALID_HANDLE_VALUE && hNullIn != GetStdHandle(STD_INPUT_HANDLE)) {
    CloseHandle(hNullIn);
    hNullIn = NULL;
  }

  if (!ok) {
    DWORD e = GetLastError();
    CloseHandle(hRead);
    std::ostringstream oss;
    oss << "CreateProcess failed (" << static_cast<unsigned long>(e) << ")";
    outError = oss.str();
    return false;
  }

  // Read all output.
  std::string buf;
  char tmp[4096];
  DWORD read = 0;
  while (true) {
    BOOL r = ReadFile(hRead, tmp, sizeof(tmp), &read, NULL);
    if (!r || read == 0) break;
    buf.append(tmp, tmp + read);
  }

  CloseHandle(hRead);
  hRead = NULL;

  WaitForSingleObject(pi.hProcess, INFINITE);

  DWORD exitCode = 0;
  GetExitCodeProcess(pi.hProcess, &exitCode);

  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);

  outStdoutUtf8 = std::move(buf);

  // Trim trailing CR/LF.
  while (!outStdoutUtf8.empty() && (outStdoutUtf8.back() == '\n' || outStdoutUtf8.back() == '\r')) {
    outStdoutUtf8.pop_back();
  }

  if (exitCode != 0) {
    std::ostringstream oss;
    oss << "Process exit code " << static_cast<unsigned long>(exitCode)
        << " (0x" << std::hex << static_cast<unsigned long>(exitCode) << std::dec << ")";

    if (!outStdoutUtf8.empty()) {
      // Include a short snippet of output to help debugging.
      std::string snippet = outStdoutUtf8;
      if (snippet.size() > 600) {
        snippet.resize(600);
        snippet += "...";
      }
      oss << "\n\nOutput:\n" << snippet;
    }

    outError = oss.str();
    return false;
  }

  return true;
}

std::wstring findEspeakExe(const std::wstring& espeakDir) {
  if (espeakDir.empty()) return L"";

  fs::path base(espeakDir);

  fs::path ng = base / "espeak-ng.exe";
  if (fs::exists(ng)) return ng.wstring();

  fs::path legacy = base / "espeak.exe";
  if (fs::exists(legacy)) return legacy.wstring();

  return L"";
}

// Returns the "data home" directory to pass to espeak_Initialize/--path.
// According to speak_lib.h, this should be the directory that *contains*
// the espeak-ng-data (or espeak-data) directory.
//
// Examples:
//   C:\eSpeak NG\            -> contains espeak-ng-data  => return C:\eSpeak NG\ (home)
//   C:\eSpeak NG\bin\       -> parent contains data     => return C:\eSpeak NG\ (home)
//   C:\eSpeak NG\espeak-ng-data\ -> base is data dir    => return C:\eSpeak NG\ (home)
std::wstring findEspeakDataDir(const std::wstring& espeakDir) {
  if (espeakDir.empty()) return L"";

  fs::path base(espeakDir);
  std::error_code ec;

  auto hasDataDir = [&](const fs::path& home) -> bool {
    if (home.empty()) return false;
    const fs::path ng = home / "espeak-ng-data";
    const fs::path legacy = home / "espeak-data";
    if (fs::exists(ng, ec) && fs::is_directory(ng, ec)) return true;
    if (fs::exists(legacy, ec) && fs::is_directory(legacy, ec)) return true;
    return false;
  };

  // If the user picked the actual data directory, return its parent.
  {
    const std::wstring leaf = base.filename().wstring();
    if (_wcsicmp(leaf.c_str(), L"espeak-ng-data") == 0 || _wcsicmp(leaf.c_str(), L"espeak-data") == 0) {
      return base.parent_path().wstring();
    }
  }

  // Common: <base> contains espeak-ng-data/espeak-data.
  if (hasDataDir(base)) return base.wstring();

  // Some layouts: <base>/share contains the data directory.
  {
    const fs::path share = base / "share";
    if (hasDataDir(share)) return share.wstring();
  }

  // If the user picked a bin folder, the parent might contain the data.
  {
    const fs::path parent = base.parent_path();
    if (hasDataDir(parent)) return parent.wstring();
  }

  return L"";
}


static std::wstring findEspeakDllInDir(const std::wstring& espeakDir) {
  if (espeakDir.empty()) return L"";

  fs::path base(espeakDir);

  // Common Windows names across eSpeak NG builds.
  const std::vector<fs::path> candidates = {
    base / "libespeak-ng.dll",
    base / "espeak-ng.dll",
    base / "libespeak.dll",
    base / "espeak.dll",
    base / "bin" / "libespeak-ng.dll",
    base / "bin" / "espeak-ng.dll",
    base / "bin" / "libespeak.dll",
    base / "bin" / "espeak.dll",
  };

  for (const auto& c : candidates) {
    std::error_code ec;
    if (fs::exists(c, ec) && fs::is_regular_file(c, ec)) {
      return c.wstring();
    }
  }

  return L"";
}

std::wstring findEspeakDll(const std::wstring& espeakDir) {
  return findEspeakDllInDir(espeakDir);
}

// Minimal dynamic binding to eSpeak NG / eSpeak for TextToPhonemes.
// NVDA uses espeak_TextToPhonemes with phoneme mode 0x36182; that corresponds to
// IPA output in UTF-8 plus additional flags.
struct EspeakDyn {
  HMODULE mod = NULL;
  std::wstring dllPath;

  // Function pointers (cdecl, matches NVDA's ctypes.cdll usage).
  int (__cdecl* espeak_Initialize)(int output, int buflength, const char* path, int options) = nullptr;
  int (__cdecl* espeak_Terminate)() = nullptr;
  int (__cdecl* espeak_SetVoiceByName)(const char* name) = nullptr;
  const char* (__cdecl* espeak_TextToPhonemes)(const void** textptr, int textmode, int phonememode) = nullptr;

  // Optional eSpeak-NG API (not required by NVDA, but helpful for some builds).
  void (__cdecl* espeak_ng_InitializePath)(const char* path) = nullptr;
  int (__cdecl* espeak_ng_SetVoiceByName)(const char* name) = nullptr;

  bool initialized = false;

  void reset() {
    initialized = false;
    espeak_Initialize = nullptr;
    espeak_Terminate = nullptr;
    espeak_SetVoiceByName = nullptr;
    espeak_TextToPhonemes = nullptr;
    espeak_ng_InitializePath = nullptr;
    espeak_ng_SetVoiceByName = nullptr;
    if (mod) {
      FreeLibrary(mod);
      mod = NULL;
    }
    dllPath.clear();
  }
};

static EspeakDyn g_espeak;

static std::string widePathToUtf8(const std::wstring& w) {
  if (w.empty()) return {};
  int needed = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
  if (needed <= 0) return {};
  std::string out;
  out.resize(static_cast<size_t>(needed - 1));
  WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, out.data(), needed, nullptr, nullptr);
  return out;
}

static std::vector<std::string> buildVoiceCandidates(std::string tag) {
  // Try a few variants, similar to NVDA's driver.
  for (char& c : tag) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }

  std::vector<std::string> out;
  auto pushUnique = [&](const std::string& s) {
    if (s.empty()) return;
    for (const auto& e : out) {
      if (e == s) return;
    }
    out.push_back(s);
  };

  pushUnique(tag);

  // Swap separators.
  {
    std::string t = tag;
    for (char& c : t) if (c == '_') c = '-';
    pushUnique(t);
  }
  {
    std::string t = tag;
    for (char& c : t) if (c == '-') c = '_';
    pushUnique(t);
  }

  // Base language.
  {
    size_t cut = tag.find('-');
    if (cut == std::string::npos) cut = tag.find('_');
    if (cut != std::string::npos) {
      pushUnique(tag.substr(0, cut));
    }
  }

  // NVDA falls back to English; keep it last.
  pushUnique("en");

  return out;
}

bool espeakTextToIpaViaDll(
  const std::wstring& espeakDir,
  const std::string& langTagUtf8,
  const std::wstring& text,
  std::string& outIpaUtf8,
  std::string& outError
) {
  outIpaUtf8.clear();
  outError.clear();

  if (espeakDir.empty()) {
    outError = "eSpeak directory is empty";
    return false;
  }

  const std::wstring dllPath = findEspeakDllInDir(espeakDir);
  if (dllPath.empty()) {
    outError = "No eSpeak DLL found (looked for libespeak-ng.dll / espeak-ng.dll / espeak.dll)";
    return false;
  }

  // Load or reuse.
  if (!g_espeak.mod || g_espeak.dllPath != dllPath) {
    g_espeak.reset();

    // LOAD_WITH_ALTERED_SEARCH_PATH makes dependency resolution prefer the DLL's directory.
    g_espeak.mod = LoadLibraryExW(dllPath.c_str(), NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!g_espeak.mod) {
      DWORD e = GetLastError();
      outError = "LoadLibraryEx failed (" + std::to_string(static_cast<unsigned long>(e)) + ")";
      return false;
    }

    g_espeak.dllPath = dllPath;

    auto gp = [&](const char* name) -> FARPROC { return GetProcAddress(g_espeak.mod, name); };

    g_espeak.espeak_Initialize = reinterpret_cast<decltype(g_espeak.espeak_Initialize)>(gp("espeak_Initialize"));
    g_espeak.espeak_Terminate = reinterpret_cast<decltype(g_espeak.espeak_Terminate)>(gp("espeak_Terminate"));
    g_espeak.espeak_SetVoiceByName = reinterpret_cast<decltype(g_espeak.espeak_SetVoiceByName)>(gp("espeak_SetVoiceByName"));
    g_espeak.espeak_TextToPhonemes = reinterpret_cast<decltype(g_espeak.espeak_TextToPhonemes)>(gp("espeak_TextToPhonemes"));

    // Optional eSpeak-NG API entry points (present in libespeak-ng.dll builds).
    g_espeak.espeak_ng_InitializePath = reinterpret_cast<decltype(g_espeak.espeak_ng_InitializePath)>(gp("espeak_ng_InitializePath"));
    g_espeak.espeak_ng_SetVoiceByName = reinterpret_cast<decltype(g_espeak.espeak_ng_SetVoiceByName)>(gp("espeak_ng_SetVoiceByName"));

    if (!g_espeak.espeak_Initialize || !g_espeak.espeak_TextToPhonemes ||
        (!g_espeak.espeak_SetVoiceByName && !g_espeak.espeak_ng_SetVoiceByName)) {
      outError = "eSpeak DLL is missing required exports (espeak_Initialize / espeak_TextToPhonemes and a SetVoiceByName variant)";
      g_espeak.reset();
      return false;
    }
  }

  // Initialize once.
  if (!g_espeak.initialized) {
    const std::wstring dataHomeW = findEspeakDataDir(espeakDir);
    const std::string dataHomeUtf8 = widePathToUtf8(dataHomeW);

    // If available, tell the eSpeak-NG API where espeak-ng-data lives.
    // This is especially important when the process CWD isn't the eSpeak directory.
    if (g_espeak.espeak_ng_InitializePath && !dataHomeUtf8.empty()) {
      g_espeak.espeak_ng_InitializePath(dataHomeUtf8.c_str());
    }

    // espeak_AUDIO_OUTPUT_RETRIEVAL = 1 (no playback). Using retrieval is safest for a GUI tool.
    // NOTE: speak_lib.h documents that "path" is the directory that *contains* espeak-ng-data.
    int sr = g_espeak.espeak_Initialize(1 /* retrieval */, 0 /* buflen */, dataHomeUtf8.empty() ? nullptr : dataHomeUtf8.c_str(), 0);
    if (sr <= 0) {
      outError = "espeak_Initialize failed";
      return false;
    }
    g_espeak.initialized = true;
  }

  // Set voice.
  bool voiceOk = false;
  auto trySetVoice = [&](const std::string& name) -> bool {
    // Prefer the eSpeak-NG API when present, but the legacy API is what NVDA uses.
    if (g_espeak.espeak_ng_SetVoiceByName && g_espeak.espeak_ng_SetVoiceByName(name.c_str()) == 0) return true;
    if (g_espeak.espeak_SetVoiceByName && g_espeak.espeak_SetVoiceByName(name.c_str()) == 0) return true;
    return false;
  };
  for (const auto& v : buildVoiceCandidates(langTagUtf8)) {
    if (trySetVoice(v)) {
      voiceOk = true;
      break;
    }
  }
  (void)voiceOk; // even if it fails, we'll still try to convert with the current voice.

  // Prepare a mutable, null-terminated UTF-16 buffer.
  std::vector<wchar_t> buf(text.begin(), text.end());
  buf.push_back(L'\0');

  const void* p = static_cast<const void*>(buf.data());
  const void* last = nullptr;

  // Match NVDA's phoneme mode.
  const int kTextModeWChar = 3; // espeakCHARS_WCHAR
  const int kNvdaPhonemeMode = 0x36182;

  std::string out;
  while (p && p != last) {
    last = p;
    const char* chunk = g_espeak.espeak_TextToPhonemes(&p, kTextModeWChar, kNvdaPhonemeMode);
    if (!chunk) break;
    out += chunk;
  }

  // Trim whitespace.
  while (!out.empty() && (out.back() == '\r' || out.back() == '\n' || out.back() == ' ' || out.back() == '\t')) out.pop_back();
  size_t start = 0;
  while (start < out.size() && (out[start] == ' ' || out[start] == '\t' || out[start] == '\r' || out[start] == '\n')) start++;
  outIpaUtf8 = out.substr(start);

  if (outIpaUtf8.empty()) {
    // Sometimes eSpeak returns empty for whitespace-only input.
    outError = "eSpeak produced empty IPA";
    return false;
  }

  return true;
}

} // namespace nvsp_editor

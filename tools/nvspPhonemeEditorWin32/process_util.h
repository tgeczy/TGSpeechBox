#pragma once

#include <string>
#include <vector>

#include <windows.h>

namespace nvsp_editor {

// Run a process and capture its stdout as UTF-8 bytes.
// exePath: full path to the exe.
// args: command line arguments (without the exe name).
// Returns true on success.
bool runProcessCaptureStdout(
  const std::wstring& exePath,
  const std::wstring& args,
  std::string& outStdoutUtf8,
  std::string& outError
);

// Find espeak-ng.exe or espeak.exe inside a directory.
std::wstring findEspeakExe(const std::wstring& espeakDir);

// Find an eSpeak data directory within the configured eSpeak directory.
// Returns a full path to either espeak-ng-data or espeak-data if present.
std::wstring findEspeakDataDir(const std::wstring& espeakDir);

//
// Find an eSpeak shared library within the configured eSpeak directory.
// This allows us to call espeak_TextToPhonemes() directly, matching NVDA's
// internal eSpeak pipeline more closely than command-line flags.
// Returns a full path to a DLL such as libespeak-ng.dll, espeak-ng.dll, etc.
std::wstring findEspeakDll(const std::wstring& espeakDir);

// Convert text to IPA/phonemes via the eSpeak DLL (if available).
// langTagUtf8 should be like "en", "hu", "pt-br". text is UTF-16.
// On success, outIpaUtf8 receives UTF-8 IPA and the function returns true.
bool espeakTextToIpaViaDll(
  const std::wstring& espeakDir,
  const std::string& langTagUtf8,
  const std::wstring& text,
  std::string& outIpaUtf8,
  std::string& outError
);

} // namespace nvsp_editor

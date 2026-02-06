#pragma once

#define UNICODE
#define _UNICODE

#include <windows.h>

#include <string>
#include <map>
#include <utility>
#include <vector>

#include "nvsp_runtime.h"
#include "yaml_edit.h"

struct AddMappingDialogState {
  nvsp_editor::ReplacementRule rule;
  std::vector<std::string> classNames;
  nvsp_editor::LanguageYaml* language = nullptr;  // for class editing
  bool ok = false;
};

struct ClassEditorDialogState {
  std::map<std::string, std::string> classes;  // className -> members string
  bool ok = false;
};

struct ClonePhonemeDialogState {
  std::vector<std::string> keys;
  std::string fromKey;
  std::string newKey;
  bool ok = false;
};

struct EditValueDialogState {
  std::string field;
  std::string value;
  nvsp_editor::Node baseMap;
  nvsp_editor::NvspRuntime* runtime = nullptr;

  bool livePreview = true;
  bool ok = false;

  bool armed = false;
  UINT_PTR previewTimer = 0;
};

struct EditSettingDialogState {
  std::string key;
  std::string value;
  std::vector<std::string> knownKeys;
  bool ok = false;
};

struct EditSettingsDialogState {
  std::vector<std::pair<std::string, std::string>> settings;  // key/value
  std::vector<std::string> knownKeys;
  bool ok = false;
};

struct EditPhonemeDialogState {
  std::string phonemeKey;
  nvsp_editor::Node original;
  nvsp_editor::Node working;
  nvsp_editor::NvspRuntime* runtime = nullptr;
  bool ok = false;
};

struct SpeechSettingsDialogState {
  nvsp_editor::SpeechSettings settings;
  nvsp_editor::NvspRuntime* runtime = nullptr;

  // Discovered voice profiles from phonemes.yaml
  std::vector<std::string> voiceProfiles;

  // Frame param UI
  std::vector<std::string> paramNames;
  int selectedParam = 0;
  
  // Voicing param UI
  std::vector<std::string> voicingParamNames;
  int selectedVoicingParam = 0;
  
  // FrameEx param UI (voice quality: creakiness, breathiness, jitter, shimmer, sharpness)
  std::vector<std::string> frameExParamNames;
  int selectedFrameExParam = 0;
  
  bool ok = false;
};

struct PhonemizerSettingsDialogState {
  // If empty, the editor uses espeak-ng.exe/espeak.exe found in the configured eSpeak directory.
  std::wstring exePath;

  // Argument templates. Placeholders: {lang} {qlang} {text} {qtext} {dataDir} {pathArg}
  std::wstring argsStdin;
  std::wstring argsCli;

  // When true, we try STDIN first and fall back to CLI args if provided.
  bool preferStdin = true;

  // Sentence-aware chunk size for phonemizer calls.
  int maxChunkChars = 420;

  bool ok = false;
};

// Dialog launch helpers (return true if OK was pressed).
bool ShowAddMappingDialog(HINSTANCE hInst, HWND parent, AddMappingDialogState& st);
bool ShowClonePhonemeDialog(HINSTANCE hInst, HWND parent, ClonePhonemeDialogState& st);
bool ShowEditValueDialog(HINSTANCE hInst, HWND parent, EditValueDialogState& st);
bool ShowEditSettingsDialog(HINSTANCE hInst, HWND parent, EditSettingsDialogState& st);
bool ShowEditPhonemeDialog(HINSTANCE hInst, HWND parent, EditPhonemeDialogState& st);
bool ShowSpeechSettingsDialog(HINSTANCE hInst, HWND parent, SpeechSettingsDialogState& st);
bool ShowPhonemizerSettingsDialog(HINSTANCE hInst, HWND parent, PhonemizerSettingsDialogState& st);
bool ShowClassEditorDialog(HINSTANCE hInst, HWND parent, ClassEditorDialogState& st);

// Persistence for speech settings (tgsbPhonemeEditor.ini).
nvsp_editor::SpeechSettings loadSpeechSettingsFromIni();
void saveSpeechSettingsToIni(const nvsp_editor::SpeechSettings& s);

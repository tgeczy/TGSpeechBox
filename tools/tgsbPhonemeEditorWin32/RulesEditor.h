#pragma once

#define UNICODE
#define _UNICODE

#include <windows.h>
#include <string>
#include <vector>
#include "yaml_edit.h"

namespace tgsb_editor {

struct AllophoneRulesDialogState {
  std::vector<AllophoneRuleEntry> rules;
  LanguageYaml* language = nullptr;
  bool ok = false;
  bool modified = false;
};

struct SpecialCoarticDialogState {
  std::vector<SpecialCoarticRuleEntry> rules;
  LanguageYaml* language = nullptr;
  bool ok = false;
  bool modified = false;
};

bool ShowAllophoneRulesDialog(HINSTANCE hInst, HWND parent, AllophoneRulesDialogState& st);
bool ShowSpecialCoarticDialog(HINSTANCE hInst, HWND parent, SpecialCoarticDialogState& st);

} // namespace tgsb_editor

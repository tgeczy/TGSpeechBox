#pragma once

#include <string>
#include <vector>

#include <windows.h>

#include "sample.h"

namespace tgsb_editor {

bool writeWav16Mono(
  const std::wstring& path,
  int sampleRate,
  const std::vector<sample>& samples,
  std::string& outError
);

// Convenience for building a temp file name in %TEMP%.
std::wstring makeTempWavPath(const std::wstring& prefix);

} // namespace tgsb_editor

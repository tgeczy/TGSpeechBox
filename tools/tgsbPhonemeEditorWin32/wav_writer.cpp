/*
TGSpeechBox — WAV file writer.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#include "wav_writer.h"

#include <cstdint>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace tgsb_editor {

static void writeLE16(std::ofstream& f, std::uint16_t v) {
  char b[2];
  b[0] = static_cast<char>(v & 0xFF);
  b[1] = static_cast<char>((v >> 8) & 0xFF);
  f.write(b, 2);
}

static void writeLE32(std::ofstream& f, std::uint32_t v) {
  char b[4];
  b[0] = static_cast<char>(v & 0xFF);
  b[1] = static_cast<char>((v >> 8) & 0xFF);
  b[2] = static_cast<char>((v >> 16) & 0xFF);
  b[3] = static_cast<char>((v >> 24) & 0xFF);
  f.write(b, 4);
}

bool writeWav16Mono(
  const std::wstring& path,
  int sampleRate,
  const std::vector<sample>& samples,
  std::string& outError
) {
  outError.clear();

  if (path.empty()) {
    outError = "Output path is empty";
    return false;
  }
  if (sampleRate <= 0) {
    outError = "Invalid sample rate";
    return false;
  }

  const std::uint16_t channels = 1;
  const std::uint16_t bitsPerSample = 16;
  const std::uint16_t blockAlign = static_cast<std::uint16_t>(channels * (bitsPerSample / 8));
  const std::uint32_t byteRate = static_cast<std::uint32_t>(sampleRate) * blockAlign;

  const std::uint32_t dataSize = static_cast<std::uint32_t>(samples.size() * sizeof(sampleVal));
  const std::uint32_t fmtSize = 16;
  const std::uint32_t riffSize = 4 + (8 + fmtSize) + (8 + dataSize);

  std::ofstream f(fs::path(path), std::ios::binary);
  if (!f) {
    outError = "Could not open output file";
    return false;
  }

  // RIFF header
  f.write("RIFF", 4);
  writeLE32(f, riffSize);
  f.write("WAVE", 4);

  // fmt chunk
  f.write("fmt ", 4);
  writeLE32(f, fmtSize);
  writeLE16(f, 1); // PCM
  writeLE16(f, channels);
  writeLE32(f, static_cast<std::uint32_t>(sampleRate));
  writeLE32(f, byteRate);
  writeLE16(f, blockAlign);
  writeLE16(f, bitsPerSample);

  // data chunk
  f.write("data", 4);
  writeLE32(f, dataSize);
  if (!samples.empty()) {
    f.write(reinterpret_cast<const char*>(samples.data()), static_cast<std::streamsize>(samples.size() * sizeof(sampleVal)));
  }

  return true;
}

std::wstring makeTempWavPath(const std::wstring& prefix) {
  wchar_t tempDir[MAX_PATH] = {0};
  DWORD n = GetTempPathW(MAX_PATH, tempDir);
  if (n == 0 || n >= MAX_PATH) {
    return L"tgsb_temp.wav";
  }

  wchar_t tempFile[MAX_PATH] = {0};
  std::wstring pfx = prefix;
  if (pfx.size() < 3) pfx.append(3 - pfx.size(), L'_');
  if (pfx.size() > 3) pfx.resize(3);

  if (!GetTempFileNameW(tempDir, pfx.c_str(), 0, tempFile)) {
    return std::wstring(tempDir) + L"tgsb_temp.wav";
  }

  std::wstring out = tempFile;
  size_t dot = out.rfind(L'.');
  if (dot != std::wstring::npos) {
    out = out.substr(0, dot);
  }
  out += L".wav";
  return out;
}

} // namespace tgsb_editor

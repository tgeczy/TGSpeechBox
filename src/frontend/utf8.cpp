/*
TGSpeechBox — UTF-8 encoding and decoding utilities.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#include "utf8.h"

#include <cstdint>

namespace nvsp_frontend {

static inline char32_t kReplacementChar = 0xFFFD;

std::u32string utf8ToU32(std::string_view s) {
  std::u32string out;
  out.reserve(s.size());

  const unsigned char* p = reinterpret_cast<const unsigned char*>(s.data());
  const unsigned char* end = p + s.size();

  while (p < end) {
    uint32_t cp = 0;
    unsigned char c0 = *p++;

    if (c0 < 0x80) {
      cp = c0;
    } else if ((c0 >> 5) == 0x6) {
      // 110xxxxx 10xxxxxx
      if (p >= end) {
        out.push_back(kReplacementChar);
        break;
      }
      unsigned char c1 = *p++;
      if ((c1 & 0xC0) != 0x80) {
        out.push_back(kReplacementChar);
        continue;
      }
      cp = ((c0 & 0x1F) << 6) | (c1 & 0x3F);
      if (cp < 0x80) {
        out.push_back(kReplacementChar);
        continue;
      }
    } else if ((c0 >> 4) == 0xE) {
      // 1110xxxx 10xxxxxx 10xxxxxx
      if (p + 1 >= end) {
        out.push_back(kReplacementChar);
        break;
      }
      unsigned char c1 = *p++;
      unsigned char c2 = *p++;
      if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80) {
        out.push_back(kReplacementChar);
        continue;
      }
      cp = ((c0 & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (c2 & 0x3F);
      if (cp < 0x800) {
        out.push_back(kReplacementChar);
        continue;
      }
      // UTF-16 surrogate halves are not valid Unicode scalar values.
      if (cp >= 0xD800 && cp <= 0xDFFF) {
        out.push_back(kReplacementChar);
        continue;
      }
    } else if ((c0 >> 3) == 0x1E) {
      // 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
      if (p + 2 >= end) {
        out.push_back(kReplacementChar);
        break;
      }
      unsigned char c1 = *p++;
      unsigned char c2 = *p++;
      unsigned char c3 = *p++;
      if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80) {
        out.push_back(kReplacementChar);
        continue;
      }
      cp = ((c0 & 0x07) << 18) | ((c1 & 0x3F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
      if (cp < 0x10000 || cp > 0x10FFFF) {
        out.push_back(kReplacementChar);
        continue;
      }
    } else {
      out.push_back(kReplacementChar);
      continue;
    }

    out.push_back(static_cast<char32_t>(cp));
  }

  return out;
}

std::string u32ToUtf8(std::u32string_view s) {
  std::string out;
  out.reserve(s.size());

  for (char32_t ch : s) {
    uint32_t cp = static_cast<uint32_t>(ch);
    if (cp <= 0x7F) {
      out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
      out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
      out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }

  return out;
}

std::string normalizeLangTag(std::string_view tag) {
  std::string out;
  out.reserve(tag.size());
  for (char c : tag) {
    if (c >= 'A' && c <= 'Z') {
      out.push_back(static_cast<char>(c - 'A' + 'a'));
    } else if (c == '_') {
      out.push_back('-');
    } else {
      out.push_back(c);
    }
  }
  return out;
}

} // namespace nvsp_frontend

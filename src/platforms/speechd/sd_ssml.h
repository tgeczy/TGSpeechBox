/*
TGSpeechBox — the text of a Speech Dispatcher SPEAK message.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#ifndef TGSB_SD_SSML_H
#define TGSB_SD_SSML_H

#include <cstdint>
#include <string>

namespace tgsb_sd {

// Speech Dispatcher hands a module the text of a SPEAK message as SSML: the
// text wrapped in <speak>, a <mark name="__spd_N"/> after each sentence, and
// the characters that would read as markup written as entities.  For a
// client's plain text the server writes "<", ">" and "&" as "&lt;", "&gt;"
// and "&amp;" (speech-dispatcher 0.11.1, src/server/index_marking.c,
// insert_index_marks); a client that sends SSML itself may use any entity
// XML has.  CHAR and KEY messages are not SSML: the character or key name
// comes as it is (src/server/output.c), and must not pass through here.
//
// The module used to drop the tags and leave the entities, so "Tom & Jerry"
// reached the phonemizer as "Tom &amp; Jerry" and was read with the entity's
// name in it; and it did the same to CHAR and KEY, where a lone "<" was
// taken for the start of a tag and made no sound at all
// (tests/cpp/test_sd_ssml.cpp, tests/test_linux_speechd_module.py).
//
// sdSsmlToText: tags are removed, and &lt; &gt; &amp; &apos; &quot; and the
// numeric forms &#60; and &#x3C; become their characters.  One pass, so a
// decoded "<" is text and never the start of a tag.  An "&" that begins
// nothing known is kept as it is.

namespace detail {

inline void appendUtf8(std::string& out, std::uint32_t cp) {
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

// The entity that starts at s[at] ('&'): its character is appended to `out`
// and the number of bytes it took is returned, or 0 when it is not one.
inline std::size_t decodeEntity(const std::string& s, std::size_t at, std::string& out) {
  const std::size_t semi = s.find(';', at + 1);
  if (semi == std::string::npos || semi - at > 10 || semi == at + 1) return 0;
  const std::string name = s.substr(at + 1, semi - at - 1);
  struct Named { const char* name; char ch; };
  static const Named kNamed[] = {{"lt", '<'}, {"gt", '>'}, {"amp", '&'}, {"apos", '\''}, {"quot", '"'}};
  for (const Named& n : kNamed) {
    if (name == n.name) {
      out += n.ch;
      return semi - at + 1;
    }
  }
  if (name.size() < 2 || name[0] != '#') return 0;
  const bool hex = name[1] == 'x' || name[1] == 'X';
  std::uint32_t cp = 0;
  std::size_t digits = 0;
  for (std::size_t i = hex ? 2 : 1; i < name.size(); ++i, ++digits) {
    const char c = name[i];
    std::uint32_t d;
    if (c >= '0' && c <= '9') d = static_cast<std::uint32_t>(c - '0');
    else if (hex && c >= 'a' && c <= 'f') d = static_cast<std::uint32_t>(c - 'a' + 10);
    else if (hex && c >= 'A' && c <= 'F') d = static_cast<std::uint32_t>(c - 'A' + 10);
    else return 0;
    cp = cp * (hex ? 16u : 10u) + d;
    if (cp > 0x10FFFF) return 0;
  }
  // Not a character: nothing, a surrogate, or a control other than tab,
  // line feed and carriage return (XML 1.0, production [2] Char).
  if (digits == 0 || (cp >= 0xD800 && cp <= 0xDFFF)) return 0;
  if (cp < 0x20 && cp != 0x09 && cp != 0x0A && cp != 0x0D) return 0;
  appendUtf8(out, cp);
  return semi - at + 1;
}

}  // namespace detail

inline std::string sdSsmlToText(const std::string& ssml) {
  std::string out;
  out.reserve(ssml.size());
  bool inTag = false;
  for (std::size_t i = 0; i < ssml.size();) {
    const char c = ssml[i];
    if (inTag) {
      if (c == '>') inTag = false;
      ++i;
    } else if (c == '<') {
      inTag = true;
      ++i;
    } else if (c == '&') {
      const std::size_t used = detail::decodeEntity(ssml, i, out);
      if (used == 0) {
        out += c;
        ++i;
      } else {
        i += used;
      }
    } else {
      out += c;
      ++i;
    }
  }
  return out;
}

}  // namespace tgsb_sd

#endif

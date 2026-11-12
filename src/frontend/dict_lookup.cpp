/*
TGSpeechBox — Pronunciation dictionary lookup implementation.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#include "dict_lookup.h"
#include "utf8.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// Debug logging (shares TPARSER_DEBUG_LOG toggle from text_parser.cpp).
#if 0
#include <cstdio>
#include <cstdlib>
static FILE* dlLogFile() {
  static FILE* f = nullptr;
  if (!f) {
    const char* tmp = std::getenv("TEMP");
    if (!tmp) tmp = std::getenv("TMP");
    if (!tmp) tmp = "/tmp";
    std::string path = std::string(tmp) + "/tgsb_dict_lookup.log";
    f = std::fopen(path.c_str(), "a");
  }
  return f;
}
#define DLLOG(...) do { FILE* _f = dlLogFile(); if (_f) { std::fprintf(_f, __VA_ARGS__); std::fflush(_f); } } while(0)
#else
#define DLLOG(...) ((void)0)
#endif

namespace nvsp_frontend {

// Strip Unicode variation selectors (U+FE0E text, U+FE0F emoji) from a
// UTF-8 string.  iOS often appends U+FE0F when copying emoji, which
// prevents dictionary lookup from matching the bare codepoint.
// U+FE0E = EF B8 8E, U+FE0F = EF B8 8F in UTF-8.
static std::string stripVariationSelectors(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (i + 2 < s.size() &&
        static_cast<uint8_t>(s[i])   == 0xEF &&
        static_cast<uint8_t>(s[i+1]) == 0xB8 &&
        (static_cast<uint8_t>(s[i+2]) == 0x8E ||
         static_cast<uint8_t>(s[i+2]) == 0x8F)) {
      i += 2;  // skip 3-byte sequence
      continue;
    }
    out.push_back(s[i]);
  }
  return out;
}

// Return the byte length of a Unicode punctuation codepoint at position i,
// or 0 if the byte at i is not the start of one.  Covers ASCII punctuation
// plus common multi-byte quotation marks and Spanish inverted punctuation.
static size_t unicodePunctLen(const std::string& s, size_t i) {
  auto b = static_cast<uint8_t>(s[i]);

  // ASCII punctuation — single byte.
  if (b < 0x80 && std::ispunct(b)) return 1;

  // 2-byte: C2 xx
  if (b == 0xC2 && i + 1 < s.size()) {
    auto b1 = static_cast<uint8_t>(s[i + 1]);
    if (b1 == 0xA1 || b1 == 0xAB || b1 == 0xBB || b1 == 0xBF)
      return 2;  // ¡ « » ¿
  }

  // 3-byte: E2 80 xx
  if (b == 0xE2 && i + 2 < s.size() && static_cast<uint8_t>(s[i + 1]) == 0x80) {
    auto b2 = static_cast<uint8_t>(s[i + 2]);
    // U+2018–U+201F: quotation marks (98–9F)
    // U+2026: ellipsis (A6)
    // U+2013–U+2014: en/em dash (93–94)
    if ((b2 >= 0x93 && b2 <= 0x9F) || b2 == 0xA6)
      return 3;
  }

  return 0;
}

// Lowercase a UTF-8 word for lookup (#164).  Byte-wise std::tolower folds
// only A to Z, so "Ész" never reached the entry for "ész"; and it follows
// the C locale, which NVDA's Python sets to the Windows code page, where it
// rewrites the bytes of every non-ASCII letter.  Malformed bytes are kept.
static std::string lowerUtf8(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size();) {
    const auto b = static_cast<uint8_t>(s[i]);
    size_t n = b < 0x80 ? 1 : (b >> 5) == 0x6 ? 2 : (b >> 4) == 0xE ? 3 : (b >> 3) == 0x1E ? 4 : 0;
    if (i + n > s.size()) n = 0;
    char32_t cp = n == 1 ? b : n == 2 ? (b & 0x1F) : n == 3 ? (b & 0x0F) : (b & 0x07);
    for (size_t k = 1; k < n; ++k) {
      const auto cc = static_cast<uint8_t>(s[i + k]);
      if ((cc & 0xC0) != 0x80) { n = 0; break; }
      cp = (cp << 6) | (cc & 0x3F);
    }
    if (n == 0) {  // not UTF-8: keep the byte
      out.push_back(s[i++]);
      continue;
    }
    const char32_t lower = foldCodepointLower(cp);
    if (lower != cp) out += u32ToUtf8(std::u32string(1, lower));
    else out.append(s, i, n);
    i += n;
  }
  return out;
}

// The byte in `s` where its last `n` codepoints begin.
static size_t lastCodepointsStart(const std::string& s, size_t n) {
  size_t pos = s.size();
  for (; n > 0 && pos > 0; --n) {
    --pos;
    while (pos > 0 && (static_cast<uint8_t>(s[pos]) & 0xC0) == 0x80) --pos;
  }
  return pos;
}

// Strip leading/trailing punctuation for lookup: token[lo, hi) is the word.
// Uses Unicode-aware check so curly quotes, inverted punctuation,
// en/em dashes, and ellipsis are stripped alongside ASCII punctuation.
static void wordCore(const std::string& token, size_t& lo, size_t& hi) {
  lo = 0;
  while (lo < token.size()) {
    size_t plen = unicodePunctLen(token, lo);
    if (!plen) break;
    lo += plen;
  }
  hi = token.size();
  while (hi > lo) {
    // Scan backwards: find the start of the last UTF-8 character.
    size_t back = hi - 1;
    while (back > lo && (static_cast<uint8_t>(token[back]) & 0xC0) == 0x80)
      --back;
    size_t plen = unicodePunctLen(token, back);
    if (!plen || back + plen != hi) break;
    hi = back;
  }
}

std::string dictWordKey(const std::string& token) {
  size_t lo = 0;
  size_t hi = token.size();
  wordCore(token, lo, hi);
  return lowerUtf8(std::string(token, lo, hi - lo));
}

// Whether dictReplaceInText would respell this token as one word: an entry in
// its exact case or lowercased, or a stem with one of the suffixes.  Not an
// entry that gives IPA: the word's IPA is swapped in later, word for word,
// and a hyphenated word does not always come back from the phonemizer as one.
static bool dictHasWord(const std::string& token, const PronDict& dict,
                        const std::vector<std::string>& suffixes) {
  size_t lo = 0;
  size_t hi = token.size();
  wordCore(token, lo, hi);
  if (lo >= hi) return false;
  const std::string core(token, lo, hi - lo);
  const std::string lowerKey = lowerUtf8(core);
  auto find = [&](const std::string& exact, const std::string& lower) {
    auto it = dict.entries.find(exact);
    if (it == dict.entries.end() || it->second.masked) it = dict.entries.find(lower);
    return (it == dict.entries.end() || it->second.masked) ? nullptr : &it->second;
  };
  if (const DictEntry* whole = find(stripVariationSelectors(core), lowerKey)) return whole->toIpa.empty();
  for (const auto& sfx : suffixes) {
    if (lowerKey.size() <= sfx.size()) continue;
    const size_t stemLen = lowerKey.size() - sfx.size();
    if (stemLen < 3) continue;
    if (lowerKey.compare(stemLen, sfx.size(), sfx) != 0) continue;
    size_t sfxCodepoints = 0;
    for (unsigned char c : sfx) sfxCodepoints += (c & 0xC0) != 0x80;
    const size_t coreStemLen = lastCodepointsStart(core, sfxCodepoints);
    const DictEntry* stem = find(stripVariationSelectors(core.substr(0, coreStemLen)),
                                 lowerKey.substr(0, stemLen));
    if (stem && !stem->toText.empty()) return true;
  }
  return false;
}

std::vector<size_t> dictWordHyphens(const std::string& text, const PronDict& dict,
    const std::vector<std::string>& suffixes) {
  std::vector<size_t> keep;
  if (dict.entries.empty()) return keep;
  auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
  // A word of more parts than this is not looked for.
  const size_t kMaxParts = 8;

  size_t i = 0;
  while (i < text.size()) {
    if (isSpace(text[i])) { ++i; continue; }
    const size_t tokenStart = i;
    std::vector<size_t> hyphens;
    for (; i < text.size() && !isSpace(text[i]); ++i)
      if (text[i] == '-') hyphens.push_back(i);
    const size_t tokenEnd = i;
    if (hyphens.empty()) continue;

    // Part p of the token lies between hyphen p-1 and hyphen p.  From each
    // part, the longest run of parts that is a dictionary word keeps the
    // hyphens inside it.
    const size_t parts = hyphens.size() + 1;
    for (size_t a = 0; a + 1 < parts;) {
      const size_t start = a == 0 ? tokenStart : hyphens[a - 1] + 1;
      size_t b = std::min(parts - 1, a + kMaxParts - 1);
      for (; b > a; --b) {
        const size_t end = b == parts - 1 ? tokenEnd : hyphens[b];
        if (dictHasWord(text.substr(start, end - start), dict, suffixes)) break;
      }
      for (size_t h = a; h < b; ++h) keep.push_back(hyphens[h]);
      a = b + 1;
    }
  }
  return keep;
}

std::string dictReplaceInText(const std::string& text, const PronDict& dict,
    const std::vector<std::string>& suffixes,
    std::unordered_map<std::string, std::string>* ipaOverrides) {
  if (text.empty() || dict.entries.empty()) return text;

  std::string result;
  result.reserve(text.size() + 32);

  size_t i = 0;
  while (i < text.size()) {
    // Skip whitespace.
    if (text[i] == ' ' || text[i] == '\t' ||
        text[i] == '\n' || text[i] == '\r') {
      result.push_back(text[i]);
      ++i;
      continue;
    }

    // Extract word token.
    size_t wordStart = i;
    while (i < text.size() && text[i] != ' ' && text[i] != '\t' &&
           text[i] != '\n' && text[i] != '\r') {
      ++i;
    }
    std::string token = text.substr(wordStart, i - wordStart);
    DLLOG("  token: \"%s\" (len=%zu)\n", token.c_str(), token.size());

    size_t lo = 0;
    size_t hi = token.size();
    wordCore(token, lo, hi);

    DLLOG("    lo=%zu hi=%zu core=\"%s\"\n", lo, hi,
          std::string(token, lo, hi - lo).c_str());

    if (lo >= hi) {
      result += token;
      continue;
    }

    // Case-sensitive lookup: try exact case first (e.g., "Parton" as proper
    // noun), then fallback to lowercase (e.g., "parton" as common word).
    // This lets users have different pronunciations for capitalized vs
    // lowercase variants of the same word.
    // Strip variation selectors so "🦯\uFE0F" matches dict key "🦯".
    std::string exactKey = stripVariationSelectors(
        std::string(token, lo, hi - lo));
    const std::string lowerKey = lowerUtf8(std::string(token, lo, hi - lo));

    auto it = dict.entries.find(exactKey);
    if (it == dict.entries.end() || it->second.masked) {
      // Fallback to lowercase.
      it = dict.entries.find(lowerKey);
      if (it == dict.entries.end() || it->second.masked) {
        std::string core(token, lo, hi - lo);

        // Suffix stripping: try removing each suffix (longest-first) and
        // look up the stem.  "launching" → strip "ing" → "launch" → match.
        bool suffixHandled = false;
        if (!suffixes.empty()) {
          for (const auto& sfx : suffixes) {
            if (lowerKey.size() <= sfx.size()) continue;
            size_t stemLen = lowerKey.size() - sfx.size();
            if (stemLen < 3) continue;
            if (lowerKey.compare(stemLen, sfx.size(), sfx) != 0) continue;

            // The suffix as written is as many codepoints as the lowercase
            // one, not always as many bytes ("İ" is two, its "i" one).
            size_t sfxCodepoints = 0;
            for (unsigned char c : sfx) sfxCodepoints += (c & 0xC0) != 0x80;
            const size_t coreStemLen = lastCodepointsStart(core, sfxCodepoints);

            std::string stemExact = stripVariationSelectors(
                core.substr(0, coreStemLen));
            std::string stemLower = lowerKey.substr(0, stemLen);

            auto sit = dict.entries.find(stemExact);
            if (sit == dict.entries.end() || sit->second.masked)
              sit = dict.entries.find(stemLower);
            if (sit == dict.entries.end() || sit->second.masked) continue;

            // For suffix-stripped matches, always use text respelling so
            // eSpeak phonemizes the suffix naturally in context (e.g. "'s"
            // becomes /z/ not "ess").  IPA-only entries (no toText) are
            // skipped — they need explicit suffixed dict entries.
            if (sit->second.toText.empty()) continue;

            std::string origSuffix = core.substr(coreStemLen);
            std::string rep;
            rep.reserve(sit->second.toText.size() + origSuffix.size());
            for (char c : sit->second.toText)
              rep += (c == '-') ? ' ' : c;
            rep += origSuffix;
            DLLOG("  suffixReplace: \"%s\" -> \"%s\"\n",
                  core.c_str(), rep.c_str());
            result += token.substr(0, lo);
            result += rep;
            result += token.substr(hi);
            suffixHandled = true;
            break;
          }
        }
        if (suffixHandled) continue;

        // Token not found as a whole.  If it contains hyphens, split into
        // hyphen-separated parts and look up each part individually so that
        // "this-pentagon-official" still matches a dict entry for "pentagon"
        // even when the full hyphenated string has no entry.
        if (core.find('-') != std::string::npos) {
          DLLOG("  hyphen-split fallback for \"%s\"\n", core.c_str());
          std::string rebuilt;
          rebuilt.reserve(core.size() + 16);
          size_t pos = 0;
          while (pos < core.size()) {
            size_t dash = core.find('-', pos);
            if (dash == std::string::npos) dash = core.size();
            std::string part = core.substr(pos, dash - pos);
            if (!part.empty()) {
              const std::string partLower = lowerUtf8(part);
              std::string partExact = stripVariationSelectors(part);
              auto pit = dict.entries.find(partExact);
              if (pit == dict.entries.end() || pit->second.masked)
                pit = dict.entries.find(partLower);
              if (pit != dict.entries.end() && !pit->second.masked) {
                if (ipaOverrides && !pit->second.toIpa.empty()) {
                  (*ipaOverrides)[partLower] = pit->second.toIpa;
                  rebuilt += part;
                } else {
                  std::string rep;
                  for (char c : pit->second.toText)
                    rep += (c == '-') ? ' ' : c;
                  DLLOG("    part \"%s\" -> \"%s\"\n", part.c_str(), rep.c_str());
                  rebuilt += rep;
                }
              } else {
                rebuilt += part;
              }
            }
            if (dash < core.size()) {
              rebuilt += '-';
              pos = dash + 1;
            } else {
              pos = dash;
            }
          }
          result += token.substr(0, lo);
          result += rebuilt;
          result += token.substr(hi);
        } else {
          result += token;
        }
        continue;
      }
    }

    // If this entry has a toIpa override, don't replace the text — leave
    // the original word so eSpeak phonemizes it naturally.  Record the
    // override for downstream IPA splicing in runTextParser.
    if (ipaOverrides && !it->second.toIpa.empty()) {
      DLLOG("  dictIpaOverride: \"%s\" -> ipa \"%s\"\n",
            lowerKey.c_str(), it->second.toIpa.c_str());
      (*ipaOverrides)[lowerKey] = it->second.toIpa;
      result += token;  // keep original word
      continue;
    }

    // Convert hyphens to spaces in toText — users write them as pronunciation
    // guides ("shi-kah-go") but eSpeak treats hyphens inconsistently as word
    // boundaries in isolation vs phrase context.  Converting to spaces makes
    // "shi-kah-go" and "shi kah go" produce identical results.
    std::string replacement;
    replacement.reserve(it->second.toText.size());
    for (char c : it->second.toText) {
      replacement += (c == '-') ? ' ' : c;
    }

    DLLOG("  dictReplace: \"%s\" -> \"%s\"\n",
          it->second.fromText.c_str(), replacement.c_str());

    // Replace core with cleaned toText, preserving surrounding punctuation.
    result += token.substr(0, lo);
    result += replacement;
    result += token.substr(hi);
  }

  return result;
}

}  // namespace nvsp_frontend

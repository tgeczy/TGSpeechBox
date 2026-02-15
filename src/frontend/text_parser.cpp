/*
TGSpeechBox — Text parser with CMU Dict stress correction.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

// =============================================================================
// Text Parser — pre-IPA-engine text-level corrections
// =============================================================================
//
// Sits between callers and convertIpaToTokens().  Receives both the original
// text and eSpeak's IPA output, applies word-level plugins, and returns
// corrected IPA.  The IPA engine never knows text was involved.
//
// Current plugin: stress lookup (CMU Dict → stress digit patterns).
// Future plugins (numbers, function-word reduction) slot in at the end
// of runTextParser().

#include "text_parser.h"
#include "utf8.h"

#include <algorithm>
#include <cstdint>
#include <sstream>

namespace nvsp_frontend {

namespace {

// ── IPA vowel codepoint set ────────────────────────────────────────────────
//
// Used for counting vowel nuclei in an IPA chunk.  Consecutive vowels
// (+ length mark ː) count as a single nucleus (handles diphthongs).

static bool isIpaVowel(char32_t c) {
  switch (c) {
    // Basic Latin vowels
    case U'a': case U'e': case U'i': case U'o': case U'u': case U'y':
    // IPA-specific vowels
    case U'\u0251':  // ɑ  open back unrounded
    case U'\u00E6':  // æ  near-open front unrounded
    case U'\u025B':  // ɛ  open-mid front unrounded
    case U'\u026A':  // ɪ  near-close front unrounded
    case U'\u0254':  // ɔ  open-mid back rounded
    case U'\u0259':  // ə  schwa
    case U'\u028A':  // ʊ  near-close back rounded
    case U'\u028C':  // ʌ  open-mid back unrounded
    case U'\u0252':  // ɒ  open back rounded
    case U'\u025C':  // ɜ  open-mid central unrounded
    case U'\u0250':  // ɐ  near-open central
    case U'\u0264':  // ɤ  close-mid back unrounded
    case U'\u0275':  // ɵ  close-mid central rounded
    case U'\u0258':  // ɘ  close-mid central unrounded
    case U'\u025E':  // ɞ  open-mid central rounded
    case U'\u0276':  // ɶ  open front rounded
    case U'\u0268':  // ɨ  close central unrounded
    case U'\u0289':  // ʉ  close central rounded
    case U'\u026F':  // ɯ  close back unrounded
    case U'\u025D':  // ɝ  r-colored schwa
    case U'\u025A':  // ɚ  r-colored schwa (mid central)
    case U'\u00F8':  // ø  close-mid front rounded
    case U'\u1D7B':  // ᵻ  near-close central unrounded (eSpeak reduced vowel)
    case U'\u1D7F':  // ᵿ  near-close central rounded (eSpeak reduced vowel)
      return true;
    default:
      return false;
  }
}

// Reduced vowels that cannot meaningfully carry primary stress.
// Putting ˈ on these is counterproductive — the vowel quality is already
// committed to "reduced," so stress won't sound stressed.
static bool isReducedVowel(char32_t c) {
  switch (c) {
    case U'\u0259':  // ə  schwa
    case U'\u0250':  // ɐ  near-open central
    case U'\u1D7B':  // ᵻ  near-close central unrounded (eSpeak)
    case U'\u1D7F':  // ᵿ  near-close central rounded (eSpeak)
    case U'\u025A':  // ɚ  r-colored schwa
      return true;
    default:
      return false;
  }
}

static bool isLengthMark(char32_t c) {
  return c == U'\u02D0';  // ː
}

static bool isTieBar(char32_t c) {
  return c == U'\u0361';  // ◌͡
}

static bool isSyllabicMark(char32_t c) {
  return c == U'\u0329';  // ◌̩  combining vertical line below
}

static bool isStressMark(char32_t c) {
  return c == U'\u02C8' || c == U'\u02CC';  // ˈ or ˌ
}

// ── Word splitting ─────────────────────────────────────────────────────────

static std::vector<std::string> splitOnWhitespace(const std::string& s) {
  std::vector<std::string> words;
  std::istringstream ss(s);
  std::string w;
  while (ss >> w) {
    words.push_back(std::move(w));
  }
  return words;
}

// Split IPA on spaces.  eSpeak separates word-level IPA with spaces.
static std::vector<std::string> splitIpaWords(const std::string& ipa) {
  std::vector<std::string> chunks;
  size_t start = 0;
  while (start < ipa.size()) {
    size_t sp = ipa.find(' ', start);
    if (sp == std::string::npos) {
      chunks.push_back(ipa.substr(start));
      break;
    }
    if (sp > start) {
      chunks.push_back(ipa.substr(start, sp - start));
    }
    start = sp + 1;
  }
  return chunks;
}

// ── Lowercase (ASCII only — text words are English) ────────────────────────

static std::string asciiLower(const std::string& s) {
  std::string out = s;
  for (auto& c : out) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
  }
  return out;
}

// Strip punctuation from the edges of a text word (e.g. "hello," → "hello").
static std::string stripPunct(const std::string& s) {
  size_t start = 0;
  while (start < s.size() && !std::isalpha(static_cast<unsigned char>(s[start]))) ++start;
  size_t end = s.size();
  while (end > start && !std::isalpha(static_cast<unsigned char>(s[end - 1]))) --end;
  if (start >= end) return {};
  return s.substr(start, end - start);
}

// ── Vowel nucleus counting ─────────────────────────────────────────────────

struct NucleusInfo {
  size_t start;  // byte offset of the first vowel codepoint in the nucleus
};

// Find all vowel nuclei in a u32 IPA chunk.  Consecutive vowels + ː = 1.
// A tie bar (U+0361) after a vowel binds the next character into the same
// nucleus (e.g. e͡ɪ = one diphthong nucleus, not two).
// A syllabic mark (U+0329) after a consonant makes it a nucleus (n̩, l̩, m̩).
static std::vector<NucleusInfo> findNuclei(const std::u32string& u32) {
  std::vector<NucleusInfo> nuclei;
  bool inVowel = false;
  for (size_t i = 0; i < u32.size(); ++i) {
    if (isTieBar(u32[i]) && inVowel) {
      // Tie bar extends the nucleus — skip it and the next character.
      if (i + 1 < u32.size()) ++i;
      continue;
    }
    // Syllabic consonant: consonant + U+0329 = nucleus.
    // Check if the NEXT character is a syllabic mark.
    if (!isIpaVowel(u32[i]) && !inVowel &&
        i + 1 < u32.size() && isSyllabicMark(u32[i + 1])) {
      nuclei.push_back({i});
      ++i;  // skip the syllabic mark
      inVowel = false;
      continue;
    }
    if (isIpaVowel(u32[i])) {
      if (!inVowel) {
        nuclei.push_back({i});
        inVowel = true;
      }
    } else if (isLengthMark(u32[i]) && inVowel) {
      // Length mark extends the nucleus — stay in vowel state.
    } else {
      inVowel = false;
    }
  }
  return nuclei;
}

// ── Stress remapping ───────────────────────────────────────────────────────

// Remove all ˈ and ˌ from a u32 string.
static std::u32string stripStress(const std::u32string& s) {
  std::u32string out;
  out.reserve(s.size());
  for (char32_t c : s) {
    if (!isStressMark(c)) out.push_back(c);
  }
  return out;
}

// Insert stress marks into an IPA chunk according to a digit pattern.
// Places ˈ/ˌ immediately before each vowel nucleus — matches eSpeak's
// convention and avoids the onset-legality problem entirely.
static std::u32string applyStressPattern(
    const std::u32string& stripped,
    const std::vector<NucleusInfo>& nuclei,
    const std::vector<int>& pattern)
{
  struct Insertion {
    size_t pos;
    char32_t mark;
  };
  std::vector<Insertion> insertions;

  for (size_t n = 0; n < nuclei.size() && n < pattern.size(); ++n) {
    int digit = pattern[n];
    if (digit == 0) continue;  // unstressed — no mark

    char32_t mark = (digit == 1) ? U'\u02C8' : U'\u02CC';  // ˈ or ˌ

    // Insert directly before the vowel nucleus — no onset walk-back.
    // Guard: never insert after a tie bar (would split a ligature).
    size_t pos = nuclei[n].start;
    if (pos > 0 && isTieBar(stripped[pos - 1])) continue;
    insertions.push_back({pos, mark});
  }

  // Apply insertions from back to front to preserve positions.
  std::u32string result = stripped;
  std::sort(insertions.begin(), insertions.end(),
            [](const Insertion& a, const Insertion& b) { return a.pos > b.pos; });
  for (const auto& ins : insertions) {
    result.insert(result.begin() + static_cast<ptrdiff_t>(ins.pos), ins.mark);
  }
  return result;
}

// ── Onset maximization ───────────────────────────────────────────────────
//
// Insert IPA '.' syllable boundaries at linguistically correct positions
// using the Maximal Onset Principle: for a consonant cluster between two
// vowel nuclei, assign the longest suffix that is a legal onset to the
// following syllable.

static std::u32string applySyllableBoundaries(
    const std::u32string& stripped,
    const std::vector<NucleusInfo>& nuclei,
    const std::vector<std::u32string>& legalOnsets)
{
  // Build insertion list (positions where '.' goes).
  std::vector<size_t> dots;

  for (size_t n = 0; n + 1 < nuclei.size(); ++n) {
    // Find end of current nucleus (first non-vowel, non-length-mark after
    // nucleus start).
    size_t codaStart = nuclei[n].start;
    {
      bool inV = false;
      for (size_t j = nuclei[n].start; j < stripped.size(); ++j) {
        if (isIpaVowel(stripped[j])) {
          inV = true;
        } else if (isLengthMark(stripped[j]) && inV) {
          // length mark extends vowel
        } else if (isTieBar(stripped[j]) && inV) {
          if (j + 1 < stripped.size()) ++j;  // skip tied char
        } else if (isSyllabicMark(stripped[j])) {
          // skip
        } else {
          codaStart = j;
          break;
        }
      }
      if (codaStart == nuclei[n].start) continue;  // no consonants between
    }

    size_t onsetEnd = nuclei[n + 1].start;  // just before next nucleus

    if (codaStart >= onsetEnd) continue;  // adjacent nuclei (diphthong)

    // Extract the consonant cluster.
    std::u32string cluster(stripped.begin() + codaStart,
                           stripped.begin() + onsetEnd);

    // Try suffix lengths from longest to 2 (single consonant onset is
    // always legal — that's the default fallback).
    size_t onsetLen = 1;  // default: one consonant goes to next syllable
    for (size_t tryLen = cluster.size(); tryLen >= 2; --tryLen) {
      std::u32string suffix(cluster.end() - tryLen, cluster.end());
      for (const auto& legal : legalOnsets) {
        if (suffix == legal) {
          onsetLen = tryLen;
          goto found;
        }
      }
    }
    found:

    // Insert '.' before the onset.
    size_t dotPos = onsetEnd - onsetLen;
    if (dotPos > codaStart || onsetLen == cluster.size()) {
      // Only insert if there's at least one coda consonant, OR the whole
      // cluster is a legal onset (all consonants go to next syllable).
      dots.push_back(dotPos);
    } else {
      // Fallback: put dot after first consonant.
      dots.push_back(codaStart + 1);
    }
  }

  if (dots.empty()) return stripped;

  // Apply dots from back to front to preserve positions.
  std::u32string result = stripped;
  std::sort(dots.begin(), dots.end(), std::greater<size_t>());
  for (size_t pos : dots) {
    result.insert(result.begin() + static_cast<ptrdiff_t>(pos), U'.');
  }
  return result;
}

// Convert a u32 string back to UTF-8.
static std::string u32ToUtf8(const std::u32string& s) {
  std::string result;
  result.reserve(s.size() * 3);
  for (char32_t c : s) {
    if (c < 0x80) {
      result.push_back(static_cast<char>(c));
    } else if (c < 0x800) {
      result.push_back(static_cast<char>(0xC0 | (c >> 6)));
      result.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else if (c < 0x10000) {
      result.push_back(static_cast<char>(0xE0 | (c >> 12)));
      result.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
      result.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else {
      result.push_back(static_cast<char>(0xF0 | (c >> 18)));
      result.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
      result.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
      result.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    }
  }
  return result;
}

// Apply stress correction to a single IPA word chunk.
// Returns the original chunk unchanged if no correction applies.
static std::string correctStress(
    const std::string& textWord,
    const std::string& ipaChunk,
    const std::unordered_map<std::string, std::vector<int>>& dict,
    const std::vector<std::u32string>& legalOnsets)
{
  // Lowercase and strip punctuation from text word.
  const std::string key = asciiLower(stripPunct(textWord));
  if (key.empty()) return ipaChunk;

  // Lookup.
  auto it = dict.find(key);
  if (it == dict.end()) return ipaChunk;

  const std::vector<int>& pattern = it->second;

  // Monosyllables: never override eSpeak's contextual stress on single-syllable
  // words ("for", "the", "a", "blank", etc.).  Only correct multi-syllable words.
  if (pattern.size() <= 1) return ipaChunk;

  // Convert IPA chunk to u32 for codepoint-level processing.
  std::u32string u32 = utf8ToU32(ipaChunk);

  // Strip existing stress marks before counting nuclei.
  std::u32string stripped = stripStress(u32);

  // Count vowel nuclei.
  auto nuclei = findNuclei(stripped);
  if (nuclei.size() != pattern.size()) {
    // Mismatch — eSpeak segmented differently than CMU Dict expected.
    // Do nothing; eSpeak's stress stands.
    return ipaChunk;
  }

  // Safety: never place primary stress (ˈ) on a reduced vowel nucleus.
  // eSpeak chose ə/ᵻ/ɐ/ɚ because it already decided that syllable is
  // reduced — forcing stress onto it can't fix the vowel quality and
  // sounds wrong.  Skip the entire word if any primary lands on reduced.
  for (size_t n = 0; n < nuclei.size() && n < pattern.size(); ++n) {
    if (pattern[n] == 1 && isReducedVowel(stripped[nuclei[n].start])) {
      return ipaChunk;
    }
  }

  // Apply syllable boundaries (dots) first, then re-find nuclei, then stress.
  // Dots must go on the stripped (stress-free) string so positions are clean.
  if (!legalOnsets.empty() && nuclei.size() >= 2) {
    std::u32string dotted = applySyllableBoundaries(stripped, nuclei, legalOnsets);
    auto dottedNuclei = findNuclei(dotted);
    std::u32string corrected = applyStressPattern(dotted, dottedNuclei, pattern);
    return u32ToUtf8(corrected);
  }

  // No onset table or monosyllable — just stress.
  std::u32string corrected = applyStressPattern(stripped, nuclei, pattern);
  return u32ToUtf8(corrected);
}

}  // namespace

// =============================================================================
// Public API
// =============================================================================

std::string runTextParser(
    const std::string& text,
    const std::string& ipa,
    const std::unordered_map<std::string, std::vector<int>>& stressDict,
    const std::vector<std::u32string>& legalOnsets)
{
  if (text.empty() || stressDict.empty()) return ipa;

  auto textWords = splitOnWhitespace(text);
  auto ipaChunks = splitIpaWords(ipa);

  if (textWords.empty() || ipaChunks.empty()) return ipa;

  // When word counts don't match, skip the entire utterance.  Numbers,
  // abbreviations, and contractions cause eSpeak to expand or merge words,
  // making positional alignment unreliable.  A wrong match that passes
  // the nucleus guard (e.g. "driver" vs "DP") is worse than no correction.
  if (textWords.size() != ipaChunks.size()) return ipa;

  bool anyChange = false;

  for (size_t i = 0; i < textWords.size(); ++i) {
    std::string corrected = correctStress(textWords[i], ipaChunks[i], stressDict, legalOnsets);
    if (corrected != ipaChunks[i]) {
      ipaChunks[i] = std::move(corrected);
      anyChange = true;
    }
  }

  if (!anyChange) return ipa;

  // Reassemble.
  std::string result;
  for (size_t i = 0; i < ipaChunks.size(); ++i) {
    if (i > 0) result.push_back(' ');
    result += ipaChunks[i];
  }
  return result;
}

}  // namespace nvsp_frontend

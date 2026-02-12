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
    case U'\u00F8':  // ø  close-mid front rounded
      return true;
    default:
      return false;
  }
}

static bool isLengthMark(char32_t c) {
  return c == U'\u02D0';  // ː
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
static std::vector<NucleusInfo> findNuclei(const std::u32string& u32) {
  std::vector<NucleusInfo> nuclei;
  bool inVowel = false;
  for (size_t i = 0; i < u32.size(); ++i) {
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
// For each nucleus, walks backward from the nucleus start to find the
// syllable onset (first consonant before any vowel), then inserts the
// stress mark there.  This matches eSpeak's convention.
static std::u32string applyStressPattern(
    const std::u32string& stripped,
    const std::vector<NucleusInfo>& nuclei,
    const std::vector<int>& pattern)
{
  // Build a list of (insert_position, stress_char) pairs.
  // We process from the end to avoid invalidating positions.
  struct Insertion {
    size_t pos;
    char32_t mark;
  };
  std::vector<Insertion> insertions;

  for (size_t n = 0; n < nuclei.size() && n < pattern.size(); ++n) {
    int digit = pattern[n];
    if (digit == 0) continue;  // unstressed — no mark

    char32_t mark = (digit == 1) ? U'\u02C8' : U'\u02CC';  // ˈ or ˌ

    // Walk backward from nucleus start to find syllable onset.
    // The onset is the run of non-vowel, non-stress-mark, non-space
    // codepoints immediately before the vowel.
    size_t pos = nuclei[n].start;
    while (pos > 0) {
      char32_t prev = stripped[pos - 1];
      if (isIpaVowel(prev) || isLengthMark(prev) || isStressMark(prev) || prev == U' ') {
        break;
      }
      --pos;
    }

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

// Apply stress correction to a single IPA word chunk.
// Returns the original chunk unchanged if no correction applies.
static std::string correctStress(
    const std::string& textWord,
    const std::string& ipaChunk,
    const std::unordered_map<std::string, std::vector<int>>& dict)
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

  // Apply the new stress pattern.
  std::u32string corrected = applyStressPattern(stripped, nuclei, pattern);

  // Convert back to UTF-8.
  std::string result;
  result.reserve(corrected.size() * 3);
  for (char32_t c : corrected) {
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

}  // namespace

// =============================================================================
// Public API
// =============================================================================

std::string runTextParser(
    const std::string& text,
    const std::string& ipa,
    const std::unordered_map<std::string, std::vector<int>>& stressDict)
{
  if (text.empty() || stressDict.empty()) return ipa;

  auto textWords = splitOnWhitespace(text);
  auto ipaChunks = splitIpaWords(ipa);

  if (textWords.empty() || ipaChunks.empty()) return ipa;

  // Simple 1:1 zip when counts match.
  // When counts differ, walk both lists and match what we can.
  const size_t n = std::min(textWords.size(), ipaChunks.size());
  bool anyChange = false;

  for (size_t i = 0; i < n; ++i) {
    std::string corrected = correctStress(textWords[i], ipaChunks[i], stressDict);
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

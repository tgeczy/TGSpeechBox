/*
TGSpeechBox — Pre-eSpeak text transforms (numbers, dates, compounds, years).
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#ifndef TGSB_FRONTEND_TEXT_PREPARE_H
#define TGSB_FRONTEND_TEXT_PREPARE_H

#include "pack.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace nvsp_frontend {

// Expand a numeric text word into spoken-word components using language-pack
// YAML rules.  Returns empty vector if rules are disabled, the string isn't
// a pure integer, or the word lists are too short.
// Commas are stripped ("1,000" -> 1000).
// Leading zeros trigger digit-by-digit reading ("07" -> "zero seven").
std::vector<std::string> expandNumber(
    const std::string& numStr,
    const NumberExpansionRules& rules);

// Split text words at digit->alpha boundaries and around symbols that eSpeak
// expands into spoken words (%, $, #, +, &, @).
// "25Increasing" -> ["25", "Increasing"]
// "100%"         -> ["100", "%"]
void splitMixedTokens(std::vector<std::string>& words);

// Split compound words using a compound map, inserting \x1F (Unit Separator)
// between halves instead of space.
std::string splitCompoundsInText(
    const std::string& text,
    const std::unordered_map<std::string, std::vector<std::string>>& compoundMap);

// Insert ordinal suffixes on bare day numbers adjacent to English month names.
// "June 6" -> "June 6th" (only backward: month then number).
std::string insertDateOrdinals(const std::string& text);

// Expand time patterns so eSpeak reads times naturally.
// "6:03" -> "6 oh 3", "12:45" -> "12 45", "5:00" -> "5 o'clock".
// Handles both raw ":" and NVDA's "colon" expansion.
std::string expandTimes(const std::string& text, const std::string& ohDigit);

// Separate digit-hyphen-digit so year splitting can process both halves.
// "2024-2025" -> "2024 2025" (raw), "2024 dash-2025" -> "2024 dash 2025" (NVDA).
std::string separateHyphenatedNumbers(const std::string& text);

// Split 4-digit numbers into two 2-digit pairs for year-style reading.
// "1995" -> "19 95" ("nineteen ninety-five").
std::string splitYears(const std::string& text, const std::string& ohDigit);

// Spanish spelling (#145): a word-initial "y"/"Y" followed by a lowercase
// consonant is the vowel i ("Yndio" -> "Indio", "Ybarra" -> "Ibarra"),
// which eSpeak reads as the stop [ɟ] with the stress on the wrong syllable.
// All-caps words ("YPF") are left alone: they are spelled out.
std::string initialYBeforeConsonantAsI(const std::string& text);

// Spanish (#146): "tw" before a vowel becomes "tu" ("Twain" -> "Tuain"),
// keeping the case of the letters.
std::string twBeforeVowelAsTu(const std::string& text);

// Spanish (#146): an all-caps word, or the all-caps tail of a word
// ("WinRAR"), that Spanish syllables could carry is lowercased so eSpeak
// reads it as a word instead of spelling it ("MAS", "UN", "WEB").  Words
// Spanish couldn't pronounce ("PC", "DNI", "BBC", "ONG") and Roman
// numerals are left to be spelled.
std::string pronounceableCapsAsWords(const std::string& text);

// One clause of host text (#133): where it starts and ends (bytes, trailing
// whitespace excluded), where the next one starts, the clause type the
// frontend's pitch passes read ('.', ',', '?', '!', ':' or ';'), and the pause
// after it (0 none, 1 comma, 2 sentence).  Every host splits with this, so
// every platform pauses at the same places.
struct ClauseSpan {
  size_t start = 0;
  size_t end = 0;
  size_t next = 0;
  char type = '.';
  int pauseClass = 0;
};

// The next clause of UTF-8 `text` at or after byte `pos`; false when only
// whitespace is left.  The rule is the NVDA driver's (split after . ? ! , : ;
// and an ellipsis when a space, the end, or for an ellipsis a word follows;
// never after a dot that follows a digit), plus a comma-like break at dashes
// between words, around parenthesised or bracketed text, and before Spanish
// inverted question and exclamation marks.
bool nextClause(std::string_view text, size_t pos, ClauseSpan& out);

// The pause after a clause, in ms, for a host's pause setting (0 off,
// 1 short, 2 long).
double clausePauseMs(int pauseClass, int pauseMode);

}  // namespace nvsp_frontend

#endif  // TGSB_FRONTEND_TEXT_PREPARE_H

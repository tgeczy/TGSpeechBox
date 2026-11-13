/*
TGSpeechBox — Text parser interface.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#ifndef TGSB_FRONTEND_TEXT_PARSER_H
#define TGSB_FRONTEND_TEXT_PARSER_H

#include "pack.h"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace nvsp_frontend {

// Run text-level plugins on IPA before it enters the IPA engine.
//
// Plugins (applied in order):
//   1. Number expansion — expand numeric text words ("24" → "twenty four")
//      using YAML-driven rules so alignment is 1:1 with eSpeak's IPA output.
//   2. Compound stress fallback — if a word is missing from stressDict but
//      present in compoundMap, apply default compound stress (primary on
//      first nucleus, secondary on last).
//   3. Stress lookup — if a word appears in stressDict, reposition stress
//      marks (ˈ ˌ) to match the dictionary pattern.
//
// If text is empty, stressDict is empty, or no corrections apply, the
// original IPA is returned unchanged.  Every failure mode is "do nothing."
std::string runTextParser(
    const std::string& text,
    const std::string& ipa,
    const std::unordered_map<std::string, std::vector<int>>& stressDict,
    const std::unordered_map<std::string, std::vector<std::string>>& compoundMap,
    const std::vector<std::u32string>& legalOnsets,
    const NumberExpansionRules& numberRules,
    const std::unordered_map<std::string, std::string>& ipaOverrides = {});

// Pre-eSpeak text normalization.
//
// Applies text-level transforms before eSpeak phonemization:
//   0. Pronunciation dictionary — "monster" → "mahnster" (highest priority)
//   1. Compound splitting — "dogfood" → "dog\x1Ffood" (if in compoundMap)
//   2. Date ordinals — "June 6" → "June 6th" (English only)
//   3. Year splitting — "1995" → "19 95" (if yearSplitting enabled)
//
// The modified text should be fed to eSpeak so it phonemizes correctly.
// If no transforms apply, returns the original text unchanged.
std::string prepareTextForEspeak(
    const std::string& text,
    const std::unordered_map<std::string, std::vector<std::string>>& compoundMap,
    const PronDict& pronDict,
    const std::unordered_set<std::string>& disabledDictTypes,
    const std::string& langTag,
    bool yearSplitting,
    bool thousandsSeparatorCommaToSpace = false,
    const std::string& ohDigit = "",
    const std::vector<std::string>& dictSuffixes = {},
    std::unordered_map<std::string, std::string>* ipaOverrides = nullptr,
    const std::vector<std::string>& twelveHourClockWords = {},
    const std::string& decimalSeparator = "");

}  // namespace nvsp_frontend

#endif  // TGSB_FRONTEND_TEXT_PARSER_H

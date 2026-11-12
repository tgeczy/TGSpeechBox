/*
TGSpeechBox — Pronunciation dictionary lookup.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#ifndef TGSB_FRONTEND_DICT_LOOKUP_H
#define TGSB_FRONTEND_DICT_LOOKUP_H

#include "pack.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace nvsp_frontend {

// Replace words in text using the pronunciation dictionary.
// Case-insensitive lookup, preserves surrounding punctuation.
// Hyphens in replacement text are converted to spaces for eSpeak
// consistency between isolation and phrase context.
// Masked entries are skipped.
//
// If ipaOverrides is non-null, words whose dict entry has a non-empty
// toIpa are NOT replaced in the text (left for eSpeak to phonemize
// naturally).  Instead, their lowercase key and toIpa value are
// recorded in the map for downstream IPA splicing.
//
// suffixes: per-language list of suffixes (pre-sorted longest-first).
// When an exact/lowercase lookup fails, each suffix is tried: if the
// word minus the suffix matches a dict entry, the replacement is used
// with the suffix reattached (text respelling) or split off for
// separate eSpeak phonemization (IPA injection).
std::string dictReplaceInText(const std::string& text, const PronDict& dict,
    const std::vector<std::string>& suffixes = {},
    std::unordered_map<std::string, std::string>* ipaOverrides = nullptr);

// The hyphens of `text` (byte offsets, ascending) that lie inside a word
// dictReplaceInText has a respelling for, such as "wi-fi" or "x-ray", with
// a suffix too ("x-rays").  In a longer hyphenated word the longest entry
// wins: "wi-fi-enabled" keeps the hyphen of "wi-fi".  Text preparation
// splits words at the other hyphens before the dictionary runs (#168).
// An entry that gives IPA for a hyphenated word is not counted.
std::vector<size_t> dictWordHyphens(const std::string& text, const PronDict& dict,
    const std::vector<std::string>& suffixes = {});

// The key dictReplaceInText records a word's IPA override under: the word
// without the punctuation at its edges, lowercased in any alphabet.  Empty
// for a token that is only punctuation.
std::string dictWordKey(const std::string& token);

}  // namespace nvsp_frontend

#endif  // TGSB_FRONTEND_DICT_LOOKUP_H

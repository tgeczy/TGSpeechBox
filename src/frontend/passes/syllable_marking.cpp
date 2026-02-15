/*
TGSpeechBox — Syllable marking pass.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

// =============================================================================
// Syllable Marking Pass — assign syllableIndex to every token
// =============================================================================
//
// Walks the token stream and converts the existing syllableStart booleans
// (set by the IPA engine) into sequential per-word syllableIndex values.
// This lets downstream passes (boundary smoothing, coarticulation) distinguish
// within-syllable transitions (one gesture) from cross-syllable transitions
// (new gesture).

#include "syllable_marking.h"

namespace nvsp_frontend::passes {

bool runSyllableMarking(
    PassContext& ctx,
    std::vector<Token>& tokens,
    std::string& outError) {
  (void)ctx;
  (void)outError;

  int currentSyllable = -1;

  for (auto& t : tokens) {
    if (t.silence || !t.def) {
      t.syllableIndex = -1;
      continue;
    }

    if (t.wordStart) {
      currentSyllable = 0;
    } else if (t.syllableStart) {
      ++currentSyllable;
    }

    t.syllableIndex = currentSyllable;
  }

  return true;
}

}  // namespace nvsp_frontend::passes

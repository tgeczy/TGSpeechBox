/*
TGSpeechBox — Syllable marking pass interface.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#ifndef TGSB_FRONTEND_PASSES_SYLLABLE_MARKING_H
#define TGSB_FRONTEND_PASSES_SYLLABLE_MARKING_H

#include "pass_common.h"

namespace nvsp_frontend::passes {

// Converts existing syllableStart booleans into sequential syllableIndex
// integers per word, enabling downstream passes to distinguish
// within-syllable from cross-syllable transitions.
bool runSyllableMarking(
    PassContext& ctx,
    std::vector<Token>& tokens,
    std::string& outError);

}  // namespace nvsp_frontend::passes

#endif  // TGSB_FRONTEND_PASSES_SYLLABLE_MARKING_H

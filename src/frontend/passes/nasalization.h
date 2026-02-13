/*
TGSpeechBox — Nasalization pass interface.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#ifndef TGSB_FRONTEND_PASSES_NASALIZATION_H
#define TGSB_FRONTEND_PASSES_NASALIZATION_H

#include "pass_common.h"

namespace nvsp_frontend::passes {

// Anticipatory nasalization: slightly nasalize a vowel before a nasal consonant.
bool runNasalization(
    PassContext& ctx,
    std::vector<Token>& tokens,
    std::string& outError);

}  // namespace nvsp_frontend::passes

#endif  // TGSB_FRONTEND_PASSES_NASALIZATION_H

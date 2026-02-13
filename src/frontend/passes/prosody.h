/*
TGSpeechBox — Prosody pass interface.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#ifndef TGSB_FRONTEND_PASSES_PROSODY_H
#define TGSB_FRONTEND_PASSES_PROSODY_H

#include "pass_common.h"

namespace nvsp_frontend::passes {

// Prosody rules that are easier to express at the token level
// (e.g., phrase-final lengthening).
bool runProsody(
    PassContext& ctx,
    std::vector<Token>& tokens,
    std::string& outError);

}  // namespace nvsp_frontend::passes

#endif  // TGSB_FRONTEND_PASSES_PROSODY_H

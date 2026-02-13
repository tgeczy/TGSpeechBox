/*
TGSpeechBox — Microprosody pass interface.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#ifndef TGSB_FRONTEND_PASSES_MICROPROSODY_H
#define TGSB_FRONTEND_PASSES_MICROPROSODY_H

#include "pass_common.h"

namespace nvsp_frontend::passes {

// Small pitch adjustments that improve “speechy” feel:
// e.g. voiceless consonants slightly raise the next vowel onset.
bool runMicroprosody(
    PassContext& ctx,
    std::vector<Token>& tokens,
    std::string& outError);

}  // namespace nvsp_frontend::passes

#endif  // TGSB_FRONTEND_PASSES_MICROPROSODY_H

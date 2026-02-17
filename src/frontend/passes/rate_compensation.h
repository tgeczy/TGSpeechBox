/*
TGSpeechBox — Rate compensation pass interface.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#ifndef TGSB_FRONTEND_PASSES_RATE_COMPENSATION_H
#define TGSB_FRONTEND_PASSES_RATE_COMPENSATION_H

#include "pass_common.h"

namespace nvsp_frontend::passes {

// Rate compensation: enforce perceptual duration floors at high speed.
// Replaces the old "reduction" pass. Absorbs rate-dependent schwa reduction.
bool runRateCompensation(
    PassContext& ctx,
    std::vector<Token>& tokens,
    std::string& outError);

}  // namespace nvsp_frontend::passes

#endif  // TGSB_FRONTEND_PASSES_RATE_COMPENSATION_H

/*
TGSpeechBox — Trajectory limiting pass interface.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#pragma once

#include "pass_common.h"

namespace nvsp_frontend::passes {

// Trajectory limiting.
//
// Caps how quickly selected formant targets are allowed to move at token
// boundaries by increasing the incoming token's fadeMs (crossfade time).
bool runTrajectoryLimit(PassContext& ctx, std::vector<Token>& tokens, std::string& outError);

}  // namespace nvsp_frontend::passes

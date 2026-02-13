/*
TGSpeechBox — Pass pipeline interface.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#ifndef TGSB_FRONTEND_PASSES_PASS_PIPELINE_H
#define TGSB_FRONTEND_PASSES_PASS_PIPELINE_H

#include "pass_common.h"

namespace nvsp_frontend {

// Run all registered passes for the given stage.
// Returns false and sets outError if any pass fails.
bool runPasses(
    PassContext& ctx,
    PassStage stage,
    std::vector<Token>& tokens,
    std::string& outError);

}  // namespace nvsp_frontend

#endif  // TGSB_FRONTEND_PASSES_PASS_PIPELINE_H

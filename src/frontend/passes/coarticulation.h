#ifndef NVSP_FRONTEND_PASSES_COARTICULATION_H
#define NVSP_FRONTEND_PASSES_COARTICULATION_H

#include "pass_common.h"

namespace nvsp_frontend::passes {

// Locus-based coarticulation tweaks (formant targets + optional velar pinch).
bool runCoarticulation(
    PassContext& ctx,
    std::vector<Token>& tokens,
    std::string& outError);

}  // namespace nvsp_frontend::passes

#endif  // NVSP_FRONTEND_PASSES_COARTICULATION_H

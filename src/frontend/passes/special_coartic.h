#ifndef TGSB_FRONTEND_PASSES_SPECIAL_COARTIC_H
#define TGSB_FRONTEND_PASSES_SPECIAL_COARTIC_H

#include "pass_common.h"

namespace nvsp_frontend::passes {

// Special coarticulation rules (language-specific Hz deltas).
//
// This pass applies configurable formant shifts to vowels adjacent to specific
// trigger consonants. Rules are defined in YAML and stored in
// LanguagePack::specialCoarticRules.
//
// Runs PostTiming, after the generic coarticulation pass and before
// boundary_smoothing.
bool runSpecialCoarticulation(
    PassContext& ctx,
    std::vector<Token>& tokens,
    std::string& outError);

}  // namespace nvsp_frontend::passes

#endif  // TGSB_FRONTEND_PASSES_SPECIAL_COARTIC_H

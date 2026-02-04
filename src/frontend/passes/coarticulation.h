#ifndef NVSP_FRONTEND_PASSES_COARTICULATION_H
#define NVSP_FRONTEND_PASSES_COARTICULATION_H

#include "pass_common.h"

namespace nvsp_frontend::passes {

// DECTalk-style coarticulation: shifts vowel START formants toward consonant
// locus, sets endCf to canonical target. DSP ramps smoothly within vowel.
bool runCoarticulation(
    PassContext& ctx,
    std::vector<Token>& tokens,
    std::string& outError);

}  // namespace nvsp_frontend::passes

#endif  // NVSP_FRONTEND_PASSES_COARTICULATION_H

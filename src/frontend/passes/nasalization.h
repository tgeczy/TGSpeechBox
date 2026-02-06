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

#ifndef TGSB_FRONTEND_PASSES_ALLOPHONES_H
#define TGSB_FRONTEND_PASSES_ALLOPHONES_H

#include "pass_common.h"

namespace nvsp_frontend::passes {

// Positional allophones (very conservative defaults).
bool runAllophones(
    PassContext& ctx,
    std::vector<Token>& tokens,
    std::string& outError);

}  // namespace nvsp_frontend::passes

#endif  // TGSB_FRONTEND_PASSES_ALLOPHONES_H

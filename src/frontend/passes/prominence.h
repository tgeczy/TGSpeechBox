#ifndef TGSB_FRONTEND_PASSES_PROMINENCE_H
#define TGSB_FRONTEND_PASSES_PROMINENCE_H

#include "pass_common.h"

namespace nvsp_frontend::passes {

bool runProminence(
    PassContext& ctx,
    std::vector<Token>& tokens,
    std::string& outError);

}  // namespace nvsp_frontend::passes

#endif  // TGSB_FRONTEND_PASSES_PROMINENCE_H

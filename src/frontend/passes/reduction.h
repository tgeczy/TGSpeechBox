#ifndef NVSP_FRONTEND_PASSES_REDUCTION_H
#define NVSP_FRONTEND_PASSES_REDUCTION_H

#include "pass_common.h"

namespace nvsp_frontend::passes {

// Rate-dependent reductions (schwa weakening, optional cluster simplification).
bool runReduction(
    PassContext& ctx,
    std::vector<Token>& tokens,
    std::string& outError);

}  // namespace nvsp_frontend::passes

#endif  // NVSP_FRONTEND_PASSES_REDUCTION_H

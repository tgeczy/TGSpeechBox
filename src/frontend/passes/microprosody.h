#ifndef NVSP_FRONTEND_PASSES_MICROPROSODY_H
#define NVSP_FRONTEND_PASSES_MICROPROSODY_H

#include "pass_common.h"

namespace nvsp_frontend::passes {

// Small pitch adjustments that improve “speechy” feel:
// e.g. voiceless consonants slightly raise the next vowel onset.
bool runMicroprosody(
    PassContext& ctx,
    std::vector<Token>& tokens,
    std::string& outError);

}  // namespace nvsp_frontend::passes

#endif  // NVSP_FRONTEND_PASSES_MICROPROSODY_H

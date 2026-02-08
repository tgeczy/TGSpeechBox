#ifndef TGSB_FRONTEND_PASSES_CLUSTER_TIMING_H
#define TGSB_FRONTEND_PASSES_CLUSTER_TIMING_H

#include "pass_common.h"

namespace nvsp_frontend::passes {

// Cluster timing — context-sensitive consonant duration adjustment.
//
// Shortens consonants in clusters and word-medial/word-final positions
// to produce more natural timing. Runs PostTiming, after length_contrast
// and before boundary_smoothing.
bool runClusterTiming(
    PassContext& ctx,
    std::vector<Token>& tokens,
    std::string& outError);

}  // namespace nvsp_frontend::passes

#endif  // TGSB_FRONTEND_PASSES_CLUSTER_TIMING_H

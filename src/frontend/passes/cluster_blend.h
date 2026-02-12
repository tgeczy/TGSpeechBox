#ifndef TGSB_FRONTEND_PASSES_CLUSTER_BLEND_H
#define TGSB_FRONTEND_PASSES_CLUSTER_BLEND_H

#include "pass_common.h"

namespace nvsp_frontend::passes {

// Cluster blend — C→C articulatory anticipation.
//
// Sets endCf targets on the first consonant of a cluster so its formants
// ramp toward the second consonant's place of articulation.  This creates
// gestural overlap: e.g. /n/ before /k/ starts moving toward velar territory
// before the velar burst arrives.
//
// Complements cluster_timing (duration) and boundary_smoothing (fade speed)
// by adding spectral anticipation to consonant clusters.
//
// Runs PostTiming, after cluster_timing and coarticulation.
bool runClusterBlend(
    PassContext& ctx,
    std::vector<Token>& tokens,
    std::string& outError);

}  // namespace nvsp_frontend::passes

#endif  // TGSB_FRONTEND_PASSES_CLUSTER_BLEND_H

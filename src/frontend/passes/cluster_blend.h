/*
TGSpeechBox — Cluster blend pass interface.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#ifndef TGSB_FRONTEND_PASSES_CLUSTER_BLEND_H
#define TGSB_FRONTEND_PASSES_CLUSTER_BLEND_H

#include "pass_common.h"

namespace nvsp_frontend::passes {

// Cluster blend — C→C articulatory anticipation.
//
// Tints C2's start formants toward C1 and sets endCf on C2 back to canonical,
// so the DSP ramps from the tinted start to the true target.  This creates
// gestural overlap: e.g. /k/ after /n/ begins with a velar-nasal spectral
// trace that fades into the canonical /k/ burst.
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

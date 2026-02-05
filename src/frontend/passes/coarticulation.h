#ifndef NVSP_FRONTEND_PASSES_COARTICULATION_H
#define NVSP_FRONTEND_PASSES_COARTICULATION_H

#include "pass_common.h"

namespace nvsp_frontend::passes {

// Coarticulation / locus transitions.
//
// This pass models consonant->vowel locus transitions by shifting the *vowel's*
// START formant targets toward a consonant-dependent locus target, while keeping
// the vowel's END formant targets at the canonical vowel targets. The DSP then
// ramps cf/pf from start -> end across the vowel frame.
//
// "Graduated" coarticulation (pack.lang.coarticulationGraduated) scales the
// strength down when the nearest triggering consonant is separated from the vowel
// by other consonants.
//
// Locus targets are computed using a MITalk-style interpolation:
//   locus = src + k * (trg - src)
// where src are the consonant formant targets, trg are the vowel targets, and
// k is ~0.42.
bool runCoarticulation(
    PassContext& ctx,
    std::vector<Token>& tokens,
    std::string& outError);

}  // namespace nvsp_frontend::passes

#endif  // NVSP_FRONTEND_PASSES_COARTICULATION_H

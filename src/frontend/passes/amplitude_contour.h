/*
TGSpeechBox — Amplitude contour pass interface.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/
#ifndef TGSB_FRONTEND_PASSES_AMPLITUDE_CONTOUR_H
#define TGSB_FRONTEND_PASSES_AMPLITUDE_CONTOUR_H

#include "pass_common.h"

namespace nvsp_frontend::passes {

// Gives every voiced sonorant segment its own loudness level and its own
// within-segment fall, so a sentence has the amplitude structure of speech
// rather than a row of flat holds.  Writes voiceAmplitude (level) and
// Token::endVoiceAmplitudeScale (fall); the DSP ramps per sample.
bool runAmplitudeContour(
    PassContext& ctx,
    std::vector<Token>& tokens,
    std::string& outError);

}  // namespace nvsp_frontend::passes

#endif  // TGSB_FRONTEND_PASSES_AMPLITUDE_CONTOUR_H

/*
TGSpeechBox — eSpeak-style pitch model pass.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

// =============================================================================
// eSpeak-style Pitch Pass — ToBI-based intonation contour
// =============================================================================
//
// Extracted from the espeak_style branch of calculatePitches() in
// ipa_engine.cpp.
//
// Architecture: the utterance is divided into regions (pre-head, head,
// nucleus, tail) delimited by stressed syllables.  Each region gets a
// linear pitch path interpolated from IntonationClause parameters.  The
// head section uses a stepped pitch pattern that cycles through
// headSteps, giving a characteristic eSpeak-like cadence.

#include "pitch_espeak.h"
#include "pitch_common.h"
#include "../ipa_engine.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace nvsp_frontend {

// -------------------------------------------------------------------------
// Clause parameter helpers (moved from ipa_engine.cpp)
// -------------------------------------------------------------------------

static IntonationClause defaultClause(char clause) {
  // Defaults from ipa_convert.py table.
  IntonationClause c;
  if (clause == '.') {
    c.preHeadStart = 46;
    c.preHeadEnd = 57;
    c.headExtendFrom = 4;
    c.headStart = 80;
    c.headEnd = 50;
    c.headSteps = {100,75,50,25,0,63,38,13,0};
    c.headStressEndDelta = -16;
    c.headUnstressedRunStartDelta = -8;
    c.headUnstressedRunEndDelta = -5;
    c.nucleus0Start = 64;
    c.nucleus0End = 8;
    c.nucleusStart = 70;
    c.nucleusEnd = 18;
    c.tailStart = 24;
    c.tailEnd = 8;
  } else if (clause == ',') {
    c.preHeadStart = 46;
    c.preHeadEnd = 57;
    c.headExtendFrom = 4;
    c.headStart = 80;
    c.headEnd = 60;
    c.headSteps = {100,75,50,25,0,63,38,13,0};
    c.headStressEndDelta = -16;
    c.headUnstressedRunStartDelta = -8;
    c.headUnstressedRunEndDelta = -5;
    c.nucleus0Start = 34;
    c.nucleus0End = 52;
    c.nucleusStart = 78;
    c.nucleusEnd = 34;
    c.tailStart = 34;
    c.tailEnd = 52;
  } else if (clause == '?') {
    c.preHeadStart = 45;
    c.preHeadEnd = 56;
    c.headExtendFrom = 3;
    c.headStart = 75;
    c.headEnd = 43;
    c.headSteps = {100,75,50,20,60,35,11,0};
    c.headStressEndDelta = -16;
    c.headUnstressedRunStartDelta = -7;
    c.headUnstressedRunEndDelta = 0;
    c.nucleus0Start = 34;
    c.nucleus0End = 68;
    c.nucleusStart = 86;
    c.nucleusEnd = 21;
    c.tailStart = 34;
    c.tailEnd = 68;
  } else if (clause == '!') {
    c.preHeadStart = 46;
    c.preHeadEnd = 57;
    c.headExtendFrom = 3;
    c.headStart = 90;
    c.headEnd = 50;
    c.headSteps = {100,75,50,16,82,50,32,16};
    c.headStressEndDelta = -16;
    c.headUnstressedRunStartDelta = -9;
    c.headUnstressedRunEndDelta = 0;
    c.nucleus0Start = 92;
    c.nucleus0End = 4;
    c.nucleusStart = 92;
    c.nucleusEnd = 80;
    c.tailStart = 76;
    c.tailEnd = 4;
  } else {
    return defaultClause('.');
  }
  return c;
}

static const IntonationClause& getClauseParams(const LanguagePack& lang, char clause, IntonationClause& storage) {
  auto it = lang.intonation.find(clause);
  if (it != lang.intonation.end()) return it->second;
  storage = defaultClause(clause);
  return storage;
}

// -------------------------------------------------------------------------
// Public entry point
// -------------------------------------------------------------------------

void applyPitchEspeak(
    std::vector<Token>& tokens,
    const PackSet& pack,
    double /*speed*/,
    double basePitch,
    double inflection,
    char clauseType) {

  if (tokens.empty()) return;

  IntonationClause tmp;
  const IntonationClause& params = getClauseParams(pack.lang, clauseType, tmp);

  int preHeadStart = 0;
  int preHeadEnd = static_cast<int>(tokens.size());

  // Find first stressed syllable.
  for (int i = 0; i < preHeadEnd; ++i) {
    if (tokens[i].syllableStart && tokens[i].stress == 1) {
      preHeadEnd = i;
      break;
    }
  }

  if (preHeadEnd - preHeadStart > 0) {
    applyPitchPath(tokens, preHeadStart, preHeadEnd, basePitch, inflection, params.preHeadStart, params.preHeadEnd);
  }

  int nucleusStart = static_cast<int>(tokens.size());
  int nucleusEnd = nucleusStart;
  int tailStart = nucleusStart;
  int tailEnd = nucleusStart;

  // Find last stressed syllable, scanning backwards.
  for (int i = nucleusEnd - 1; i >= preHeadEnd; --i) {
    if (tokens[i].syllableStart) {
      if (tokens[i].stress == 1) {
        nucleusStart = i;
        break;
      } else {
        nucleusEnd = i;
        tailStart = i;
      }
    }
  }

  const bool hasTail = (tailEnd - tailStart) > 0;
  if (hasTail) {
    applyPitchPath(tokens, tailStart, tailEnd, basePitch, inflection, params.tailStart, params.tailEnd);
  }

  if (nucleusEnd - nucleusStart > 0) {
    if (hasTail) {
      applyPitchPath(tokens, nucleusStart, nucleusEnd, basePitch, inflection, params.nucleusStart, params.nucleusEnd);
    } else {
      applyPitchPath(tokens, nucleusStart, nucleusEnd, basePitch, inflection, params.nucleus0Start, params.nucleus0End);
    }
  }

  // Head section (between preHeadEnd and nucleusStart).
  if (preHeadEnd < nucleusStart) {
    int headStartPitch = params.headStart;
    int headEndPitch = params.headEnd;

    int lastHeadStressStart = -1;
    int lastHeadUnstressedRunStart = -1;
    int stressEndPitch = headEndPitch;

    const std::vector<int>& steps = params.headSteps.empty() ? std::vector<int>{100,75,50,25,0} : params.headSteps;
    const int extendFrom = std::min(std::max(params.headExtendFrom, 0), static_cast<int>(steps.size()));

    int stepIndex = 0;
    auto nextStep = [&]() -> int {
      if (stepIndex < static_cast<int>(steps.size())) {
        return steps[stepIndex++];
      }
      // cycle
      const int cycleLen = static_cast<int>(steps.size()) - extendFrom;
      if (cycleLen <= 0) return steps.back();
      const int idx = extendFrom + ((stepIndex++ - static_cast<int>(steps.size())) % cycleLen);
      return steps[idx];
    };

    for (int i = preHeadEnd; i <= nucleusStart; ++i) {
      Token& t = tokens[i];
      const bool syllableStress = (t.stress == 1);
      if (t.syllableStart) {
        if (lastHeadStressStart >= 0) {
          const int stepPct = nextStep();
          const int stressStartPitch = headEndPitch + static_cast<int>(((headStartPitch - headEndPitch) / 100.0) * stepPct);
          stressEndPitch = stressStartPitch + params.headStressEndDelta;
          applyPitchPath(tokens, lastHeadStressStart, i, basePitch, inflection, stressStartPitch, stressEndPitch);
          lastHeadStressStart = -1;
        }

        if (syllableStress) {
          if (lastHeadUnstressedRunStart >= 0) {
            const int runStartPitch = stressEndPitch + params.headUnstressedRunStartDelta;
            const int runEndPitch = stressEndPitch + params.headUnstressedRunEndDelta;
            applyPitchPath(tokens, lastHeadUnstressedRunStart, i, basePitch, inflection, runStartPitch, runEndPitch);
            lastHeadUnstressedRunStart = -1;
          }
          lastHeadStressStart = i;
        } else if (lastHeadUnstressedRunStart < 0) {
          lastHeadUnstressedRunStart = i;
        }
      }
    }
  }
}

} // namespace nvsp_frontend

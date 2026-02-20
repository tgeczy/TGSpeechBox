/*
TGSpeechBox — Diphthong collapse pass.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#include "diphthong_collapse.h"

#include <algorithm>

namespace nvsp_frontend::passes {

namespace {

static inline bool tokIsVowel(const Token& t) {
  return t.def && ((t.def->flags & kIsVowel) != 0);
}

} // namespace

bool runDiphthongCollapse(
  PassContext& ctx,
  std::vector<Token>& tokens,
  std::string& outError
) {
  (void)outError;

  const auto& lp = ctx.pack.lang;
  if (!lp.diphthongCollapseEnabled) return true;

  const int cf1 = static_cast<int>(FieldId::cf1);
  const int cf2 = static_cast<int>(FieldId::cf2);
  const int cf3 = static_cast<int>(FieldId::cf3);
  const int pf1 = static_cast<int>(FieldId::pf1);
  const int pf2 = static_cast<int>(FieldId::pf2);
  const int pf3 = static_cast<int>(FieldId::pf3);
  const int vp  = static_cast<int>(FieldId::voicePitch);
  const int evp = static_cast<int>(FieldId::endVoicePitch);

  // Scan for tied vowel pairs: A.tiedTo && B.tiedFrom && both kIsVowel.
  // Iterate by index (not iterator) because we erase token B in place.
  for (size_t i = 0; i + 1 < tokens.size(); /* advanced inside */) {
    Token& a = tokens[i];
    Token& b = tokens[i + 1];

    if (!a.tiedTo || !b.tiedFrom || !tokIsVowel(a) || !tokIsVowel(b)) {
      ++i;
      continue;
    }

    // === Merge B into A ===

    // Duration: combined, with floor to ensure enough micro-frames for the glide.
    a.durationMs += b.durationMs;
    if (a.durationMs < lp.diphthongDurationFloorMs)
      a.durationMs = lp.diphthongDurationFloorMs;

    // Start formants: already in A's field[] (cf1/2/3, pf1/2/3).
    // End formants: take from B's field[] (what B's steady-state would be).
    // Use token-level field values when set, fall back to PhonemeDef.
    auto getField = [](const Token& t, int fid) -> double {
      if ((t.setMask & (1ull << fid)) != 0) return t.field[fid];
      if (t.def) {
        return t.def->field[fid];
      }
      return 0.0;
    };

    a.hasEndCf1 = true;  a.endCf1 = getField(b, cf1);
    a.hasEndCf2 = true;  a.endCf2 = getField(b, cf2);
    a.hasEndCf3 = true;  a.endCf3 = getField(b, cf3);

    // Parallel end targets: use B's parallel formants.
    // These will fall back to endCf in frame_emit if not explicitly set
    // on Token, but setting them here future-proofs for nasal diphthongs.
    a.hasEndPf1 = true;  a.endPf1 = getField(b, pf1);
    a.hasEndPf2 = true;  a.endPf2 = getField(b, pf2);
    a.hasEndPf3 = true;  a.endPf3 = getField(b, pf3);

    // Pitch: onset from A, offset from B.
    // A's voicePitch stays as-is.  Set endVoicePitch to B's pitch.
    double bPitch = getField(b, vp);
    if (bPitch > 0.0) {
      a.field[evp] = bPitch;
      a.setMask |= (1ull << evp);
    }

    // Flag it
    a.isDiphthongGlide = true;

    // Inherit A's syllableIndex, stress, wordStart, syllableStart (already there).
    // fadeMs from A (entry fade into the diphthong).
    // Clear tied flags — this is now a single merged token.
    a.tiedTo = false;
    a.tiedFrom = false;

    // Erase token B
    tokens.erase(tokens.begin() + static_cast<ptrdiff_t>(i + 1));

    // Do NOT double-merge triphthongs.
    // After collapsing [A,B] -> [AB], advance past the merged token.
    // If there was a triphthong [A,B,C] with A.tiedTo, B.tiedTo+tiedFrom,
    // C.tiedFrom, the first merge creates [AB,C].  AB has tiedTo=false,
    // so the next iteration won't merge AB+C.  Correct.
    ++i;
  }

  return true;
}

} // namespace nvsp_frontend::passes

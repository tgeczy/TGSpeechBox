/*
TGSpeechBox — Microprosody pass (F0 perturbations + pre-voiceless shortening).
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.

Six independently-gated effects:
  Phase 1: Onset F0 — backward-looking (voiceless raise / voiced lower)
  Phase 2: Endpoint F0 — forward-looking (voiceless raise / voiced lower)
  Phase 3: Intrinsic vowel F0 (high vowels higher, low vowels lower)
  Phase 4: Pre-voiceless shortening — vowel duration shrinks before voiceless C
  Phase 5: Voiceless coda lengthening — voiceless C grows after voiced segment
*/

#include "microprosody.h"

#include <algorithm>

namespace nvsp_frontend::passes {

namespace {

static inline bool isVowel(const Token& t) {
  return t.def && ((t.def->flags & kIsVowel) != 0);
}

static inline bool isVoicelessConsonant(const Token& t) {
  if (!t.def) return false;
  if ((t.def->flags & kIsVowel) != 0) return false;
  return (t.def->flags & kIsVoiced) == 0;
}

static inline bool isVoicedStop(const Token& t) {
  if (!t.def) return false;
  if ((t.def->flags & kIsStop) == 0) return false;
  return (t.def->flags & kIsVoiced) != 0;
}

// Voiced obstruents: stops, affricates, and voiced fricatives.
// Excludes sonorants (nasals, liquids, semivowels).
static inline bool isVoicedObstruent(const Token& t) {
  if (!t.def) return false;
  if ((t.def->flags & kIsVowel) != 0) return false;
  if ((t.def->flags & kIsVoiced) == 0) return false;
  const uint32_t f = t.def->flags;
  if ((f & kIsStop) != 0) return true;
  if ((f & kIsAfricate) != 0) return true;
  // Voiced fricatives: check frication amplitude on def or token.
  int idx = static_cast<int>(FieldId::fricationAmplitude);
  uint64_t bit = 1ull << idx;
  double val = 0.0;
  if (t.setMask & bit) val = t.field[idx];
  else if (t.def->setMask & bit) val = t.def->field[idx];
  return val > 0.05;
}

static inline bool isSilenceOrMissing(const Token& t) {
  return t.silence || !t.def;
}

static inline bool hasField(const Token& t, FieldId id) {
  return (t.setMask & (1ULL << static_cast<int>(id))) != 0;
}

}  // namespace

bool runMicroprosody(PassContext& ctx, std::vector<Token>& tokens, std::string& outError) {
  (void)outError;

  const auto& lang = ctx.pack.lang;
  if (!lang.microprosodyEnabled) return true;

  const int vpIdx = static_cast<int>(FieldId::voicePitch);
  const int epIdx = static_cast<int>(FieldId::endVoicePitch);
  const int f1Idx = static_cast<int>(FieldId::cf1);
  const uint64_t f1Bit = 1ULL << f1Idx;

  for (size_t i = 0; i < tokens.size(); ++i) {
    Token& v = tokens[i];
    if (!isVowel(v) || isSilenceOrMissing(v)) continue;

    if (!hasField(v, FieldId::voicePitch) || !hasField(v, FieldId::endVoicePitch))
      continue;

    // Skip very short vowels — no room for microprosody.
    if (v.durationMs < lang.microprosodyMinVowelMs) continue;

    double startP = v.field[vpIdx];
    double endP = v.field[epIdx];
    const double origStartP = startP;
    const double origEndP = endP;

    // Find prev and next non-silence tokens.
    const Token* prev = nullptr;
    for (size_t j = i; j > 0; --j) {
      if (!isSilenceOrMissing(tokens[j - 1])) { prev = &tokens[j - 1]; break; }
    }
    const Token* next = nullptr;
    for (size_t j = i + 1; j < tokens.size(); ++j) {
      if (!isSilenceOrMissing(tokens[j])) { next = &tokens[j]; break; }
    }

    // ── Phase 1: Onset F0 (backward-looking) ──
    if (prev) {
      if (lang.microprosodyVoicelessF0RaiseEnabled && isVoicelessConsonant(*prev)) {
        startP += lang.microprosodyVoicelessF0RaiseHz;
      } else if (lang.microprosodyVoicedF0LowerEnabled && isVoicedObstruent(*prev)) {
        double d = lang.microprosodyVoicedF0LowerHz;
        if (!isVoicedStop(*prev)) d *= lang.microprosodyVoicedFricativeLowerScale;
        startP -= d;
      }
    }

    // ── Phase 2: Endpoint F0 (forward-looking) ──
    if (next && lang.microprosodyFollowingF0Enabled) {
      if (isVoicelessConsonant(*next)) {
        endP += lang.microprosodyFollowingVoicelessRaiseHz;
      } else if (isVoicedObstruent(*next)) {
        endP -= lang.microprosodyFollowingVoicedLowerHz;
      }
    }

    // ── Phase 3: Intrinsic vowel F0 ──
    if (lang.microprosodyIntrinsicF0Enabled) {
      double f1 = 0.0;
      if (v.setMask & f1Bit) f1 = v.field[f1Idx];
      else if (v.def && (v.def->setMask & f1Bit)) f1 = v.def->field[f1Idx];

      if (f1 > 0.0) {
        double delta = 0.0;
        if (f1 < lang.microprosodyIntrinsicF0HighThreshold) {
          delta = lang.microprosodyIntrinsicF0HighRaiseHz;
        } else if (f1 > lang.microprosodyIntrinsicF0LowThreshold) {
          delta = -lang.microprosodyIntrinsicF0LowDropHz;
        }
        if (delta != 0.0) {
          startP += delta;
          endP += delta;
        }
      }
    }

    // ── Clamp total perturbation ──
    if (lang.microprosodyMaxTotalDeltaHz > 0.0) {
      const double cap = lang.microprosodyMaxTotalDeltaHz;
      const double dStart = startP - origStartP;
      const double dEnd = endP - origEndP;
      if (dStart >  cap) startP = origStartP + cap;
      if (dStart < -cap) startP = origStartP - cap;
      if (dEnd   >  cap) endP   = origEndP   + cap;
      if (dEnd   < -cap) endP   = origEndP   - cap;
    }

    // Write back (20 Hz floor to prevent zero/negative pitch).
    v.field[vpIdx] = std::max(20.0, startP);
    v.field[epIdx] = std::max(20.0, endP);

    // ── Phase 4: Pre-voiceless shortening (duration, not pitch) ──
    // Skip diphthong glides: the merged token carries the entire formant
    // trajectory and needs its full duration.  Shortening squishes the glide
    // into too few micro-frames, making it inaudible before voiceless stops.
    if (next && lang.microprosodyPreVoicelessShortenEnabled && !v.isDiphthongGlide) {
      if (isVoicelessConsonant(*next)) {
        v.durationMs *= lang.microprosodyPreVoicelessShortenScale;
        v.durationMs = std::max(v.durationMs, lang.microprosodyPreVoicelessMinMs);
      }
    }
  }

  // ── Phase 5: Voiceless coda lengthening (duration, not pitch) ──
  // Complement to Phase 4: when vowels shorten before voiceless consonants,
  // the voiceless consonants grow to keep syllable weight constant.
  // Cho & Ladefoged (1999): voiceless codas lengthen after voiced segments.
  if (lang.microprosodyVoicelessCodaLengthenEnabled) {
    for (size_t i = 1; i < tokens.size(); ++i) {
      Token& t = tokens[i];
      if (isSilenceOrMissing(t)) continue;
      if (!isVoicelessConsonant(t)) continue;

      // Look back: was the previous non-silence token voiced?
      const Token* prev = nullptr;
      for (int j = static_cast<int>(i) - 1; j >= 0; --j) {
        if (!tokens[static_cast<size_t>(j)].silence && tokens[static_cast<size_t>(j)].def) {
          prev = &tokens[static_cast<size_t>(j)];
          break;
        }
      }
      if (!prev) continue;

      // Previous must be voiced (vowel, voiced consonant, nasal, liquid).
      if ((prev->def->flags & kIsVoiced) || (prev->def->flags & kIsVowel)) {
        t.durationMs *= lang.microprosodyVoicelessCodaLengthenScale;
      }
    }
  }

  return true;
}

}  // namespace nvsp_frontend::passes

/*
TGSpeechBox — Legacy pitch mode pass.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

// =============================================================================
// Legacy Pitch Pass — classic screen-reader pitch contour
// =============================================================================
//
// Extracted from calculatePitchesLegacy() in ipa_engine.cpp.
//
// This is intentionally time-based (uses accumulated voiced duration) rather
// than table-based, and tends to produce a more predictable "classic" screen
// reader prosody at higher rates.
//
// Declination is gentle and linear (1/(1+k*t)), with a distinct final-word
// inflection region shaped by clause type.  Stress accents are applied on
// vowel nuclei with a decaying stressInflection multiplier.

#include "pitch_legacy.h"
#include "pitch_common.h"
#include "../ipa_engine.h"

#include <algorithm>
#include <cmath>

namespace nvsp_frontend {

void applyPitchLegacy(
    std::vector<Token>& tokens,
    const PackSet& pack,
    double speed,
    double basePitch,
    double inflection,
    char clauseType) {

  // Port of ipa-older.py calculatePhonemePitches().
  //
  // This is intentionally time-based (uses accumulated voiced duration) rather than
  // table-based, and tends to produce a more predictable "classic" screen reader
  // prosody at higher rates.

  if (speed <= 0.0) speed = 1.0;

  // The legacy pitch math was historically paired with a lower default inflection
  // setting (e.g. 35) than many modern configs (often 60).
  // To keep legacyPitchMode usable without forcing users to retune sliders,
  // we apply an optional scale here.
  double inflScale = pack.lang.legacyPitchInflectionScale;
  // 0 is flat, as in every other mode (#177); it used to be read as 1, so
  // the slider's lowest position gave more movement than its middle.
  if (!(inflScale >= 0.0)) inflScale = 1.0;  // negative or not a number: as before
  // Keep this bounded to avoid pathological values from bad configs.
  if (inflScale > 2.0) inflScale = 2.0;
  const double infl = inflection * inflScale;

  double totalVoicedDuration = 0.0;
  double finalInflectionStartTime = 0.0;
  bool needsSetFinalInflectionStartTime = false;
  int finalVoicedIndex = -1;

  const Token* last = nullptr;
  for (size_t i = 0; i < tokens.size(); ++i) {
    const Token& t = tokens[i];

    if (t.wordStart) {
      needsSetFinalInflectionStartTime = true;
    }

    if (pitchTokenIsVoiced(t)) {
      finalVoicedIndex = static_cast<int>(i);
      if (needsSetFinalInflectionStartTime) {
        finalInflectionStartTime = totalVoicedDuration;
        needsSetFinalInflectionStartTime = false;
      }
      totalVoicedDuration += t.durationMs;
    } else if (last && pitchTokenIsVoiced(*last)) {
      // When we leave a voiced segment, count the fade time as part of the voiced run.
      totalVoicedDuration += last->fadeMs;
    }

    last = &t;
  }

  if (totalVoicedDuration <= 0.0) {
    // No voiced frames: set a constant pitch so downstream code has sane values.
    for (Token& t : tokens) {
      setPitchFields(t, basePitch, basePitch);
    }
    return;
  }

  double durationCounter = 0.0;
  double curBasePitch = basePitch;
  double lastEndVoicePitch = basePitch;
  // An exclamation had no shape of its own (#155).  It is read as a
  // statement with stronger accents, as the default mode reads one: both
  // fall to the same low (statements and exclamations end at about the same
  // pitch in O'Shaughnessy 1976, PhD thesis, MIT).
  const double accentScale =
      (clauseType == '!') ? pack.lang.legacyExclamationAccentScale : 1.0;
  // A statement and an exclamation end going down, a question going up.  A
  // comma is left exactly as it was.
  const bool fallingClause = (clauseType == '.' || clauseType == '!');
  const bool risingClause = (clauseType == '?');
  double stressInflection = infl / 1.5 * accentScale;

  Token* lastToken = nullptr;
  bool syllableStress = false;
  bool firstStress = true;

  for (size_t i = 0; i < tokens.size(); ++i) {
    Token& t = tokens[i];

    if (t.syllableStart) {
      syllableStress = (t.stress == 1);
    }

    double voicePitch = lastEndVoicePitch;
    const bool inFinalInflection = (durationCounter >= finalInflectionStartTime);

    // Advance the duration counter.
    if (pitchTokenIsVoiced(t)) {
      durationCounter += t.durationMs;
    } else if (lastToken && pitchTokenIsVoiced(*lastToken)) {
      durationCounter += lastToken->fadeMs;
    }

    const double oldBasePitch = curBasePitch;

    if (infl == 0.0) {
      curBasePitch = basePitch;
    } else if (!inFinalInflection) {
      // Gentle declination across the clause.
      curBasePitch = basePitch / (1.0 + (infl / 25000.0) * durationCounter * speed);
    } else {
      // Final inflection is shaped only over the last word.
      const double denom = (totalVoicedDuration - finalInflectionStartTime);
      double ratio = 0.0;
      if (denom > 0.0) {
        ratio = (durationCounter - finalInflectionStartTime) / denom;
      }

      if (clauseType == '.') {
        ratio /= 1.5;
      } else if (clauseType == '?') {
        ratio = 0.5 - (ratio / 1.2);
      } else if (clauseType == ',') {
        ratio /= 8.0;
      } else {
        ratio = ratio / 1.75;
      }

      // A question's ratio ends at 0.5 - 1/1.2 = -1/3, so the divisor is
      // 1 - infl/2: zero at the top of the inflection scale with full
      // inflection (infl = 2), where the pitch was infinite, and thousands
      // of Hz just before it.  The divisor stops at the ceiling.
      curBasePitch = basePitch /
          std::max(1.0 / kPitchCeilRatio, 1.0 + (infl * ratio * 1.5));
    }

    double endVoicePitch = curBasePitch;

    // Add a pitch accent on the vowel in the stressed syllable.
    if (syllableStress && tokenIsVowel(t)) {
      // When the clause ends on this vowel, the accent sits where the clause
      // is going: at the vowel's start in a statement or an exclamation, so
      // the vowel falls from it, and at its end in a question.  A clause
      // whose only stress was its last vowel used to end on the accent peak,
      // so a one-word statement did not fall; and a question with earlier
      // stresses used to start on the peak and go from 130 to 128 Hz (#155).
      // Any other clause keeps the old rule: at the end when this is the
      // clause's first stress, at the start otherwise.
      const bool endsClause = (static_cast<int>(i) >= finalVoicedIndex);
      if (endsClause && (fallingClause || (!risingClause && !firstStress))) {
        voicePitch = basePitch * (1.0 + stressInflection);
      } else if (firstStress || endsClause) {
        voicePitch = oldBasePitch * (1.0 + stressInflection / 3.0);
        endVoicePitch = curBasePitch * (1.0 + stressInflection);
      } else {
        voicePitch = oldBasePitch * (1.0 + stressInflection / 3.0);
        endVoicePitch = oldBasePitch * (1.0 + stressInflection);
      }
      firstStress = false;

      stressInflection *= 0.9;
      stressInflection = std::max(stressInflection, infl / 2.0 * accentScale);
      syllableStress = false;
    }

    // Match the legacy behavior: ensure pitch continuity by snapping the previous
    // token's end pitch to this token's start pitch (useful when accents start).
    if (lastToken) {
      const int evp = static_cast<int>(FieldId::endVoicePitch);
      lastToken->field[evp] = voicePitch;
      lastToken->setMask |= (1ull << evp);
    }

    setPitchFields(t, voicePitch, endVoicePitch);
    // The accent that a clause-final vowel now falls from, or rises to, is
    // steep; on a word-initial vowel the word-boundary dip frame would run
    // all of it in 8 ms and the vowel would start again from the top.
    t.pitchRampAcrossWordDip = (fallingClause || risingClause);
    lastEndVoicePitch = endVoicePitch;
    lastToken = &t;
  }

  // With a stress accent on top of the question's rise the clause can still
  // go past the ceiling, and a long statement at full inflection under the
  // floor.
  boundClausePitch(tokens, basePitch);
}

} // namespace nvsp_frontend

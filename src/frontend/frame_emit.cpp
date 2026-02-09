// frame_emit.cpp - TGSpeechBox — Frame emission for frontend
//
// Extracted from ipa_engine.cpp to reduce file size.
// Contains emitFrames() and emitFramesEx() implementations.

#include "ipa_engine.h"

#include <cstring>
#include <cmath>
#include <algorithm>
#include <type_traits>

namespace nvsp_frontend {

// Helper to clamp a value to [0, 1]
static inline double clamp01(double v) {
  if (v < 0.0) return 0.0;
  if (v > 1.0) return 1.0;
  return v;
}

// Helper to clamp sharpness multiplier to reasonable range
static inline double clampSharpness(double v) {
  if (v < 0.1) return 0.1;
  if (v > 5.0) return 5.0;
  return v;
}

void emitFrames(
  const PackSet& pack,
  const std::vector<Token>& tokens,
  int userIndexBase,
  TrajectoryState* trajectoryState,
  nvspFrontend_FrameCallback cb,
  void* userData
) {
  if (!cb) return;

  // We intentionally treat nvspFrontend_Frame as a dense sequence of doubles.
  // Enforce that assumption at compile time so future edits fail loudly.
  static_assert(sizeof(nvspFrontend_Frame) == sizeof(double) * kFrameFieldCount,
                "nvspFrontend_Frame must remain exactly kFrameFieldCount doubles with no padding");
  static_assert(std::is_standard_layout<nvspFrontend_Frame>::value,
                "nvspFrontend_Frame must remain standard-layout");
  static_assert(std::is_trivially_copyable<nvspFrontend_Frame>::value,
                "nvspFrontend_Frame must remain trivially copyable");

  const bool trillEnabled = (pack.lang.trillModulationMs > 0.0);

  const int vp = static_cast<int>(FieldId::voicePitch);
  const int evp = static_cast<int>(FieldId::endVoicePitch);
  const int va = static_cast<int>(FieldId::voiceAmplitude);
  const int fa = static_cast<int>(FieldId::fricationAmplitude);

  // Trill modulation constants.
  //
  // We implement the trill as an amplitude modulation on voiceAmplitude using a
  // sequence of short frames (micro-frames). This keeps the speechPlayer.dll ABI
  // stable (no extra fields) while avoiding pack-level hacks such as duplicating
  // phoneme tokens.
  //
  // These constants were chosen to produce an audible trill without introducing
  // clicks or an overly "tremolo" sound. Packs can tune the trill duration and
  // micro-frame fade via settings, but not the depth (kept fixed for simplicity).
  constexpr double kTrillCloseFactor = 0.22;   // voiceAmplitude multiplier during closure
  constexpr double kTrillCloseFrac = 0.28;     // fraction of cycle spent in closure
  constexpr double kTrillFricFloor = 0.12;     // minimum fricationAmplitude during closure (if frication is present)
  // Minimum phase duration for the trill micro-frames. Keep this small so
  // very fast modulation settings (e.g. 2ms cycles) still behave as expected.
  constexpr double kMinPhaseMs = 0.25;


  // ============================================
  // TRAJECTORY LIMITING STATE (per-handle)
  // ============================================
  // Reset state at start of each utterance
  const LanguagePack& lang = pack.lang;
  trajectoryState->hasPrevFrame = false;

  // Track previous frame values for voiced closure continuation
  bool hadPrevFrame = false;

  for (const Token& t : tokens) {
    if (t.silence || !t.def) {
      // For voiced closure (voice bar): use a NULL frame with a generous fade.
      // frame.cpp's NULL handling copies the entire previous frame and just zeros
      // preFormantGain, keeping all resonator coefficients (frequencies AND bandwidths)
      // stable.  This avoids IIR transients from bandwidth discontinuities that
      // caused clicks at 16 kHz and occasional clicks at 22050 Hz.
      if (t.voicedClosure && hadPrevFrame) {
        double vbFadeMs = t.fadeMs;
        if (vbFadeMs < 8.0) vbFadeMs = 8.0;
        cb(userData, nullptr, t.durationMs, vbFadeMs, userIndexBase);
        continue;
      }
      cb(userData, nullptr, t.durationMs, t.fadeMs, userIndexBase);
      continue;
    }

    // Build a dense array of doubles and memcpy into the frame.
    // This avoids UB from treating a struct as an array via pointer arithmetic.
    double base[kFrameFieldCount] = {};
    const std::uint64_t mask = t.setMask;
    for (int f = 0; f < kFrameFieldCount; ++f) {
      if ((mask & (1ull << f)) == 0) continue;
      base[f] = t.field[f];
    }

    // Optional trill modulation (only when `_isTrill` is true for the phoneme).
    if (trillEnabled && tokenIsTrill(t) && t.durationMs > 0.0) {
      double totalDur = t.durationMs;

      // Trill flutter speed is hardcoded to a natural-sounding ~35Hz.
      // The pack setting (trillModulationMs) controls the *total duration* via calculateTimes().
      constexpr double kFixedTrillCycleMs = 28.0;

      double cycleMs = kFixedTrillCycleMs;

      // For short trills, compress the cycle so we still get at least one closure dip.
      if (cycleMs > totalDur) cycleMs = totalDur;

      // Split the cycle into an "open" and "closure" phase.
      double closeMs = cycleMs * kTrillCloseFrac;
      double openMs = cycleMs - closeMs;

      // Keep both phases non-trivial (prevents zero-length frames).
      if (openMs < kMinPhaseMs) {
        openMs = kMinPhaseMs;
        closeMs = std::max(kMinPhaseMs, cycleMs - openMs);
      }
      if (closeMs < kMinPhaseMs) {
        closeMs = kMinPhaseMs;
        openMs = std::max(kMinPhaseMs, cycleMs - closeMs);
      }

      // Fade between micro-frames. If not configured, choose a small default
      // relative to the cycle.
      double microFadeMs = pack.lang.trillModulationFadeMs;
      if (microFadeMs <= 0.0) {
        microFadeMs = std::min(2.0, cycleMs * 0.12);
      }

      const bool hasVoiceAmp = ((mask & (1ull << va)) != 0);
      const bool hasFricAmp = ((mask & (1ull << fa)) != 0);
      const double baseVoiceAmp = base[va];
      const double baseFricAmp = base[fa];

      const double startPitch = base[vp];
      const double endPitch = base[evp];
      const double pitchDelta = endPitch - startPitch;

      double remaining = totalDur;
      double pos = 0.0;
      bool highPhase = true;
      bool firstPhase = true;

      while (remaining > 1e-9) {
        double phaseDur = highPhase ? openMs : closeMs;
        if (phaseDur > remaining) phaseDur = remaining;

        // Interpolate pitch over the original token's duration so pitch remains continuous.
        double t0 = (totalDur > 0.0) ? (pos / totalDur) : 0.0;
        double t1 = (totalDur > 0.0) ? ((pos + phaseDur) / totalDur) : 1.0;

        double seg[kFrameFieldCount];
        std::memcpy(seg, base, sizeof(seg));

        seg[vp] = startPitch + pitchDelta * t0;
        seg[evp] = startPitch + pitchDelta * t1;

        if (!highPhase) {
          if (hasVoiceAmp) {
            seg[va] = baseVoiceAmp * kTrillCloseFactor;
          }
          // Add a small noise burst on closure to make the trill more perceptible,
          // but only if the phoneme already has a frication path.
          if (hasFricAmp && baseFricAmp > 0.0) {
            seg[fa] = std::max(baseFricAmp, kTrillFricFloor);
          }
        }

        nvspFrontend_Frame frame;
        std::memcpy(&frame, seg, sizeof(frame));

        // In speechPlayer.dll, the fade duration belongs to the *incoming* frame
        // (it's the crossfade from the previous frame to this one). Preserve the
        // token's original fade on entry to the trill, then use microFadeMs for
        // the internal micro-frame boundaries.
        double fadeIn = firstPhase ? t.fadeMs : microFadeMs;
        if (fadeIn <= 0.0) fadeIn = microFadeMs;

        // Prevent fade dominating very short micro-frames.
        if (fadeIn > phaseDur) fadeIn = phaseDur;

        cb(userData, &frame, phaseDur, fadeIn, userIndexBase);
        hadPrevFrame = true;

        remaining -= phaseDur;
        pos += phaseDur;
        highPhase = !highPhase;
        firstPhase = false;

        // If the remaining duration is too small to fit another phase, let the loop
        // handle it naturally by truncating phaseDur above.
      }

      continue;
    }

    nvspFrontend_Frame frame;
    std::memcpy(&frame, base, sizeof(frame));


    // ============================================
    // TRAJECTORY LIMITING
    // ============================================
    // Limit how fast formant frequencies can change to reduce harsh transitions.
    // Skip semivowels, liquids, and nasals - they need sharp formant transitions.
    // Also skip when the PREVIOUS frame was a nasal: nasal place perception
    // depends on F2 transitions in adjacent vowels, so clamping the vowel
    // after a nasal destroys the place cue (e.g. "nyolc" → "nyölc").
    const bool isNasal = t.def && ((t.def->flags & kIsNasal) != 0);
    const bool skipTrajectoryLimit = (t.def && (
        (t.def->flags & kIsSemivowel) != 0 ||
        (t.def->flags & kIsLiquid) != 0 ||
        (t.def->flags & kIsNasal) != 0
    )) || trajectoryState->prevWasNasal;
    if (lang.trajectoryLimitEnabled && trajectoryState->hasPrevFrame && t.durationMs > 0.0 && !skipTrajectoryLimit) {
      const size_t idx_cf2 = static_cast<size_t>(FieldId::cf2);
      const size_t idx_cf3 = static_cast<size_t>(FieldId::cf3);
      const size_t idx_pf2 = static_cast<size_t>(FieldId::pf2);
      const size_t idx_pf3 = static_cast<size_t>(FieldId::pf3);
      double maxDelta, delta;

      // Use a duration floor so high speech rates don't starve formant transitions.
      // At speed 1.0, tokens are ~60ms so 40ms never activates. At high speed,
      // tokens shrink to ~15ms, preventing formants from reaching their targets.
      const double effectiveDur = std::max(t.durationMs, 40.0);
      
      // cf2 limiting
      if ((lang.trajectoryLimitApplyMask & (1ULL << idx_cf2)) != 0) {
        if (lang.trajectoryLimitMaxHzPerMs[idx_cf2] > 0.0) {
          maxDelta = lang.trajectoryLimitMaxHzPerMs[idx_cf2] * effectiveDur;
          delta = frame.cf2 - trajectoryState->prevCf2;
          if (delta > maxDelta) frame.cf2 = trajectoryState->prevCf2 + maxDelta;
          else if (delta < -maxDelta) frame.cf2 = trajectoryState->prevCf2 - maxDelta;
        }
      }
      
      // cf3 limiting
      if ((lang.trajectoryLimitApplyMask & (1ULL << idx_cf3)) != 0) {
        if (lang.trajectoryLimitMaxHzPerMs[idx_cf3] > 0.0) {
          maxDelta = lang.trajectoryLimitMaxHzPerMs[idx_cf3] * effectiveDur;
          delta = frame.cf3 - trajectoryState->prevCf3;
          if (delta > maxDelta) frame.cf3 = trajectoryState->prevCf3 + maxDelta;
          else if (delta < -maxDelta) frame.cf3 = trajectoryState->prevCf3 - maxDelta;
        }
      }
      
      // pf2 limiting
      if ((lang.trajectoryLimitApplyMask & (1ULL << idx_pf2)) != 0) {
        if (lang.trajectoryLimitMaxHzPerMs[idx_pf2] > 0.0) {
          maxDelta = lang.trajectoryLimitMaxHzPerMs[idx_pf2] * effectiveDur;
          delta = frame.pf2 - trajectoryState->prevPf2;
          if (delta > maxDelta) frame.pf2 = trajectoryState->prevPf2 + maxDelta;
          else if (delta < -maxDelta) frame.pf2 = trajectoryState->prevPf2 - maxDelta;
        }
      }
      
      // pf3 limiting
      if ((lang.trajectoryLimitApplyMask & (1ULL << idx_pf3)) != 0) {
        if (lang.trajectoryLimitMaxHzPerMs[idx_pf3] > 0.0) {
          maxDelta = lang.trajectoryLimitMaxHzPerMs[idx_pf3] * effectiveDur;
          delta = frame.pf3 - trajectoryState->prevPf3;
          if (delta > maxDelta) frame.pf3 = trajectoryState->prevPf3 + maxDelta;
          else if (delta < -maxDelta) frame.pf3 = trajectoryState->prevPf3 - maxDelta;
        }
      }
    }
    
    // Update previous frame values for next iteration
    trajectoryState->prevCf2 = frame.cf2;
    trajectoryState->prevCf3 = frame.cf3;
    trajectoryState->prevPf2 = frame.pf2;
    trajectoryState->prevPf3 = frame.pf3;
    trajectoryState->prevVoiceAmp = base[va];
    trajectoryState->prevFricAmp = base[fa];
    trajectoryState->hasPrevFrame = true;
    trajectoryState->prevWasNasal = isNasal;

    cb(userData, &frame, t.durationMs, t.fadeMs, userIndexBase);
    hadPrevFrame = true;
  }
}

void emitFramesEx(
  const PackSet& pack,
  const std::vector<Token>& tokens,
  int userIndexBase,
  const nvspFrontend_FrameEx& frameExDefaults,
  TrajectoryState* trajectoryState,
  nvspFrontend_FrameExCallback cb,
  void* userData
) {
  if (!cb) return;

  // Same static asserts as emitFrames
  static_assert(sizeof(nvspFrontend_Frame) == sizeof(double) * kFrameFieldCount,
                "nvspFrontend_Frame must remain exactly kFrameFieldCount doubles with no padding");
  static_assert(std::is_standard_layout<nvspFrontend_Frame>::value,
                "nvspFrontend_Frame must remain standard-layout");
  static_assert(std::is_trivially_copyable<nvspFrontend_Frame>::value,
                "nvspFrontend_Frame must remain trivially copyable");

  const bool trillEnabled = (pack.lang.trillModulationMs > 0.0);

  const int vp = static_cast<int>(FieldId::voicePitch);
  const int evp = static_cast<int>(FieldId::endVoicePitch);
  const int va = static_cast<int>(FieldId::voiceAmplitude);
  const int fa = static_cast<int>(FieldId::fricationAmplitude);

  // Trill modulation constants (same as emitFrames)
  constexpr double kTrillCloseFactor = 0.22;
  constexpr double kTrillCloseFrac = 0.28;
  constexpr double kTrillFricFloor = 0.12;
  constexpr double kMinPhaseMs = 0.25;

  // Trajectory limiting state (per-handle, reset at utterance start)
  const LanguagePack& lang = pack.lang;
  trajectoryState->hasPrevFrame = false;

  // Track whether we've emitted at least one real frame
  bool hadPrevFrame = false;

  for (const Token& t : tokens) {
    if (t.silence || !t.def) {
      // For voiced closure (voice bar): use a NULL frame with a generous fade.
      // frame.cpp's NULL handling copies the entire previous frame and just zeros
      // preFormantGain, keeping all resonator coefficients (frequencies AND bandwidths)
      // stable.  This avoids IIR transients from bandwidth discontinuities that
      // caused clicks at 16 kHz and occasional clicks at 22050 Hz.
      if (t.voicedClosure && hadPrevFrame) {
        double vbFadeMs = t.fadeMs;
        if (vbFadeMs < 8.0) vbFadeMs = 8.0;
        cb(userData, nullptr, nullptr, t.durationMs, vbFadeMs, userIndexBase);
        continue;
      }
      
      // True silence frame - no FrameEx
      cb(userData, nullptr, nullptr, t.durationMs, t.fadeMs, userIndexBase);
      continue;
    }

    // Build base frame (same as emitFrames)
    double base[kFrameFieldCount] = {};
    const std::uint64_t mask = t.setMask;
    for (int f = 0; f < kFrameFieldCount; ++f) {
      if ((mask & (1ull << f)) == 0) continue;
      base[f] = t.field[f];
    }

    // Build FrameEx by mixing user defaults with per-phoneme values.
    // The mixing formula:
    //   - creakiness, breathiness, jitter, shimmer: additive, clamped to [0,1]
    //   - sharpness: multiplicative (phoneme * user), clamped to reasonable range
    //   - endCf1/2/3, endPf1/2/3: direct values (Hz), NAN if not set
    // Per-phoneme values override only if explicitly set (has* flags).
    nvspFrontend_FrameEx frameEx;
    
    // Get per-phoneme values (0 / 1.0 neutral if not set)
    double phonemeCreakiness = (t.def && t.def->hasCreakiness) ? t.def->creakiness : 0.0;
    double phonemeBreathiness = (t.def && t.def->hasBreathiness) ? t.def->breathiness : 0.0;
    double phonemeJitter = (t.def && t.def->hasJitter) ? t.def->jitter : 0.0;
    double phonemeShimmer = (t.def && t.def->hasShimmer) ? t.def->shimmer : 0.0;
    double phonemeSharpness = (t.def && t.def->hasSharpness) ? t.def->sharpness : 1.0;
    
    // Phoneme can only BOOST sharpness, never dull it - this ensures the user's
    // configured sharpness is never reduced by per-phoneme values. A phoneme
    // wanting "less sharp" would actually make it less distinct from neighbors.
    if (phonemeSharpness < 1.0) phonemeSharpness = 1.0;
    
    frameEx.creakiness = clamp01(phonemeCreakiness + frameExDefaults.creakiness);
    frameEx.breathiness = clamp01(phonemeBreathiness + frameExDefaults.breathiness);
    frameEx.jitter = clamp01(phonemeJitter + frameExDefaults.jitter);
    frameEx.shimmer = clamp01(phonemeShimmer + frameExDefaults.shimmer);

    double userSharpness = (frameExDefaults.sharpness > 0.0) ? frameExDefaults.sharpness : 1.0;
    frameEx.sharpness = clampSharpness(phonemeSharpness * userSharpness);    
    // Formant end targets: token-level (from coarticulation) takes priority,
    // then phoneme-level, otherwise NAN (no ramping).
    // This enables DECTalk-style within-frame formant ramping for CV transitions.
    frameEx.endCf1 = t.hasEndCf1 ? t.endCf1 : 
                     (t.def && t.def->hasEndCf1) ? t.def->endCf1 : NAN;
    frameEx.endCf2 = t.hasEndCf2 ? t.endCf2 : 
                     (t.def && t.def->hasEndCf2) ? t.def->endCf2 : NAN;
    frameEx.endCf3 = t.hasEndCf3 ? t.endCf3 : 
                     (t.def && t.def->hasEndCf3) ? t.def->endCf3 : NAN;
    frameEx.endPf1 = t.hasEndCf1 ? t.endCf1 :  // Parallel uses same as cascade for coart
                     (t.def && t.def->hasEndPf1) ? t.def->endPf1 : NAN;
    frameEx.endPf2 = t.hasEndCf2 ? t.endCf2 :
                     (t.def && t.def->hasEndPf2) ? t.def->endPf2 : NAN;
    frameEx.endPf3 = t.hasEndCf3 ? t.endCf3 :
                     (t.def && t.def->hasEndPf3) ? t.def->endPf3 : NAN;

    // Per-parameter transition speed scales (set by boundary_smoothing pass).
    frameEx.transF1Scale = t.transF1Scale;
    frameEx.transF2Scale = t.transF2Scale;
    frameEx.transF3Scale = t.transF3Scale;
    frameEx.transNasalScale = t.transNasalScale;

    // Detect source transitions for equal-power amplitude crossfade.
    // When voicing source type changes (voiced→voiceless or vice versa),
    // linear crossfade creates an energy dip. Equal-power fixes this.
    // We check the FRAME values (not token flags) because that's what
    // the DSP actually interpolates between.
    {
      double curVA = base[va];   // voiceAmplitude of this frame
      double curFA = base[fa];   // fricationAmplitude of this frame
      bool curVoiced   = (curVA > 0.05);
      bool curFricated = (curFA > 0.05);
      if (trajectoryState->hasPrevFrame) {
        bool prevVoiced   = (trajectoryState->prevVoiceAmp > 0.05);
        bool prevFricated = (trajectoryState->prevFricAmp > 0.05);
        bool sourceChange = (prevVoiced != curVoiced) ||
                            (prevFricated != curFricated);
        frameEx.transAmplitudeMode = sourceChange ? 1.0 : 0.0;
      } else {
        frameEx.transAmplitudeMode = 0.0;
      }
    }

    // Fujisaki pitch model parameters (set by applyPitchFujisaki)
    // These pass phrase/accent commands to the DSP for natural prosody contours.
    frameEx.fujisakiEnabled = t.fujisakiEnabled ? 1.0 : 0.0;
    frameEx.fujisakiReset = t.fujisakiReset ? 1.0 : 0.0;
    frameEx.fujisakiPhraseAmp = t.fujisakiPhraseAmp;
    frameEx.fujisakiPhraseLen = lang.fujisakiPhraseLen;  // 0 = DSP default
    frameEx.fujisakiAccentAmp = t.fujisakiAccentAmp;
    frameEx.fujisakiAccentDur = lang.fujisakiAccentDur;  // 0 = DSP default
    frameEx.fujisakiAccentLen = lang.fujisakiAccentLen;  // 0 = DSP default

    // Handle trill modulation (simplified version - emits micro-frames)
    if (trillEnabled && tokenIsTrill(t) && t.durationMs > 0.0) {
      double totalDur = t.durationMs;
      constexpr double kFixedTrillCycleMs = 28.0;
      double cycleMs = kFixedTrillCycleMs;
      if (cycleMs > totalDur) cycleMs = totalDur;

      double closeMs = cycleMs * kTrillCloseFrac;
      double openMs = cycleMs - closeMs;

      if (openMs < kMinPhaseMs) {
        openMs = kMinPhaseMs;
        closeMs = std::max(kMinPhaseMs, cycleMs - openMs);
      }
      if (closeMs < kMinPhaseMs) {
        closeMs = kMinPhaseMs;
        openMs = std::max(kMinPhaseMs, cycleMs - closeMs);
      }

      double microFadeMs = pack.lang.trillModulationFadeMs;
      if (microFadeMs <= 0.0) {
        microFadeMs = std::min(2.0, cycleMs * 0.12);
      }

      const bool hasVoiceAmp = ((mask & (1ull << va)) != 0);
      const bool hasFricAmp = ((mask & (1ull << fa)) != 0);
      const double baseVoiceAmp = base[va];
      const double baseFricAmp = base[fa];

      const double startPitch = base[vp];
      const double endPitch = base[evp];
      const double pitchDelta = endPitch - startPitch;

      double remaining = totalDur;
      double pos = 0.0;
      bool highPhase = true;
      bool firstPhase = true;

      while (remaining > 1e-9) {
        double phaseDur = highPhase ? openMs : closeMs;
        if (phaseDur > remaining) phaseDur = remaining;

        double t0 = (totalDur > 0.0) ? (pos / totalDur) : 0.0;
        double t1 = (totalDur > 0.0) ? ((pos + phaseDur) / totalDur) : 1.0;

        double seg[kFrameFieldCount];
        std::memcpy(seg, base, sizeof(seg));

        seg[vp] = startPitch + pitchDelta * t0;
        seg[evp] = startPitch + pitchDelta * t1;

        if (!highPhase) {
          if (hasVoiceAmp) {
            seg[va] = baseVoiceAmp * kTrillCloseFactor;
          }
          if (hasFricAmp && baseFricAmp > 0.0) {
            seg[fa] = std::max(baseFricAmp, kTrillFricFloor);
          }
        }

        nvspFrontend_Frame frame;
        std::memcpy(&frame, seg, sizeof(frame));

        double fadeIn = firstPhase ? t.fadeMs : microFadeMs;
        if (fadeIn <= 0.0) fadeIn = microFadeMs;
        if (fadeIn > phaseDur) fadeIn = phaseDur;

        cb(userData, &frame, &frameEx, phaseDur, fadeIn, userIndexBase);
        hadPrevFrame = true;

        remaining -= phaseDur;
        pos += phaseDur;
        highPhase = !highPhase;
        firstPhase = false;
      }

      continue;
    }

    // Normal frame emission
    nvspFrontend_Frame frame;
    std::memcpy(&frame, base, sizeof(frame));

    // Trajectory limiting (same as emitFrames)
    const bool isNasal = t.def && ((t.def->flags & kIsNasal) != 0);
    const bool skipTrajectoryLimit = (t.def && (
        (t.def->flags & kIsSemivowel) != 0 ||
        (t.def->flags & kIsLiquid) != 0 ||
        (t.def->flags & kIsNasal) != 0
    )) || trajectoryState->prevWasNasal;
    if (lang.trajectoryLimitEnabled && trajectoryState->hasPrevFrame && t.durationMs > 0.0 && !skipTrajectoryLimit) {
      const size_t idx_cf2 = static_cast<size_t>(FieldId::cf2);
      const size_t idx_cf3 = static_cast<size_t>(FieldId::cf3);
      const size_t idx_pf2 = static_cast<size_t>(FieldId::pf2);
      const size_t idx_pf3 = static_cast<size_t>(FieldId::pf3);
      double maxDelta, delta;

      const double effectiveDur = std::max(t.durationMs, 40.0);
      
      if ((lang.trajectoryLimitApplyMask & (1ULL << idx_cf2)) != 0) {
        if (lang.trajectoryLimitMaxHzPerMs[idx_cf2] > 0.0) {
          maxDelta = lang.trajectoryLimitMaxHzPerMs[idx_cf2] * effectiveDur;
          delta = frame.cf2 - trajectoryState->prevCf2;
          if (delta > maxDelta) frame.cf2 = trajectoryState->prevCf2 + maxDelta;
          else if (delta < -maxDelta) frame.cf2 = trajectoryState->prevCf2 - maxDelta;
        }
      }
      
      if ((lang.trajectoryLimitApplyMask & (1ULL << idx_cf3)) != 0) {
        if (lang.trajectoryLimitMaxHzPerMs[idx_cf3] > 0.0) {
          maxDelta = lang.trajectoryLimitMaxHzPerMs[idx_cf3] * effectiveDur;
          delta = frame.cf3 - trajectoryState->prevCf3;
          if (delta > maxDelta) frame.cf3 = trajectoryState->prevCf3 + maxDelta;
          else if (delta < -maxDelta) frame.cf3 = trajectoryState->prevCf3 - maxDelta;
        }
      }
      
      if ((lang.trajectoryLimitApplyMask & (1ULL << idx_pf2)) != 0) {
        if (lang.trajectoryLimitMaxHzPerMs[idx_pf2] > 0.0) {
          maxDelta = lang.trajectoryLimitMaxHzPerMs[idx_pf2] * effectiveDur;
          delta = frame.pf2 - trajectoryState->prevPf2;
          if (delta > maxDelta) frame.pf2 = trajectoryState->prevPf2 + maxDelta;
          else if (delta < -maxDelta) frame.pf2 = trajectoryState->prevPf2 - maxDelta;
        }
      }
      
      if ((lang.trajectoryLimitApplyMask & (1ULL << idx_pf3)) != 0) {
        if (lang.trajectoryLimitMaxHzPerMs[idx_pf3] > 0.0) {
          maxDelta = lang.trajectoryLimitMaxHzPerMs[idx_pf3] * effectiveDur;
          delta = frame.pf3 - trajectoryState->prevPf3;
          if (delta > maxDelta) frame.pf3 = trajectoryState->prevPf3 + maxDelta;
          else if (delta < -maxDelta) frame.pf3 = trajectoryState->prevPf3 - maxDelta;
        }
      }
    }
    
    // Update previous frame values
    trajectoryState->prevCf2 = frame.cf2;
    trajectoryState->prevCf3 = frame.cf3;
    trajectoryState->prevPf2 = frame.pf2;
    trajectoryState->prevPf3 = frame.pf3;
    trajectoryState->prevVoiceAmp = base[va];
    trajectoryState->prevFricAmp = base[fa];
    trajectoryState->hasPrevFrame = true;
    trajectoryState->prevWasNasal = isNasal;

    cb(userData, &frame, &frameEx, t.durationMs, t.fadeMs, userIndexBase);
    hadPrevFrame = true;
  }
}

} // namespace nvsp_frontend

/*
TGSpeechBox — Token-to-FrameEx conversion and emission.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

// frame_emit.cpp - TGSpeechBox — Frame emission for frontend
//
// Extracted from ipa_engine.cpp to reduce file size.
// Contains emitFrames() and emitFramesEx() implementations.

#include "ipa_engine.h"
#include "passes/pass_common.h"

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
  // Track whether previous real token was a stop/affricate/aspiration.
  // Used to skip fricative attack ramp in post-stop clusters (/ks/, /ts/, etc.)
  bool prevTokenWasStop = false;

  for (const Token& t : tokens) {
    // ============================================
    // VOICE BAR EMISSION (voiced stop closures)
    // ============================================
    // Build voice bar from the previous real frame (which has pitch, GOQ, outputGain,
    // vibrato — everything PhonemeDef lacks) then override just the voice-bar-specific
    // fields. Falls back to NULL frame if no previous base is available.
    if (t.voicedClosure && hadPrevFrame) {
      double vbFadeMs = t.fadeMs;
      if (vbFadeMs < 8.0) vbFadeMs = 8.0;

      if (trajectoryState->hasPrevBase) {
        double vb[kFrameFieldCount];
        std::memcpy(vb, trajectoryState->prevBase, sizeof(vb));

        double vbAmp = (t.def && t.def->hasVoiceBarAmplitude) ? t.def->voiceBarAmplitude : 0.3;
        double vbF1 = (t.def && t.def->hasVoiceBarF1) ? t.def->voiceBarF1 : 150.0;

        vb[va] = vbAmp;
        vb[fa] = 0.0;
        vb[static_cast<int>(FieldId::aspirationAmplitude)] = 0.0;
        vb[static_cast<int>(FieldId::cf1)] = vbF1;
        vb[static_cast<int>(FieldId::pf1)] = vbF1;
        vb[static_cast<int>(FieldId::preFormantGain)] = vbAmp;

        nvspFrontend_Frame vbFrame;
        std::memcpy(&vbFrame, vb, sizeof(vbFrame));
        cb(userData, &vbFrame, t.durationMs, vbFadeMs, userIndexBase);
      } else {
        cb(userData, nullptr, t.durationMs, vbFadeMs, userIndexBase);
      }
      continue;
    }

    if (t.silence || !t.def) {
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

    // Save full base for voice bar emission on the next voiced closure.
    std::memcpy(trajectoryState->prevBase, base, sizeof(base));
    trajectoryState->hasPrevBase = true;

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

      prevTokenWasStop = false;
      continue;
    }

    // ============================================
    // STOP BURST MICRO-FRAME EMISSION
    // ============================================
    // Stops and affricates get 2 micro-frames: a short burst followed by
    // a decayed residual. This replaces the single flat frame with a
    // time-varying amplitude envelope that models real stop releases.
    // Only fires on the main stop token, not gap/closure/aspiration satellites.
    {
      const bool isStop = t.def && ((t.def->flags & kIsStop) != 0);
      const bool isAffricate = t.def && ((t.def->flags & kIsAfricate) != 0);

      if ((isStop || isAffricate) && !t.silence && !t.preStopGap &&
          !t.postStopAspiration && !t.voicedClosure && t.durationMs > 1.0) {

        Place place = getPlace(t.def->key);

        // Place-based defaults (from Cho & Ladefoged 1999, Stevens 1998)
        double burstMs = 7.0;
        double decayRate = 0.5;
        double spectralTilt = 0.0;

        switch (place) {
          case Place::Labial:   burstMs = 5.0;  decayRate = 0.6;  spectralTilt = 0.1;   break;
          case Place::Alveolar: burstMs = 7.0;  decayRate = 0.5;  spectralTilt = 0.0;   break;
          case Place::Velar:    burstMs = 11.0; decayRate = 0.4;  spectralTilt = -0.15; break;
          case Place::Palatal:  burstMs = 9.0;  decayRate = 0.45; spectralTilt = -0.1;  break;
          default: break;
        }

        // Phoneme-level overrides
        if (t.def->hasBurstDurationMs) burstMs = t.def->burstDurationMs;
        if (t.def->hasBurstDecayRate) decayRate = t.def->burstDecayRate;
        if (t.def->hasBurstSpectralTilt) spectralTilt = t.def->burstSpectralTilt;

        // Clamp burst to 75% of token duration so it always fires,
        // even at high speech rates. Preserves place differentiation.
        double maxBurst = t.durationMs * 0.75;
        if (burstMs > maxBurst) burstMs = maxBurst;

        {
          // Pitch interpolation: slice the token's pitch ramp proportionally
          const double startPitch = base[vp];
          const double pitchDelta = base[evp] - startPitch;
          const double totalDur = t.durationMs;
          const double burstFrac = burstMs / totalDur;

          // --- Burst micro-frame ---
          double seg1[kFrameFieldCount];
          std::memcpy(seg1, base, sizeof(seg1));
          seg1[vp] = startPitch;
          seg1[evp] = startPitch + pitchDelta * burstFrac;

          const int pa3i = static_cast<int>(FieldId::pa3);
          const int pa4i = static_cast<int>(FieldId::pa4);
          const int pa5i = static_cast<int>(FieldId::pa5);
          const int pa6i = static_cast<int>(FieldId::pa6);

          if (spectralTilt < 0.0) {
            seg1[pa5i] = std::min(1.0, seg1[pa5i] * (1.0 - spectralTilt));
            seg1[pa6i] = std::min(1.0, seg1[pa6i] * (1.0 - spectralTilt * 0.7));
          } else if (spectralTilt > 0.0) {
            seg1[pa3i] = std::min(1.0, seg1[pa3i] * (1.0 + spectralTilt));
            seg1[pa4i] = std::min(1.0, seg1[pa4i] * (1.0 + spectralTilt * 0.7));
          }

          nvspFrontend_Frame burstFrame;
          std::memcpy(&burstFrame, seg1, sizeof(burstFrame));
          cb(userData, &burstFrame, burstMs, t.fadeMs, userIndexBase);
          hadPrevFrame = true;

          // --- Decay micro-frame ---
          double seg2[kFrameFieldCount];
          std::memcpy(seg2, base, sizeof(seg2));
          seg2[vp] = startPitch + pitchDelta * burstFrac;
          seg2[evp] = startPitch + pitchDelta;  // end of token

          const int faIdx = static_cast<int>(FieldId::fricationAmplitude);
          // Stops: decay frication (burst is the only noise source, it should die).
          // Affricates: keep frication at full — the whole point is sustained frication
          // after the burst transient. Only the spectral tilt boost decays.
          if (!isAffricate) {
            seg2[faIdx] *= (1.0 - decayRate);
          }

          nvspFrontend_Frame decayFrame;
          std::memcpy(&decayFrame, seg2, sizeof(decayFrame));
          double decayDur = t.durationMs - burstMs;
          double decayFade = std::min(burstMs * 0.5, decayDur);
          cb(userData, &decayFrame, decayDur, decayFade, userIndexBase);

          // Update trajectory state
          trajectoryState->prevCf2 = burstFrame.cf2;
          trajectoryState->prevCf3 = burstFrame.cf3;
          trajectoryState->prevPf2 = burstFrame.pf2;
          trajectoryState->prevPf3 = burstFrame.pf3;
          trajectoryState->prevVoiceAmp = base[va];
          trajectoryState->prevFricAmp = base[fa];
          trajectoryState->hasPrevFrame = true;
          trajectoryState->prevWasNasal = false;

          prevTokenWasStop = true;
          continue;
        }
      }
    }

    // ============================================
    // FRICATIVE ATTACK/DECAY MICRO-FRAME EMISSION
    // ============================================
    // Fricatives get 3 micro-frames: attack (ramp up), sustain, decay (ramp down).
    // This replaces the flat fricationAmplitude with a shaped envelope.
    // Only fires on non-stop fricatives (stops already handled above).
    {
      const bool isStop = t.def && ((t.def->flags & kIsStop) != 0);
      const bool isAffricate = t.def && ((t.def->flags & kIsAfricate) != 0);
      const double fricAmp = base[fa];

      if (!isStop && !isAffricate && !t.silence && !t.preStopGap &&
          !t.postStopAspiration && !t.voicedClosure && fricAmp > 0.0) {

        double attackMs = t.def->hasFricAttackMs ? t.def->fricAttackMs : 3.0;
        double decayMs = t.def->hasFricDecayMs ? t.def->fricDecayMs : 4.0;

        // Skip attack ramp in post-stop clusters (/ks/, /ts/, /ps/, etc.)
        // where the stop burst already provides transient energy.
        // Only emit micro-frames if token is long enough for attack + decay + 2ms sustain
        if (!prevTokenWasStop && attackMs + decayMs + 2.0 < t.durationMs) {
          const int faIdx = static_cast<int>(FieldId::fricationAmplitude);

          // Pitch interpolation across 3 micro-frames
          const double startPitch = base[vp];
          const double pitchDelta = base[evp] - startPitch;
          const double totalDur = t.durationMs;
          const double sustainDur = totalDur - attackMs - decayMs;
          const double attackFrac = attackMs / totalDur;
          const double sustainEndFrac = (attackMs + sustainDur) / totalDur;

          // --- Attack micro-frame: ramp from 10% to full ---
          double seg1[kFrameFieldCount];
          std::memcpy(seg1, base, sizeof(seg1));
          seg1[faIdx] = fricAmp * 0.1;
          seg1[vp] = startPitch;
          seg1[evp] = startPitch + pitchDelta * attackFrac;

          nvspFrontend_Frame attackFrame;
          std::memcpy(&attackFrame, seg1, sizeof(attackFrame));
          cb(userData, &attackFrame, attackMs, t.fadeMs, userIndexBase);
          hadPrevFrame = true;

          // --- Sustain micro-frame: full amplitude ---
          double seg2[kFrameFieldCount];
          std::memcpy(seg2, base, sizeof(seg2));
          seg2[vp] = startPitch + pitchDelta * attackFrac;
          seg2[evp] = startPitch + pitchDelta * sustainEndFrac;

          nvspFrontend_Frame sustainFrame;
          std::memcpy(&sustainFrame, seg2, sizeof(sustainFrame));
          cb(userData, &sustainFrame, sustainDur, attackMs, userIndexBase);

          // --- Decay micro-frame: ramp from full to 30% ---
          double seg3[kFrameFieldCount];
          std::memcpy(seg3, base, sizeof(seg3));
          seg3[faIdx] = fricAmp * 0.3;
          seg3[vp] = startPitch + pitchDelta * sustainEndFrac;
          seg3[evp] = startPitch + pitchDelta;

          nvspFrontend_Frame decayFrame;
          std::memcpy(&decayFrame, seg3, sizeof(decayFrame));
          cb(userData, &decayFrame, decayMs, decayMs * 0.5, userIndexBase);

          // Update trajectory state
          trajectoryState->prevCf2 = sustainFrame.cf2;
          trajectoryState->prevCf3 = sustainFrame.cf3;
          trajectoryState->prevPf2 = sustainFrame.pf2;
          trajectoryState->prevPf3 = sustainFrame.pf3;
          trajectoryState->prevVoiceAmp = base[va];
          trajectoryState->prevFricAmp = base[fa];
          trajectoryState->hasPrevFrame = true;
          trajectoryState->prevWasNasal = false;

          prevTokenWasStop = false;
          continue;
        }
        // else: token too short, fall through to normal emission
      }
    }

    // ============================================
    // RELEASE SPREAD (postStopAspiration tokens)
    // ============================================
    // Instead of instant aspiration onset, ramp in gradually over releaseSpreadMs.
    // Emit 2 micro-frames: a quiet ramp-in followed by full aspiration.
    if (t.postStopAspiration && t.def && t.durationMs > 1.0) {
      // Look up releaseSpreadMs from the aspiration phoneme's def (or default 4ms)
      double spreadMs = t.def->hasReleaseSpreadMs ? t.def->releaseSpreadMs : 4.0;

      if (spreadMs > 0.0 && spreadMs < t.durationMs) {
        const int faIdx = static_cast<int>(FieldId::fricationAmplitude);
        const int aspIdx = static_cast<int>(FieldId::aspirationAmplitude);

        // Pitch interpolation across 2 micro-frames
        const double startPitch = base[vp];
        const double pitchDelta = base[evp] - startPitch;
        const double totalDur = t.durationMs;
        const double spreadFrac = (totalDur > 0.0) ? (spreadMs / totalDur) : 0.0;

        // --- Ramp-in micro-frame: low aspiration/frication ---
        double seg1[kFrameFieldCount];
        std::memcpy(seg1, base, sizeof(seg1));
        seg1[faIdx] *= 0.15;
        seg1[aspIdx] *= 0.15;
        seg1[vp] = startPitch;
        seg1[evp] = startPitch + pitchDelta * spreadFrac;

        nvspFrontend_Frame rampFrame;
        std::memcpy(&rampFrame, seg1, sizeof(rampFrame));
        cb(userData, &rampFrame, spreadMs, t.fadeMs, userIndexBase);
        hadPrevFrame = true;

        // --- Full aspiration micro-frame ---
        double seg2[kFrameFieldCount];
        std::memcpy(seg2, base, sizeof(seg2));
        seg2[vp] = startPitch + pitchDelta * spreadFrac;
        seg2[evp] = startPitch + pitchDelta;

        nvspFrontend_Frame fullFrame;
        std::memcpy(&fullFrame, seg2, sizeof(fullFrame));
        double fullDur = t.durationMs - spreadMs;
        cb(userData, &fullFrame, fullDur, spreadMs * 0.5, userIndexBase);

        trajectoryState->prevCf2 = fullFrame.cf2;
        trajectoryState->prevCf3 = fullFrame.cf3;
        trajectoryState->prevPf2 = fullFrame.pf2;
        trajectoryState->prevPf3 = fullFrame.pf3;
        trajectoryState->prevVoiceAmp = base[va];
        trajectoryState->prevFricAmp = base[fa];
        trajectoryState->hasPrevFrame = true;
        trajectoryState->prevWasNasal = false;

        prevTokenWasStop = true;  // aspiration is stop-related
        continue;
      }
      // else: spread fills entire token or zero, fall through to normal emission
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

    // Update prevTokenWasStop for the normal emission path.
    // Stops/affricates that fell through (burst >= duration) and postStopAspiration.
    prevTokenWasStop = t.def && (
      ((t.def->flags & kIsStop) != 0) ||
      ((t.def->flags & kIsAfricate) != 0) ||
      t.postStopAspiration);
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
  bool prevTokenWasStop = false;

  for (const Token& t : tokens) {
    // ============================================
    // VOICE BAR EMISSION (voiced stop closures) — Ex path
    // ============================================
    if (t.voicedClosure && hadPrevFrame) {
      double vbFadeMs = t.fadeMs;
      if (vbFadeMs < 8.0) vbFadeMs = 8.0;

      if (trajectoryState->hasPrevBase) {
        double vb[kFrameFieldCount];
        std::memcpy(vb, trajectoryState->prevBase, sizeof(vb));

        double vbAmp = (t.def && t.def->hasVoiceBarAmplitude) ? t.def->voiceBarAmplitude : 0.3;
        double vbF1 = (t.def && t.def->hasVoiceBarF1) ? t.def->voiceBarF1 : 150.0;

        vb[va] = vbAmp;
        vb[fa] = 0.0;
        vb[static_cast<int>(FieldId::aspirationAmplitude)] = 0.0;
        vb[static_cast<int>(FieldId::cf1)] = vbF1;
        vb[static_cast<int>(FieldId::pf1)] = vbF1;
        vb[static_cast<int>(FieldId::preFormantGain)] = vbAmp;

        nvspFrontend_Frame vbFrame;
        std::memcpy(&vbFrame, vb, sizeof(vbFrame));

        nvspFrontend_FrameEx vbFrameEx = trajectoryState->hasPrevFrameEx
            ? trajectoryState->prevFrameEx
            : frameExDefaults;
        vbFrameEx.transAmplitudeMode = 1.0;  // equal-power crossfade
        // Keep Fujisaki model running (don't reset IIR state) but don't re-fire commands.
        vbFrameEx.fujisakiPhraseAmp = 0.0;
        vbFrameEx.fujisakiAccentAmp = 0.0;
        vbFrameEx.fujisakiReset = 0.0;
        cb(userData, &vbFrame, &vbFrameEx, t.durationMs, vbFadeMs, userIndexBase);
      } else {
        cb(userData, nullptr, nullptr, t.durationMs, vbFadeMs, userIndexBase);
      }
      continue;
    }

    if (t.silence || !t.def) {
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

    // Save full base for voice bar emission on the next voiced closure.
    std::memcpy(trajectoryState->prevBase, base, sizeof(base));
    trajectoryState->hasPrevBase = true;

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
    double tokenBreathiness = t.hasTokenBreathiness ? t.tokenBreathiness : 0.0;
    frameEx.breathiness = clamp01(phonemeBreathiness + tokenBreathiness + frameExDefaults.breathiness);
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

    // Save frameEx for voice bar emission (keeps Fujisaki model alive during closures).
    trajectoryState->prevFrameEx = frameEx;
    trajectoryState->hasPrevFrameEx = true;

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

      prevTokenWasStop = false;
      continue;
    }

    // ============================================
    // STOP BURST MICRO-FRAME EMISSION (Ex path)
    // ============================================
    {
      const bool isStop = t.def && ((t.def->flags & kIsStop) != 0);
      const bool isAffricate = t.def && ((t.def->flags & kIsAfricate) != 0);

      if ((isStop || isAffricate) && !t.silence && !t.preStopGap &&
          !t.postStopAspiration && !t.voicedClosure && t.durationMs > 1.0) {

        Place place = getPlace(t.def->key);

        double burstMs = 7.0;
        double decayRate = 0.5;
        double spectralTilt = 0.0;

        switch (place) {
          case Place::Labial:   burstMs = 5.0;  decayRate = 0.6;  spectralTilt = 0.1;   break;
          case Place::Alveolar: burstMs = 7.0;  decayRate = 0.5;  spectralTilt = 0.0;   break;
          case Place::Velar:    burstMs = 11.0; decayRate = 0.4;  spectralTilt = -0.15; break;
          case Place::Palatal:  burstMs = 9.0;  decayRate = 0.45; spectralTilt = -0.1;  break;
          default: break;
        }

        if (t.def->hasBurstDurationMs) burstMs = t.def->burstDurationMs;
        if (t.def->hasBurstDecayRate) decayRate = t.def->burstDecayRate;
        if (t.def->hasBurstSpectralTilt) spectralTilt = t.def->burstSpectralTilt;

        // Clamp burst to 75% of token duration so it always fires
        double maxBurst = t.durationMs * 0.75;
        if (burstMs > maxBurst) burstMs = maxBurst;

        {
          // Pitch interpolation across 2 micro-frames
          const double startPitch = base[vp];
          const double pitchDelta = base[evp] - startPitch;
          const double totalDur = t.durationMs;
          const double burstFrac = burstMs / totalDur;

          double seg1[kFrameFieldCount];
          std::memcpy(seg1, base, sizeof(seg1));
          seg1[vp] = startPitch;
          seg1[evp] = startPitch + pitchDelta * burstFrac;

          const int pa3i = static_cast<int>(FieldId::pa3);
          const int pa4i = static_cast<int>(FieldId::pa4);
          const int pa5i = static_cast<int>(FieldId::pa5);
          const int pa6i = static_cast<int>(FieldId::pa6);

          if (spectralTilt < 0.0) {
            seg1[pa5i] = std::min(1.0, seg1[pa5i] * (1.0 - spectralTilt));
            seg1[pa6i] = std::min(1.0, seg1[pa6i] * (1.0 - spectralTilt * 0.7));
          } else if (spectralTilt > 0.0) {
            seg1[pa3i] = std::min(1.0, seg1[pa3i] * (1.0 + spectralTilt));
            seg1[pa4i] = std::min(1.0, seg1[pa4i] * (1.0 + spectralTilt * 0.7));
          }

          nvspFrontend_Frame burstFrame;
          std::memcpy(&burstFrame, seg1, sizeof(burstFrame));
          cb(userData, &burstFrame, &frameEx, burstMs, t.fadeMs, userIndexBase);
          hadPrevFrame = true;

          double seg2[kFrameFieldCount];
          std::memcpy(seg2, base, sizeof(seg2));
          seg2[vp] = startPitch + pitchDelta * burstFrac;
          seg2[evp] = startPitch + pitchDelta;

          const int faIdx = static_cast<int>(FieldId::fricationAmplitude);
          if (!isAffricate) {
            seg2[faIdx] *= (1.0 - decayRate);
          }

          nvspFrontend_Frame decayFrame;
          std::memcpy(&decayFrame, seg2, sizeof(decayFrame));
          double decayDur = t.durationMs - burstMs;
          double decayFade = std::min(burstMs * 0.5, decayDur);
          cb(userData, &decayFrame, &frameEx, decayDur, decayFade, userIndexBase);

          trajectoryState->prevCf2 = burstFrame.cf2;
          trajectoryState->prevCf3 = burstFrame.cf3;
          trajectoryState->prevPf2 = burstFrame.pf2;
          trajectoryState->prevPf3 = burstFrame.pf3;
          trajectoryState->prevVoiceAmp = base[va];
          trajectoryState->prevFricAmp = base[fa];
          trajectoryState->hasPrevFrame = true;
          trajectoryState->prevWasNasal = false;

          prevTokenWasStop = true;
          continue;
        }
      }
    }

    // ============================================
    // FRICATIVE ATTACK/DECAY MICRO-FRAME EMISSION (Ex path)
    // ============================================
    {
      const bool isStop2 = t.def && ((t.def->flags & kIsStop) != 0);
      const bool isAffricate2 = t.def && ((t.def->flags & kIsAfricate) != 0);
      const double fricAmp = base[fa];

      if (!isStop2 && !isAffricate2 && !t.silence && !t.preStopGap &&
          !t.postStopAspiration && !t.voicedClosure && fricAmp > 0.0) {

        double attackMs = t.def->hasFricAttackMs ? t.def->fricAttackMs : 3.0;
        double decayMs = t.def->hasFricDecayMs ? t.def->fricDecayMs : 4.0;

        // Skip attack ramp in post-stop clusters (/ks/, /ts/, etc.)
        if (!prevTokenWasStop && attackMs + decayMs + 2.0 < t.durationMs) {
          const int faIdx = static_cast<int>(FieldId::fricationAmplitude);

          // Pitch interpolation across 3 micro-frames
          const double startPitch = base[vp];
          const double pitchDelta = base[evp] - startPitch;
          const double totalDur = t.durationMs;
          const double sustainDur = totalDur - attackMs - decayMs;
          const double attackFrac = attackMs / totalDur;
          const double sustainEndFrac = (attackMs + sustainDur) / totalDur;

          // --- Attack micro-frame: ramp from 10% to full ---
          double seg1[kFrameFieldCount];
          std::memcpy(seg1, base, sizeof(seg1));
          seg1[faIdx] = fricAmp * 0.1;
          seg1[vp] = startPitch;
          seg1[evp] = startPitch + pitchDelta * attackFrac;

          nvspFrontend_Frame attackFrame;
          std::memcpy(&attackFrame, seg1, sizeof(attackFrame));
          cb(userData, &attackFrame, &frameEx, attackMs, t.fadeMs, userIndexBase);
          hadPrevFrame = true;

          // --- Sustain micro-frame: full amplitude ---
          double seg2s[kFrameFieldCount];
          std::memcpy(seg2s, base, sizeof(seg2s));
          seg2s[vp] = startPitch + pitchDelta * attackFrac;
          seg2s[evp] = startPitch + pitchDelta * sustainEndFrac;

          nvspFrontend_Frame sustainFrame;
          std::memcpy(&sustainFrame, seg2s, sizeof(sustainFrame));
          cb(userData, &sustainFrame, &frameEx, sustainDur, attackMs, userIndexBase);

          // --- Decay micro-frame: ramp from full to 30% ---
          double seg3[kFrameFieldCount];
          std::memcpy(seg3, base, sizeof(seg3));
          seg3[faIdx] = fricAmp * 0.3;
          seg3[vp] = startPitch + pitchDelta * sustainEndFrac;
          seg3[evp] = startPitch + pitchDelta;

          nvspFrontend_Frame decayFrame;
          std::memcpy(&decayFrame, seg3, sizeof(decayFrame));
          cb(userData, &decayFrame, &frameEx, decayMs, decayMs * 0.5, userIndexBase);

          trajectoryState->prevCf2 = sustainFrame.cf2;
          trajectoryState->prevCf3 = sustainFrame.cf3;
          trajectoryState->prevPf2 = sustainFrame.pf2;
          trajectoryState->prevPf3 = sustainFrame.pf3;
          trajectoryState->prevVoiceAmp = base[va];
          trajectoryState->prevFricAmp = base[fa];
          trajectoryState->hasPrevFrame = true;
          trajectoryState->prevWasNasal = false;

          prevTokenWasStop = false;
          continue;
        }
      }
    }

    // ============================================
    // RELEASE SPREAD (postStopAspiration tokens) — Ex path
    // ============================================
    if (t.postStopAspiration && t.def && t.durationMs > 1.0) {
      double spreadMs = t.def->hasReleaseSpreadMs ? t.def->releaseSpreadMs : 4.0;

      if (spreadMs > 0.0 && spreadMs < t.durationMs) {
        const int faIdx = static_cast<int>(FieldId::fricationAmplitude);
        const int aspIdx = static_cast<int>(FieldId::aspirationAmplitude);

        // Pitch interpolation across 2 micro-frames
        const double startPitch = base[vp];
        const double pitchDelta = base[evp] - startPitch;
        const double totalDur = t.durationMs;
        const double spreadFrac = (totalDur > 0.0) ? (spreadMs / totalDur) : 0.0;

        double seg1[kFrameFieldCount];
        std::memcpy(seg1, base, sizeof(seg1));
        seg1[faIdx] *= 0.15;
        seg1[aspIdx] *= 0.15;
        seg1[vp] = startPitch;
        seg1[evp] = startPitch + pitchDelta * spreadFrac;

        nvspFrontend_Frame rampFrame;
        std::memcpy(&rampFrame, seg1, sizeof(rampFrame));
        cb(userData, &rampFrame, &frameEx, spreadMs, t.fadeMs, userIndexBase);
        hadPrevFrame = true;

        double seg2[kFrameFieldCount];
        std::memcpy(seg2, base, sizeof(seg2));
        seg2[vp] = startPitch + pitchDelta * spreadFrac;
        seg2[evp] = startPitch + pitchDelta;

        nvspFrontend_Frame fullFrame;
        std::memcpy(&fullFrame, seg2, sizeof(fullFrame));
        double fullDur = t.durationMs - spreadMs;
        cb(userData, &fullFrame, &frameEx, fullDur, spreadMs * 0.5, userIndexBase);

        trajectoryState->prevCf2 = fullFrame.cf2;
        trajectoryState->prevCf3 = fullFrame.cf3;
        trajectoryState->prevPf2 = fullFrame.pf2;
        trajectoryState->prevPf3 = fullFrame.pf3;
        trajectoryState->prevVoiceAmp = base[va];
        trajectoryState->prevFricAmp = base[fa];
        trajectoryState->hasPrevFrame = true;
        trajectoryState->prevWasNasal = false;

        prevTokenWasStop = true;  // aspiration is stop-related
        continue;
      }
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

    prevTokenWasStop = t.def && (
      ((t.def->flags & kIsStop) != 0) ||
      ((t.def->flags & kIsAfricate) != 0) ||
      t.postStopAspiration);
  }
}

} // namespace nvsp_frontend

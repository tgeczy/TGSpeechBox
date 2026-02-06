/*
TGSpeechBox — DSP common utilities and tuning constants.

Small helper classes and numeric constants shared across the DSP engine.
All classes are header-only (methods defined inline) so no separate
compilation unit is needed.
*/

#ifndef TGSPEECHBOX_DSPCOMMON_H
#define TGSPEECHBOX_DSPCOMMON_H

#define _USE_MATH_DEFINES

#include <cmath>
#include <cstdint>
#include <cstdlib>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

const double PITWO = M_PI * 2;

// ============================================================================
// Numeric helpers
// ============================================================================

static inline double clampDouble(double v, double lo, double hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// -----------------------------------------------------------------------------
// Formant sweep bandwidth handling
//
// Sweeping a resonator's center frequency while holding bandwidth constant changes
// effective Q (= F/B). For upward sweeps this narrows the resonance and can yield
// a "whistly / boxy" quality as individual harmonics get over-emphasized.
// To keep sweeps sounding speech-like we cap Q by widening bandwidth as needed.
//
// Applied only when the current frame provides endCf/endPf targets (diphthongs etc.).
// -----------------------------------------------------------------------------

static inline double bandwidthForSweep(double freqHz, double baseBwHz, double qMax, double bwMinHz, double bwMaxHz) {
    if (!std::isfinite(freqHz) || !std::isfinite(baseBwHz) || freqHz <= 0.0 || baseBwHz <= 0.0) {
        return baseBwHz;
    }
    // Enforce minimum bandwidth (and thus a maximum Q).
    double bw = baseBwHz;
    double bwFromQ = freqHz / qMax;
    if (bwFromQ > bw) bw = bwFromQ;
    return clampDouble(bw, bwMinHz, bwMaxHz);
}

// ============================================================================
// Formant sweep Q limits
// ============================================================================

// Limits used only during within-phoneme formant sweeps (endCf/endPf).
// These keep resonators from becoming ultra-high-Q as formants move upward.
const double kSweepQMaxF1 = 10.0;
const double kSweepQMaxF2 = 18.0;
const double kSweepQMaxF3 = 20.0;

const double kSweepBwMinF1 = 30.0;
const double kSweepBwMinF2 = 40.0;
const double kSweepBwMinF3 = 60.0;
const double kSweepBwMax = 1000.0;

// ============================================================================
// Tuning knobs (DSP-layer)
// ============================================================================

// Radiation characteristic: 
// The derivative (dFlow) is naturally very quiet compared to the flow.
// We apply gain to dFlow to match the loudness of flow.
const double kRadiationDerivGainBase = 5.0;      
const double kRadiationDerivGainRefSr = 22050.0; 

// Turbulence gating curvature when glottis is open.
const double kTurbulenceFlowPower = 1.5;

// Frication shaping
const double kFricNoiseScale = 0.175;
const double kFricSoftClipK = 0.18;
const double kBypassMinGain = 0.70;
const double kBypassVoicedDuck = 0.20;
const double kVoicedFricDuck = 0.18;
const double kVoicedFricDuckPower = 1.0;

// ------------------------------------------------------------
// Adaptive frication lowpass (targets stop bursts, preserves sustained fricatives)
// ------------------------------------------------------------
// For bursts (fast rise in fricationAmplitude): use a lower cutoff (more lowpass)
// to stop "everything turns into /t/".
// For sustained fricatives (stable frication): use a higher cutoff so /s/ stays crisp.
// This helps distinguish /k/ (more mid-weighted) from /t/ (sharper) by taking
// the top edge off only at the burst onset.

// Sample-rate-aware cutoff frequencies for frication
// At 11025 Hz, Nyquist is ~5512 Hz so we need lower cutoffs
const double kFricBurstFc_11k   = 3800.0;   // 11025 Hz (Nyquist ~5512) - more aggressive
const double kFricSustainFc_11k = 5000.0;
const double kFricBurstFc_16k   = 5200.0;   // 16000 Hz (Nyquist 8000)
const double kFricSustainFc_16k = 7200.0;
const double kFricBurstFc_22k   = 3600.0;   // 22050 Hz (Nyquist ~11025)
const double kFricSustainFc_22k = 9500.0;
const double kFricBurstFc_44k   = 4200.0;   // 44100 Hz (Nyquist ~22050)
const double kFricSustainFc_44k = 14000.0;

// Sample-rate-aware cutoff frequencies for aspiration burst LP
// More aggressive than frication since aspiration through cascade is often
// the real culprit for "sharp" stop releases
const double kAspBurstFc_11k = 2400.0;   // 11025 Hz - more aggressive
const double kAspBurstFc_16k = 3200.0;   // 16000 Hz
const double kAspBurstFc_22k = 2200.0;   // 22050 Hz
const double kAspBurstFc_44k = 2500.0;   // 44100 Hz

// Burstiness detection sensitivity (higher = more sensitive to fast rises)
const double kBurstinessScale = 25.0;

// ------------------------------------------------------------
// Breathiness macro tuning (per-frame tilt offset)
// ------------------------------------------------------------
// Breathiness already drives turbulence, OQ, and pulse shape.
// This adds per-frame spectral TILT offset for true airy voice quality.
// Without tilt, you get "noisy voicing" (hoarseness).
// With tilt, you get "breathy voicing" (airy, soft highs).

// Max tilt offset at breathiness=1.0 (positive = darker/softer highs for VOICED)
const double kBreathinessTiltMaxDb = 6.0;

// Max aspiration tilt offset at breathiness=1.0 (NEGATIVE = darker/softer noise)
// This makes the breath noise spectrally match the softened voice
const double kBreathinessAspTiltMaxDb = -8.0;

// Smoothing time constant to prevent clicks when breathiness changes
const double kBreathinessTiltSmoothMs = 8.0;

// ============================================================================
// FastRandom — thread-local PRNG (replaces stdlib rand())
// ============================================================================
// Linear Congruential Generator - fast, no locking, good enough spectral
// properties for audio noise. Each NoiseGenerator/VoiceGenerator instance
// gets its own state, eliminating thread contention.

class FastRandom {
private:
    uint32_t seed;

public:
    FastRandom(uint32_t s = 12345): seed(s) {}

    void setSeed(uint32_t s) { seed = s; }

    // Returns [0, 1)
    inline double nextDouble() {
        seed = seed * 1664525u + 1013904223u;  // LCG constants from Numerical Recipes
        return (double)(seed >> 1) * (1.0 / 2147483648.0);
    }

    // Returns [-1, 1)
    inline double nextBipolar() {
        seed = seed * 1664525u + 1013904223u;
        return (double)((int32_t)seed) * (1.0 / 2147483648.0);
    }
};

// ============================================================================
// NoiseGenerator — brownish and white noise
// ============================================================================

class NoiseGenerator {
private:
    double lastValue;
    FastRandom rng;

public:
    NoiseGenerator(): lastValue(0.0), rng(54321) {}

    void reset() {
        lastValue=0.0;
    }

    // Brownish noise (smoothed random) - original behavior for frication etc.
    double getNext() {
        lastValue=(rng.nextDouble()-0.5)+0.75*lastValue;
        return lastValue;
    }
    
    // White noise - flat spectrum, better for aspiration tilt to act on
    double white() {
        return rng.nextBipolar();
    }
};

// ============================================================================
// FrequencyGenerator — phase accumulator
// ============================================================================

class FrequencyGenerator {
private:
    int sampleRate;
    double lastCyclePos;

public:
    FrequencyGenerator(int sr): sampleRate(sr), lastCyclePos(0) {}

    void reset() {
        lastCyclePos=0;
    }

    double getNext(double frequency) {
        double cyclePos=fmod((frequency/sampleRate)+lastCyclePos,1);
        lastCyclePos=cyclePos;
        return cyclePos;
    }
};

// ============================================================================
// OnePoleLowpass — simple one-pole lowpass for adaptive frication filtering
// ============================================================================

class OnePoleLowpass {
private:
    int sampleRate;
    double alpha;
    double z;

public:
    OnePoleLowpass(int sr): sampleRate(sr), alpha(0.0), z(0.0) {}

    void setCutoffHz(double fcHz) {
        if (fcHz < 10.0) fcHz = 10.0;
        double nyq = 0.5 * (double)sampleRate;
        if (fcHz > nyq * 0.95) fcHz = nyq * 0.95;
        alpha = exp(-PITWO * fcHz / (double)sampleRate);
    }

    double process(double x) {
        z = (1.0 - alpha) * x + alpha * z;
        return z;
    }

    void reset() { z = 0.0; }
};

#endif // TGSPEECHBOX_DSPCOMMON_H

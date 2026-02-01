/*
This file is a part of the NV Speech Player project. 
URL: https://bitbucket.org/nvaccess/speechplayer
Copyright 2014 NV Access Limited.
This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License version 2.0, as published by
the Free Software Foundation.
This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
This license can be found at:
http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
*/

/*
Based on klsyn-88, found at http://linguistics.berkeley.edu/phonlab/resources/
*/

#define _USE_MATH_DEFINES

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include "debug.h"
#include "utils.h"
#include "speechWaveGenerator.h"

using namespace std;

const double PITWO=M_PI*2;

// ------------------------------------------------------------
// Tuning knobs (DSP-layer).
// ------------------------------------------------------------

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

class NoiseGenerator {
private:
    double lastValue;

public:
    NoiseGenerator(): lastValue(0.0) {}

    void reset() {
        lastValue=0.0;
    }

    double getNext() {
        lastValue=(((double)rand()/(double)RAND_MAX)-0.5)+0.75*lastValue;
        return lastValue;
    }
};

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

// ------------------------------------------------------------
// One-pole lowpass filter for adaptive frication filtering
// ------------------------------------------------------------
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

class VoiceGenerator {
private:
    int sampleRate;
    FrequencyGenerator pitchGen;
    FrequencyGenerator vibratoGen;
    NoiseGenerator aspirationGen;
    double lastFlow;
    double lastVoicedIn;
    double lastVoicedOut;
    double lastVoicedSrc;
    double lastAspOut;  // for exposing aspiration to caller

    // Optional noise AM on the glottal cycle (aspiration + frication).
    double noiseGlottalModDepth;
    double lastNoiseMod;

    // Smooth aspiration gain to avoid clicks when aspirationAmplitude changes quickly.
    double smoothAspAmp;
    bool smoothAspAmpInit;
    double aspAttackCoeff;
    double aspReleaseCoeff;

    // Per-frame voice-quality modulation (DSP v5+)
    double lastCyclePos;
    double jitterMul;
    double shimmerMul;

    double voicingPeakPos;
    double voicedPreEmphA;
    double voicedPreEmphMix;

    // Speed quotient: glottal pulse asymmetry (V3 voicingTone)
    double speedQuotient;

    // Spectral tilt (Bipolar)
    double tiltTargetTlDb;
    double tiltTlDb;

    double tiltPole;
    double tiltPoleTarget;
    double tiltState;

    double tiltTlAlpha;
    double tiltPoleAlpha;

    double tiltRefHz;
    double tiltLastTlForTargets;

    // Radiation Gain (Applied ONLY to dFlow)
    double radiationDerivGain;
    
    // Radiation Mix: 0.0 = Flow (Warm), 1.0 = Derivative (Bright)
    double radiationMix;

    static double clampDouble(double v, double lo, double hi) {
        if (v < lo) return lo;
        if (v > hi) return hi;
        return v;
    }

    double calcPoleForTiltDb(double refHz, double tiltDb) const {
        if (fabs(tiltDb) < 1e-5) return 0.0;
        
        // POSITIVE TILT (Darken): Solve for attenuation
        if (tiltDb > 0.0) { 
            double nyq = 0.5 * (double)sampleRate;
            if (refHz < 1.0) refHz = 1.0;
            if (refHz > nyq * 0.95) refHz = nyq * 0.95;
            double g = pow(10.0, -tiltDb / 20.0);
            double g2 = g * g;
            double w = PITWO * refHz / (double)sampleRate;
            double cosw = cos(w);
            double A = g2 - 1.0;
            double B = 2.0 * (1.0 - g2 * cosw);
            double disc = B*B - 4.0*A*A; 
            
            if (disc < 0.0) return 0.0; 
            double sqrtDisc = sqrt(disc);
            double denom = 2.0 * A;
            if (fabs(denom) < 1e-18) return 0.0;

            double a1 = (-B + sqrtDisc) / denom;
            double a2 = (-B - sqrtDisc) / denom;
            double a = 0.0;
            bool ok1 = (a1 >= 0.0 && a1 < 1.0);
            bool ok2 = (a2 >= 0.0 && a2 < 1.0);
              if (ok1 && ok2) a = (a1 < a2) ? a1 : a2;
              else if (ok1) a = a1;
              else if (ok2) a = a2;
            else a = a1; // fallback
return clampDouble(a, 0.0, 0.9999);
        }
        // NEGATIVE TILT (Brighten): Solve for boost at Nyquist
        else {
            double targetGain = pow(10.0, -tiltDb / 20.0);
            double a = (1.0 - targetGain) / (1.0 + targetGain);
            return clampDouble(a, -0.9, -0.0001);
        }
    }

    void updateTiltTargets(double tlDbNow) {
        double tl = clampDouble(tlDbNow, -24.0, 24.0);
        tiltPoleTarget = calcPoleForTiltDb(tiltRefHz, tl);
        
        // INTELLIGENT RADIATION MIX:
        // - Negative Tilt (Brightening): We fade towards Derivative (Mix 1.0). 
        //   Flow (Integral) is naturally devoid of highs, so boosting it does little.
        //   Derivative has the highs we need to boost.
        // - Zero/Positive Tilt (Warm/Dark): We stick to Flow (Mix 0.0).
        //   This provides the "Warmth" and "Body" you requested at 0.
        
        if (tl < 0.0) {
            // Fade to derivative as we get brighter
            radiationMix = clampDouble(-tl / 10.0, 0.0, 1.0); 
        } else {
            // Keep pure flow for neutral and dark tones
            radiationMix = 0.0;
        }
    }

    double applyTilt(double in) {
        tiltTlDb += (tiltTargetTlDb - tiltTlDb) * tiltTlAlpha;
        if (fabs(tiltTlDb - tiltLastTlForTargets) > 0.01) {
            updateTiltTargets(tiltTlDb);
            tiltLastTlForTargets = tiltTlDb;
        }
        tiltPole += (tiltPoleTarget - tiltPole) * tiltPoleAlpha;
        double out = (1.0 - tiltPole) * in + tiltPole * tiltState;
        tiltState = out;
        return out;
    }

public:
    bool glottisOpen;

    VoiceGenerator(int sr): sampleRate(sr), pitchGen(sr), vibratoGen(sr), aspirationGen(),
        lastFlow(0.0), lastVoicedIn(0.0), lastVoicedOut(0.0), lastVoicedSrc(0.0), lastAspOut(0.0),
        noiseGlottalModDepth(0.0), lastNoiseMod(1.0),
        smoothAspAmp(0.0), smoothAspAmpInit(false),
        aspAttackCoeff(0.0), aspReleaseCoeff(0.0),
        lastCyclePos(0.0), jitterMul(1.0), shimmerMul(1.0),
        glottisOpen(false),
        voicingPeakPos(0.91), voicedPreEmphA(0.92), voicedPreEmphMix(0.35),
        tiltTargetTlDb(0.0), tiltTlDb(0.0),
        tiltPole(0.0), tiltPoleTarget(0.0), tiltState(0.0),
        tiltTlAlpha(0.0), tiltPoleAlpha(0.0),
        tiltRefHz(3000.0),
        tiltLastTlForTargets(1e9),
        radiationDerivGain(1.0),
        radiationMix(0.0) {

        const double tlSmoothMs = 8.0;
        const double poleSmoothMs = 5.0;

        tiltTlAlpha = 1.0 - exp(-1.0 / (sampleRate * (tlSmoothMs * 0.001)));
        tiltPoleAlpha = 1.0 - exp(-1.0 / (sampleRate * (poleSmoothMs * 0.001)));

        // Aspiration gain smoothing (attack/release in ms).
        // This helps avoid random clicks when aspirationAmplitude changes quickly.
        const double kAspAmpAttackMs = 1.0;
        const double kAspAmpReleaseMs = 3.0;
        aspAttackCoeff = 1.0 - exp(-1.0 / (0.001 * kAspAmpAttackMs * sampleRate));
        aspReleaseCoeff = 1.0 - exp(-1.0 / (0.001 * kAspAmpReleaseMs * sampleRate));

        double nyq = 0.5 * (double)sampleRate;
        if (tiltRefHz > nyq * 0.95) tiltRefHz = nyq * 0.95;
        if (tiltRefHz < 500.0) tiltRefHz = 500.0;

        radiationDerivGain = kRadiationDerivGainBase * ((double)sampleRate / kRadiationDerivGainRefSr);

        speechPlayer_voicingTone_t defaults = SPEECHPLAYER_VOICINGTONE_DEFAULTS;
        voicingPeakPos = defaults.voicingPeakPos;
        voicedPreEmphA = defaults.voicedPreEmphA;
        voicedPreEmphMix = defaults.voicedPreEmphMix;
        noiseGlottalModDepth = clampDouble(defaults.noiseGlottalModDepth, 0.0, 1.0);
        speedQuotient = clampDouble(defaults.speedQuotient, 0.5, 4.0);
        setTiltDbPerOct(defaults.voicedTiltDbPerOct);

        tiltTlDb = tiltTargetTlDb;
        updateTiltTargets(tiltTlDb);
        tiltPole = tiltPoleTarget;
        tiltLastTlForTargets = tiltTlDb;
    }

    void reset() {
        pitchGen.reset();
        vibratoGen.reset();
        aspirationGen.reset();
        lastFlow=0.0;
        lastVoicedIn=0.0;
        lastVoicedOut=0.0;
        lastVoicedSrc=0.0;
        lastAspOut=0.0;
        lastNoiseMod=1.0;
        smoothAspAmp = 0.0;
        smoothAspAmpInit = false;
        lastCyclePos = 0.0;
        jitterMul = 1.0;
        shimmerMul = 1.0;
        glottisOpen=false;
    }

    void setTiltDbPerOct(double tiltVal) {
        tiltTargetTlDb = clampDouble(tiltVal, -24.0, 24.0);
    }

    void setVoicingParams(double peakPos, double preEmphA, double preEmphMix, double tiltDb, double noiseModDepth, double sq) {
        voicingPeakPos = peakPos;
        voicedPreEmphA = preEmphA;
        voicedPreEmphMix = preEmphMix;
        noiseGlottalModDepth = clampDouble(noiseModDepth, 0.0, 1.0);
        speedQuotient = clampDouble(sq, 0.5, 4.0);
        setTiltDbPerOct(tiltDb);
    }

    void getVoicingParams(double* peakPos, double* preEmphA, double* preEmphMix, double* tiltDb, double* noiseModDepth, double* sq) const {
        if (peakPos) *peakPos = voicingPeakPos;
        if (preEmphA) *preEmphA = voicedPreEmphA;
        if (preEmphMix) *preEmphMix = voicedPreEmphMix;
        if (tiltDb) *tiltDb = tiltTargetTlDb;
        if (noiseModDepth) *noiseModDepth = noiseGlottalModDepth;
        if (sq) *sq = speedQuotient;
    }

    void setSpeedQuotient(double sq) {
        speedQuotient = clampDouble(sq, 0.5, 4.0);
    }

    double getSpeedQuotient() const {
        return speedQuotient;
    }

    double getLastNoiseMod() const { return lastNoiseMod; }

    double getNext(const speechPlayer_frame_t* frame, const speechPlayer_frameEx_t* frameEx) {
        // Optional per-frame voice quality (DSP v5+). If frameEx is NULL, all effects are disabled.
        double creakiness = 0.0;
        double breathiness = 0.0;
        double jitter = 0.0;
        double shimmer = 0.0;
        if (frameEx) {
            creakiness = frameEx->creakiness;
            breathiness = frameEx->breathiness;
            jitter = frameEx->jitter;
            shimmer = frameEx->shimmer;

            if (!std::isfinite(creakiness)) creakiness = 0.0;
            if (!std::isfinite(breathiness)) breathiness = 0.0;
            if (!std::isfinite(jitter)) jitter = 0.0;
            if (!std::isfinite(shimmer)) shimmer = 0.0;

            creakiness = clampDouble(creakiness, 0.0, 1.0);
            breathiness = clampDouble(breathiness, 0.0, 1.0);
            jitter = clampDouble(jitter, 0.0, 1.0);
            shimmer = clampDouble(shimmer, 0.0, 1.0);
        }

        double vibrato=(sin(vibratoGen.getNext(frame->vibratoSpeed)*PITWO)*0.06*frame->vibratoPitchOffset)+1;
        double pitchHz = frame->voicePitch * vibrato;
        if (!std::isfinite(pitchHz) || pitchHz < 0.0) pitchHz = 0.0;

        // Creaky voice tends to have slightly lower F0 and more irregularity.
        if (creakiness > 0.0) {
            pitchHz *= (1.0 - 0.12 * creakiness);
        }

        // If we are unvoiced, reset per-cycle multipliers so voiced segments restart clean.
        if (pitchHz <= 0.0) {
            jitterMul = 1.0;
            shimmerMul = 1.0;
        }

        // Apply per-cycle jitter multiplier (updated on cycle wraps).
        pitchHz *= jitterMul;

        double cyclePos = pitchGen.getNext(pitchHz > 0.0 ? pitchHz : 0.0);

        // Detect start of a new glottal cycle.
        const bool cycleWrapped = (pitchHz > 0.0) && (cyclePos < lastCyclePos);
        lastCyclePos = cyclePos;

        if (cycleWrapped) {
            // Map [0..1] to perceptible ranges.
            // - jitter: relative F0 variation (0.02 = realistic, but inaudible; use 0.15 for testing)
            // - shimmer: relative amplitude variation
            double jitterRel = (jitter * 0.15) + (creakiness * 0.05);
            if (jitterRel > 0.0) {
                double r = ((double)rand() / (double)RAND_MAX) * 2.0 - 1.0;
                jitterMul = 1.0 + (r * jitterRel);
                if (jitterMul < 0.2) jitterMul = 0.2;
            } else {
                jitterMul = 1.0;
            }

            double shimmerRel = (shimmer * 0.70) + (creakiness * 0.12);
            if (shimmerRel > 0.0) {
                double r = ((double)rand() / (double)RAND_MAX) * 2.0 - 1.0;
                shimmerMul = 1.0 + (r * shimmerRel);
                if (shimmerMul < 0.0) shimmerMul = 0.0;
            } else {
                shimmerMul = 1.0;
            }
        }

        // Optional Klatt-style glottal-cycle AM for noise sources.
        // When enabled, the second half of the cycle is attenuated.
        // We normalize mean gain to 1.0 so existing amplitude tuning stays sane.
        double noiseMod = 1.0;
        if (noiseGlottalModDepth > 0.0 && pitchHz > 0.0) {
            const double halfCycleAtten = 0.5 * noiseGlottalModDepth; // depth 1.0 => 0.5 attenuation
            noiseMod = (cyclePos < 0.5) ? 1.0 : (1.0 - halfCycleAtten);
            double meanGain = 1.0 - 0.25 * noiseGlottalModDepth;
            if (meanGain < 0.001) meanGain = 0.001;
            noiseMod /= meanGain;
        }
        lastNoiseMod = noiseMod;

        double aspiration=aspirationGen.getNext()*0.1*noiseMod;

        double effectiveOQ = frame->glottalOpenQuotient;
        if (effectiveOQ <= 0.0) effectiveOQ = 0.4;
        if (effectiveOQ < 0.10) effectiveOQ = 0.10;
        if (effectiveOQ > 0.95) effectiveOQ = 0.95;

        // Creakiness: shorter open phase (more closed time) in this model.
        if (creakiness > 0.0) {
            effectiveOQ += 0.10 * creakiness;
            if (effectiveOQ > 0.95) effectiveOQ = 0.95;
        }

        glottisOpen = (pitchHz > 0.0) && (cyclePos >= effectiveOQ);

        double flow = 0.0;
        if(glottisOpen) {
            double openLen = 1.0 - effectiveOQ;
            if (openLen < 0.0001) openLen = 0.0001;

            // Per-frame voice quality tweaks to pulse shape:
            // - breathiness nudges the peak later (softer/relaxed)
            // - creakiness nudges the peak earlier (tenser/pressed)
            double peakPos = voicingPeakPos + (0.02 * breathiness) - (0.05 * creakiness);

            double dt = 0.0;
            if (pitchHz > 0.0) dt = pitchHz / (double)sampleRate;
            double denom = openLen - dt;
            if (denom < 0.0001) denom = 0.0001;
            double phase = (cyclePos - effectiveOQ) / denom;
            if (phase < 0.0) phase = 0.0;
            if (phase > 1.0) phase = 1.0;

            const double minCloseSamples = 2.0;
            if (pitchHz > 0.0) {
                double periodSamples = (double)sampleRate / pitchHz;
                double minCloseFrac = minCloseSamples / (periodSamples * openLen);
                if (minCloseFrac > 0.5) minCloseFrac = 0.5;
                double limitPeakPos = 1.0 - minCloseFrac;
                if (limitPeakPos < peakPos) peakPos = limitPeakPos;
                if (peakPos < 0.50) peakPos = 0.50;
            }

            // Hybrid glottal source based on sample rate:
            // - At 11025 Hz: Blend favoring symmetric cosine (fuller, less aliasing)
            // - At 16000+ Hz: Full LF-inspired asymmetric waveform (more harmonics)
            // - Between: Smooth blend for gradual transition
            // 
            // The blend preserves fricative clarity (from LF edge) while
            // reducing aliasing artifacts (from cosine smoothness).

            // Compute symmetric cosine flow (original SpeechPlayer)
            double flowCosine;
            if (phase < peakPos) {
                flowCosine = 0.5 * (1.0 - cos(phase * M_PI / peakPos));
            } else {
                flowCosine = 0.5 * (1.0 + cos((phase - peakPos) * M_PI / (1.0 - peakPos)));
            }

            // Compute LF-inspired flow (asymmetric, more harmonics)
            // Speed quotient affects the asymmetry:
            //   SQ < 2.0: Slower opening, gentler closing (female-like)
            //   SQ = 2.0: Default/neutral
            //   SQ > 2.0: Faster opening, sharper closing (male-like, pressed)
            double flowLF;
            if (phase < peakPos) {
                // Opening phase: polynomial rise
                // speedQuotient affects the curve steepness
                double t = phase / peakPos;
                // Higher SQ = faster opening (steeper curve)
                // Lower SQ = slower opening (gentler curve)
                double openPower = 2.0 + (speedQuotient - 2.0) * 0.5;  // Range ~1.25 to ~3.0
                if (openPower < 1.0) openPower = 1.0;
                if (openPower > 4.0) openPower = 4.0;
                double tPow = pow(t, openPower);
                flowLF = tPow * (3.0 - 2.0 * t);  // Modified smoothstep
            } else {
                // Closing phase: sharper fall with "return phase" character
                double t = (phase - peakPos) / (1.0 - peakPos);
                // Sample-rate-dependent base sharpness:
                // Higher sample rates need sharper closure for fuller harmonics.
                double baseSharpness;
                if (sampleRate >= 44100) {
                    baseSharpness = 10.0;
                } else if (sampleRate >= 32000) {
                    baseSharpness = 8.0;
                } else if (sampleRate >= 22050) {
                    baseSharpness = 4.0;
                } else if (sampleRate >= 16000) {
                    baseSharpness = 3.0;
                } else {
                    baseSharpness = 2.5;
                }
                // Speed quotient modulates the closing sharpness:
                //   SQ=0.5: sharpness * 0.4 (very gentle, breathy)
                //   SQ=2.0: sharpness * 1.0 (default)
                //   SQ=4.0: sharpness * 1.6 (very sharp, pressed)
                double sqFactor = 0.4 + (speedQuotient - 0.5) * (0.6 / 1.5);  // Linear map
                if (sqFactor < 0.3) sqFactor = 0.3;
                if (sqFactor > 2.0) sqFactor = 2.0;
                double sharpness = baseSharpness * sqFactor;
                flowLF = pow(1.0 - t, sharpness);
            }

            // Blend based on sample rate:
            // - 11025 Hz: 70% cosine, 30% LF (enough edge for fricatives)
            // - 14000 Hz: 50/50 blend
            // - 16000+ Hz: 100% LF
            double lfBlend;
            if (sampleRate <= 11025) {
                lfBlend = 0.30;  // 30% LF at low rates - keeps some edge for consonants
            } else if (sampleRate >= 16000) {
                lfBlend = 1.0;   // 100% LF at high rates
            } else {
                // Linear interpolation between 11025 and 16000
                lfBlend = 0.30 + 0.70 * (double)(sampleRate - 11025) / (16000.0 - 11025.0);
            }

            flow = (1.0 - lfBlend) * flowCosine + lfBlend * flowLF;
        }

        const double flowScale = 1.6;
        flow *= flowScale;

        double dFlow = flow - lastFlow;
        lastFlow = flow;

        // ------------------------------------------------------------
        // Radiation Characteristic (Gain Corrected):
        // ------------------------------------------------------------
        // dFlow (Derivative) is naturally tiny. It needs gain to match flow.
        // Flow (Integral) is naturally loud. It DOES NOT need gain.

        double srcDeriv = dFlow * radiationDerivGain;
        // srcFlow is just flow.

        // At Tilt 0: Mix is 0.0 -> voicedSrc = flow. (WARM/FAT, no clipping)
        // At Tilt -20: Mix is 1.0 -> voicedSrc = srcDeriv. (BRIGHT/BUZZY)
        double voicedSrc = (1.0 - radiationMix) * flow + (radiationMix * srcDeriv);

        // Voiced-only pre-emphasis
        double pre = voicedSrc - (voicedPreEmphA * lastVoicedSrc);
        lastVoicedSrc = voicedSrc;
        voicedSrc = (1.0 - voicedPreEmphMix) * voicedSrc + voicedPreEmphMix * pre;

        // Klatt TL (Bipolar)
        voicedSrc = applyTilt(voicedSrc);

        // Breathiness adds extra turbulence during the open phase.
        double voiceTurbAmp = frame->voiceTurbulenceAmplitude;
        if (!std::isfinite(voiceTurbAmp)) voiceTurbAmp = 0.0;
        voiceTurbAmp = clampDouble(voiceTurbAmp, 0.0, 1.0);
        if (breathiness > 0.0) {
            voiceTurbAmp = clampDouble(voiceTurbAmp + (1.0 * breathiness), 0.0, 1.0);
        }

        double turbulence = aspiration * voiceTurbAmp;
        if(glottisOpen) {
            double flow01 = flow / flowScale;
            if(flow01 < 0.0) flow01 = 0.0;
            if(flow01 > 1.0) flow01 = 1.0;
            turbulence *= pow(flow01, kTurbulenceFlowPower);
        } else {
            turbulence = 0.0;
        }

        // Voice amplitude with optional shimmer/creakiness/breathiness scaling.
        double voiceAmp = frame->voiceAmplitude;
        if (!std::isfinite(voiceAmp)) voiceAmp = 0.0;
        voiceAmp = clampDouble(voiceAmp, 0.0, 1.0);
        if (creakiness > 0.0) {
            voiceAmp *= (1.0 - (0.35 * creakiness));
        }
        if (breathiness > 0.0) {
            // Breathy voice has weaker vocal fold vibration
            voiceAmp *= (1.0 - (0.40 * breathiness));
        }
        voiceAmp *= shimmerMul;

        double voicedIn = (voicedSrc + turbulence) * voiceAmp;
        const double dcPole = 0.9995;
        double voiced = voicedIn - lastVoicedIn + (dcPole * lastVoicedOut);
        lastVoicedIn = voicedIn;
        lastVoicedOut = voiced;

        // Smooth aspirationAmplitude (fast attack, slower release) to avoid clicks.
        double targetAspAmp = frame->aspirationAmplitude;
        if (!std::isfinite(targetAspAmp)) targetAspAmp = 0.0;
        if (targetAspAmp < 0.0) targetAspAmp = 0.0;
        if (targetAspAmp > 1.0) targetAspAmp = 1.0;

        if (breathiness > 0.0) {
            targetAspAmp = clampDouble(targetAspAmp + (1.0 * breathiness), 0.0, 1.0);
        }

        if (!smoothAspAmpInit) {
            smoothAspAmp = targetAspAmp;
            smoothAspAmpInit = true;
        } else {
            const double coeff = (targetAspAmp > smoothAspAmp) ? aspAttackCoeff : aspReleaseCoeff;
            smoothAspAmp += (targetAspAmp - smoothAspAmp) * coeff;
        }

        double aspOut = aspiration * smoothAspAmp;
        lastAspOut = aspOut;
        return aspOut + voiced;
    }

    double getLastAspOut() const { return lastAspOut; }
};
// Resonator + formant path helpers (based on the classic klsyn-style 2-pole
// sections, with a few safety/ordering fixes borrowed from other Klatt ports.)
class Resonator {
private:
    int sampleRate;
    double frequency;
    double bandwidth;
    bool anti;
    bool setOnce;
    double a, b, c;
    double p1, p2;

public:
    Resonator(int sampleRate, bool anti=false) {
        this->sampleRate=sampleRate;
        this->anti=anti;
        this->setOnce=false;
        this->p1=0;
        this->p2=0;
    }

    void setParams(double frequency, double bandwidth) {
        if(!setOnce||(frequency!=this->frequency)||(bandwidth!=this->bandwidth)) {
            this->frequency=frequency;
            this->bandwidth=bandwidth;

            const double nyquist = 0.5 * (double)sampleRate;
            const bool invalid = (!std::isfinite(frequency) || !std::isfinite(bandwidth));
            const bool disabled = (frequency <= 0.0 || bandwidth <= 0.0 || frequency >= nyquist);

            // Treat invalid/disabled sections as a straight passthrough.
            // This is important because many "unused" formants are represented
            // by zeroed params in frame data.
            if (invalid || disabled) {
                a = 1.0;
                b = 0.0;
                c = 0.0;
                setOnce = true;
                return;
            }

            double r = exp(-M_PI/sampleRate*bandwidth);
            c = -(r*r);
            b = r*cos(PITWO*frequency/(double)sampleRate)*2.0;
            a = 1.0-b-c;

            if(anti) {
                // Antiresonator is implemented as an inverted resonator transfer.
                // Avoid division by ~0 for extreme parameter values.
                if (!std::isfinite(a) || fabs(a) < 1e-12) {
                    a = 1.0;
                    b = 0.0;
                    c = 0.0;
                } else {
                    double invA = 1.0 / a;
                    a = invA;
                    b *= -invA;
                    c *= -invA;
                }
            }
        }
        this->setOnce=true;
    }

    double resonate(double in, double frequency, double bandwidth, bool allowUpdate=true) {
        if(allowUpdate) setParams(frequency,bandwidth);
        double out=a*in+b*p1+c*p2;
        p2=p1;
        p1=anti?in:out;
        return out;
    }

    void reset() {
        p1=0;
        p2=0;
        setOnce=false;
    }
};

// Pitch-synchronous F1 resonator
// Based on Qlatt pitch-sync-mod crate and Klatt 1980
// This models the acoustic coupling between glottal source and vocal tract
// that occurs during the open phase of voicing.
// 
// NOTE: We intentionally do NOT use Fujisaki state scaling here.
// While Qlatt does this, it requires very precise pitch-synchronous timing
// that we don't have. Doing it naively causes clicks.
class PitchSyncResonator {
private:
    int sampleRate;
    double a, b, c;
    double p1, p2;
    double frequency, bandwidth;
    bool setOnce;
    
    // Pitch-sync state
    double deltaFreq, deltaBw;    // Deltas to apply during open phase
    double lastTargetFreq, lastTargetBw;
    
    // Smoothing to prevent clicks at glottal boundaries
    double smoothFreq, smoothBw;
    double smoothAlpha;

    void computeCoeffs(double freq, double bw) {
        const double nyquist = 0.5 * (double)sampleRate;
        if (!std::isfinite(freq) || !std::isfinite(bw) ||
            freq <= 0.0 || bw <= 0.0 || freq >= nyquist) {
            a = 1.0; b = 0.0; c = 0.0;
            return;
        }
        double r = exp(-M_PI / sampleRate * bw);
        c = -(r * r);
        b = r * cos(PITWO * freq / (double)sampleRate) * 2.0;
        a = 1.0 - b - c;
    }

public:
    PitchSyncResonator(int sr) : sampleRate(sr), a(1.0), b(0.0), c(0.0),
        p1(0.0), p2(0.0), frequency(0.0), bandwidth(0.0), setOnce(false),
        deltaFreq(0.0), deltaBw(0.0), lastTargetFreq(0.0), lastTargetBw(0.0),
        smoothFreq(0.0), smoothBw(0.0), smoothAlpha(0.0) {
        // Smooth over ~2ms to prevent clicks at glottal transitions
        double smoothMs = 2.0;
        smoothAlpha = 1.0 - exp(-1.0 / (sampleRate * smoothMs * 0.001));
    }

    void reset() {
        p1 = 0.0;
        p2 = 0.0;
        setOnce = false;
        smoothFreq = 0.0;
        smoothBw = 0.0;
    }

    void setPitchSyncParams(double dF1, double dB1) {
        deltaFreq = dF1;
        deltaBw = dB1;
    }

    double resonate(double in, double freq, double bw, bool glottisOpen) {
        // Determine target F1/B1 based on glottal phase
        double targetFreq, targetBw;
        if (deltaFreq != 0.0 || deltaBw != 0.0) {
            // Pitch-sync modulation enabled
            if (glottisOpen) {
                // Open phase: apply deltas (raises F1, widens B1)
                targetFreq = freq + deltaFreq;
                targetBw = bw + deltaBw;
            } else {
                // Closed phase: use base values
                targetFreq = freq;
                targetBw = bw;
            }
            
            // Smooth the transitions to prevent clicks
            if (smoothFreq == 0.0) smoothFreq = targetFreq;
            if (smoothBw == 0.0) smoothBw = targetBw;
            smoothFreq += (targetFreq - smoothFreq) * smoothAlpha;
            smoothBw += (targetBw - smoothBw) * smoothAlpha;
            
            targetFreq = smoothFreq;
            targetBw = smoothBw;
        } else {
            // No pitch-sync modulation - use params directly
            targetFreq = freq;
            targetBw = bw;
        }
        
        // Only update coefficients if params changed
        if (!setOnce || targetFreq != lastTargetFreq || targetBw != lastTargetBw) {
            lastTargetFreq = targetFreq;
            lastTargetBw = targetBw;
            computeCoeffs(targetFreq, targetBw);
            setOnce = true;
        }
        
        // Standard resonator difference equation
        double out = a * in + b * p1 + c * p2;
        p2 = p1;
        p1 = out;
        return out;
    }
};

class CascadeFormantGenerator { 
private:
    int sampleRate;
    PitchSyncResonator r1;  // F1 gets pitch-sync treatment
    Resonator r2, r3, r4, r5, r6, rN0, rNP;
    
    // Pitch-sync params from voicingTone
    double pitchSyncF1Delta;
    double pitchSyncB1Delta;

public:
    CascadeFormantGenerator(int sr): sampleRate(sr), r1(sr), r2(sr), r3(sr), r4(sr), r5(sr), r6(sr), rN0(sr,true), rNP(sr),
        pitchSyncF1Delta(0.0), pitchSyncB1Delta(0.0) {}

    void reset() {
        r1.reset(); r2.reset(); r3.reset(); r4.reset(); r5.reset(); r6.reset(); rN0.reset(); rNP.reset();
    }
    
    void setPitchSyncParams(double f1DeltaHz, double b1DeltaHz) {
        pitchSyncF1Delta = f1DeltaHz;
        pitchSyncB1Delta = b1DeltaHz;
        r1.setPitchSyncParams(f1DeltaHz, b1DeltaHz);
    }

    double getNext(const speechPlayer_frame_t* frame, bool glottisOpen, double input) {
        input/=2.0;
        // Klatt cascade: N0 (antiresonator) -> NP (resonator), then cascade formants.
        // NOTE: Our phoneme tables were tuned with the classic high-to-low cascade order
        // (F6 -> F1). Even though Klatt 1980 notes some flexibility, changing the order
        // can audibly affect transitions (and can introduce clicks). So we preserve it.

        // Simple nasal fade: caNP crossfades between direct path and the NZ/NP path.
        // This keeps behavior consistent with the original SpeechPlayer tuning.
        const double n0Output = rN0.resonate(input, frame->cfN0, frame->cbN0);
        double output = calculateValueAtFadePosition(
            input,
            rNP.resonate(n0Output, frame->cfNP, frame->cbNP),
            frame->caNP
        );

        output = r6.resonate(output, frame->cf6, frame->cb6);
        output = r5.resonate(output, frame->cf5, frame->cb5);
        output = r4.resonate(output, frame->cf4, frame->cb4);
        output = r3.resonate(output, frame->cf3, frame->cb3);
        output = r2.resonate(output, frame->cf2, frame->cb2);
        // F1 uses pitch-synchronous resonator without Fujisaki compensation (dropped as we don't have F1 spikes it worked with.)
        output = r1.resonate(output, frame->cf1, frame->cb1, glottisOpen);
        return output;
    }
};

class ParallelFormantGenerator { 
private:
    int sampleRate;
    Resonator r1, r2, r3, r4, r5, r6;

public:
    ParallelFormantGenerator(int sr): sampleRate(sr), r1(sr), r2(sr), r3(sr), r4(sr), r5(sr), r6(sr) {}

    void reset() {
        r1.reset(); r2.reset(); r3.reset(); r4.reset(); r5.reset(); r6.reset();
    }

    double getNext(const speechPlayer_frame_t* frame, bool glottisOpen, double input) {
        input/=2.0;
        (void)glottisOpen;
        double output=0;
        output+=(r1.resonate(input,frame->pf1,frame->pb1)-input)*frame->pa1;
        output+=(r2.resonate(input,frame->pf2,frame->pb2)-input)*frame->pa2;
        output+=(r3.resonate(input,frame->pf3,frame->pb3)-input)*frame->pa3;
        output+=(r4.resonate(input,frame->pf4,frame->pb4)-input)*frame->pa4;
        output+=(r5.resonate(input,frame->pf5,frame->pb5)-input)*frame->pa5;
        output+=(r6.resonate(input,frame->pf6,frame->pb6)-input)*frame->pa6;
        return calculateValueAtFadePosition(output,input,frame->parallelBypass);
    }
};

class SpeechWaveGeneratorImpl: public SpeechWaveGenerator {
private:
    int sampleRate;
    VoiceGenerator voiceGenerator;
    NoiseGenerator fricGenerator;
    CascadeFormantGenerator cascade;
    ParallelFormantGenerator parallel;
    FrameManager* frameManager;
    double lastInput;
    double lastOutput;
    bool wasSilence;

    double smoothPreGain;
    double preGainAttackAlpha;
    double preGainReleaseAlpha;

    // Smooth frication amplitude to avoid sharp edges at fricative→vowel boundaries
    double smoothFricAmp;
    double fricAttackAlpha;
    double fricReleaseAlpha;

    // High-shelf EQ state for brightness
    double hsIn1, hsIn2, hsOut1, hsOut2;
    double hsB0, hsB1, hsB2, hsA1, hsA2;

    // Current voicing tone parameters (for high-shelf recalculation)
    speechPlayer_voicingTone_t currentTone;

    // ------------------------------------------------------------
    // Adaptive frication filtering (burst vs sustained)
    // ------------------------------------------------------------
    // Uses two parallel lowpass paths: one for burst (lower cutoff),
    // one for sustained frication (higher cutoff). We crossfade based
    // on "burstiness" (rate of change of fricationAmplitude).
    OnePoleLowpass fricBurstLp1, fricBurstLp2;
    OnePoleLowpass fricSustainLp1, fricSustainLp2;
    double lastTargetFricAmp;  // for transient (burst) detection - uses RAW target, not smoothed
    double lastTargetAspAmp;   // for aspiration burst detection (stops often use asp, not fric)
    double fricBurstFc;        // sample-rate-aware burst cutoff
    double fricSustainFc;      // sample-rate-aware sustain cutoff
    double burstEnv;           // 0..1, holds burstiness for a few ms
    double burstEnvDecayMul;   // per-sample decay multiplier

    // Aspiration lowpass: filters the aspiration noise that goes through the cascade
    // This is often the real culprit for "sharp" stop releases, not the frication path
    OnePoleLowpass aspLp1, aspLp2;
    double aspBurstFc;         // sample-rate-aware aspiration burst cutoff

    // Shelf ducking: reduce high-shelf boost during bursts to tame stop sharpness
    // without affecting voiced brightness baseline
    double shelfMix;           // 0..1, smoothed crossfade between unshelved and shelved
    double shelfMixAlpha;      // smoothing coefficient

    // Stop fade-out: when frames stop (interrupt), fade out over ~4ms to avoid click
    double lastBrightOut;      // store last post-shelf sample for fade tail
    int stopFadeRemaining;     // samples left in fade-out
    int stopFadeTotal;         // total fade-out length

    void initHighShelf(double fc, double gainDb, double Q) {
        // Clamp inputs to prevent NaNs and weird filter behavior from bad UI values
        double nyq = 0.5 * (double)sampleRate;
        if (!std::isfinite(fc)) fc = 2000.0;
        if (!std::isfinite(gainDb)) gainDb = 0.0;
        if (!std::isfinite(Q)) Q = 0.7;
        if (fc < 20.0) fc = 20.0;
        if (fc > nyq * 0.95) fc = nyq * 0.95;
        if (Q < 0.1) Q = 0.1;
        if (Q > 4.0) Q = 4.0;
        if (gainDb < -24.0) gainDb = -24.0;
        if (gainDb > 24.0) gainDb = 24.0;

        double A = pow(10.0, gainDb / 40.0);
        double w0 = PITWO * fc / sampleRate;
        double cosw0 = cos(w0);
        double sinw0 = sin(w0);
        double alpha = sinw0 / (2.0 * Q);

        double a0 = (A+1) - (A-1)*cosw0 + 2*sqrt(A)*alpha;
        hsB0 = (A*((A+1) + (A-1)*cosw0 + 2*sqrt(A)*alpha)) / a0;
        hsB1 = (-2*A*((A-1) + (A+1)*cosw0)) / a0;
        hsB2 = (A*((A+1) + (A-1)*cosw0 - 2*sqrt(A)*alpha)) / a0;
        hsA1 = (2*((A-1) - (A+1)*cosw0)) / a0;
        hsA2 = ((A+1) - (A-1)*cosw0 - 2*sqrt(A)*alpha) / a0;
    }

    double applyHighShelf(double in) {
        double out = hsB0*in + hsB1*hsIn1 + hsB2*hsIn2 - hsA1*hsOut1 - hsA2*hsOut2;
        hsIn2 = hsIn1;
        hsIn1 = in;
        hsOut2 = hsOut1;
        hsOut1 = out;
        return out;
    }

public:
    SpeechWaveGeneratorImpl(int sr): sampleRate(sr), voiceGenerator(sr), fricGenerator(), cascade(sr), parallel(sr), frameManager(NULL), lastInput(0.0), lastOutput(0.0), wasSilence(true), smoothPreGain(0.0), preGainAttackAlpha(0.0), preGainReleaseAlpha(0.0), smoothFricAmp(0.0), fricAttackAlpha(0.0), fricReleaseAlpha(0.0), hsIn1(0), hsIn2(0), hsOut1(0), hsOut2(0), fricBurstLp1(sr), fricBurstLp2(sr), fricSustainLp1(sr), fricSustainLp2(sr), lastTargetFricAmp(0.0), lastTargetAspAmp(0.0), fricBurstFc(0.0), fricSustainFc(0.0), burstEnv(0.0), burstEnvDecayMul(1.0), aspLp1(sr), aspLp2(sr), aspBurstFc(0.0), shelfMix(1.0), shelfMixAlpha(0.0), lastBrightOut(0.0), stopFadeRemaining(0), stopFadeTotal(0) {
        const double attackMs = 1.0;
        const double releaseMs = 0.5;
        preGainAttackAlpha = 1.0 - exp(-1.0 / (sampleRate * (attackMs * 0.001)));
        preGainReleaseAlpha = 1.0 - exp(-1.0 / (sampleRate * (releaseMs * 0.001)));

        // Frication smoothing (ms)
        const double fricAttackMs = 0.8;
        const double fricReleaseMs = 1.2;
        fricAttackAlpha = 1.0 - exp(-1.0 / (sampleRate * (fricAttackMs * 0.001)));
        fricReleaseAlpha = 1.0 - exp(-1.0 / (sampleRate * (fricReleaseMs * 0.001)));

        // Hold burstiness for ~6 ms so the burst LP actually affects stop releases
        const double burstHoldMs = 6.0;
        burstEnvDecayMul = exp(-1.0 / (sampleRate * (burstHoldMs * 0.001)));

        // Smooth shelf mix changes to avoid clicks (fast-ish)
        const double shelfMixMs = 4.0;
        shelfMixAlpha = 1.0 - exp(-1.0 / (sampleRate * (shelfMixMs * 0.001)));

        // Initialize with default voicing tone
        currentTone = speechPlayer_getDefaultVoicingTone();

        // High shelf: use defaults from voicing tone
        initHighShelf(currentTone.highShelfFcHz, currentTone.highShelfGainDb, currentTone.highShelfQ);

        // ------------------------------------------------------------
        // Adaptive frication lowpass: select cutoffs based on sample rate
        // ------------------------------------------------------------
        // Interpolate between known sample rates for smooth behavior
        if (sampleRate <= 11025) {
            fricBurstFc = kFricBurstFc_11k;
            fricSustainFc = kFricSustainFc_11k;
        } else if (sampleRate <= 16000) {
            // Interpolate between 11k and 16k
            double t = (double)(sampleRate - 11025) / (16000.0 - 11025.0);
            fricBurstFc = kFricBurstFc_11k + t * (kFricBurstFc_16k - kFricBurstFc_11k);
            fricSustainFc = kFricSustainFc_11k + t * (kFricSustainFc_16k - kFricSustainFc_11k);
        } else if (sampleRate <= 22050) {
            // Interpolate between 16k and 22k
            double t = (double)(sampleRate - 16000) / (22050.0 - 16000.0);
            fricBurstFc = kFricBurstFc_16k + t * (kFricBurstFc_22k - kFricBurstFc_16k);
            fricSustainFc = kFricSustainFc_16k + t * (kFricSustainFc_22k - kFricSustainFc_16k);
        } else if (sampleRate <= 44100) {
            // Interpolate between 22k and 44k
            double t = (double)(sampleRate - 22050) / (44100.0 - 22050.0);
            fricBurstFc = kFricBurstFc_22k + t * (kFricBurstFc_44k - kFricBurstFc_22k);
            fricSustainFc = kFricSustainFc_22k + t * (kFricSustainFc_44k - kFricSustainFc_22k);
        } else {
            // Above 44100: scale proportionally from 44k values
            double scale = (double)sampleRate / 44100.0;
            fricBurstFc = kFricBurstFc_44k * scale;
            fricSustainFc = kFricSustainFc_44k * scale;
        }

        // Set cutoff frequencies for the frication lowpass filters
        fricBurstLp1.setCutoffHz(fricBurstFc);
        fricBurstLp2.setCutoffHz(fricBurstFc);
        fricSustainLp1.setCutoffHz(fricSustainFc);
        fricSustainLp2.setCutoffHz(fricSustainFc);

        // ------------------------------------------------------------
        // Aspiration lowpass: this is often the real culprit for sharp stops
        // Use more aggressive cutoffs than frication since this is the "too sharp" path
        // Interpolate between known sample rates for smooth behavior
        // ------------------------------------------------------------
        if (sampleRate <= 11025) {
            aspBurstFc = kAspBurstFc_11k;
        } else if (sampleRate <= 16000) {
            double t = (double)(sampleRate - 11025) / (16000.0 - 11025.0);
            aspBurstFc = kAspBurstFc_11k + t * (kAspBurstFc_16k - kAspBurstFc_11k);
        } else if (sampleRate <= 22050) {
            double t = (double)(sampleRate - 16000) / (22050.0 - 16000.0);
            aspBurstFc = kAspBurstFc_16k + t * (kAspBurstFc_22k - kAspBurstFc_16k);
        } else if (sampleRate <= 44100) {
            double t = (double)(sampleRate - 22050) / (44100.0 - 22050.0);
            aspBurstFc = kAspBurstFc_22k + t * (kAspBurstFc_44k - kAspBurstFc_22k);
        } else {
            double scale = (double)sampleRate / 44100.0;
            aspBurstFc = kAspBurstFc_44k * scale;
        }
        aspLp1.setCutoffHz(aspBurstFc);
        aspLp2.setCutoffHz(aspBurstFc);
    }

    unsigned int generate(const unsigned int sampleCount, sample* sampleBuf) {
        if(!frameManager) return 0;
        for(unsigned int i=0;i<sampleCount;++i) {
            const speechPlayer_frameEx_t* frameEx = NULL;
            const speechPlayer_frame_t* frame=frameManager->getCurrentFrameWithEx(&frameEx);
            if(frame) {
                if(wasSilence) {
                    voiceGenerator.reset();
                    fricGenerator.reset();
                    cascade.reset();
                    parallel.reset();
                    lastInput=0.0;
                    lastOutput=0.0;
                    smoothPreGain=0.0;
                    smoothFricAmp=0.0;
                    // Reset adaptive frication/aspiration burst state on silence boundaries
                    lastTargetFricAmp=0.0;
                    lastTargetAspAmp=0.0;
                    burstEnv=0.0;
                    fricBurstLp1.reset(); fricBurstLp2.reset();
                    fricSustainLp1.reset(); fricSustainLp2.reset();
                    aspLp1.reset(); aspLp2.reset();
                    shelfMix = 1.0;
                    // Reset stop fade-out state
                    stopFadeTotal = 0;
                    stopFadeRemaining = 0;
                    // NOTE: Do NOT reset hsIn1/hsIn2/hsOut1/hsOut2 here!
                    wasSilence=false;
                }

                double targetPreGain = frame->preFormantGain;
                double alpha = (targetPreGain > smoothPreGain) ? preGainAttackAlpha : preGainReleaseAlpha;
                smoothPreGain += (targetPreGain - smoothPreGain) * alpha;

                double voice=voiceGenerator.getNext(frame, frameEx);

                // ------------------------------------------------------------
                // Split voice into voiced + aspiration for separate filtering
                // The aspiration path through cascade is often the real culprit
                // for "sharp" stop releases (not the frication path)
                // ------------------------------------------------------------
                double asp = voiceGenerator.getLastAspOut();
                double voicedOnly = voice - asp;

                // Smooth frication amplitude
                double targetFricAmp = frame->fricationAmplitude;
                double fricAlpha = (targetFricAmp > smoothFricAmp) ? fricAttackAlpha : fricReleaseAlpha;
                smoothFricAmp += (targetFricAmp - smoothFricAmp) * fricAlpha;

                double fricAmp = smoothFricAmp;

                if (kFricSoftClipK > 0.0) {
                    fricAmp = fricAmp * (1.0 - kFricSoftClipK * fricAmp);
                    if (fricAmp < 0.0) fricAmp = 0.0;
                }

                double bypass = frame->parallelBypass;
                if (bypass < 0.0) bypass = 0.0;
                if (bypass > 1.0) bypass = 1.0;
                double bypassGain = 1.0 - bypass * (1.0 - kBypassMinGain);

                double va = frame->voiceAmplitude;
                if (va < 0.0) va = 0.0;
                if (va > 1.0) va = 1.0;

                double bypassVoicedDuck = 1.0;
                if (bypass > 0.3 && va > 0.0) {
                    bypassVoicedDuck = 1.0 - kBypassVoicedDuck * va;
                }

                double voicedFricScale = 1.0;
                if (va > 0.0) {
                    voicedFricScale = 1.0 - kVoicedFricDuck * pow(va, kVoicedFricDuckPower);
                    if (voicedFricScale < 0.0) voicedFricScale = 0.0;
                }

                // ------------------------------------------------------------
                // Adaptive frication filtering (burst vs sustained)
                // ------------------------------------------------------------
                // Detect burst onset from RAW target (not smoothed!) so we catch
                // the actual transient. Using smoothFricAmp would make dFric tiny
                // per-sample and burstiness would stay near 0.
                
                // Frication burst
                double dFric = targetFricAmp - lastTargetFricAmp;
                lastTargetFricAmp = targetFricAmp;
                double instFric = 0.0;
                if (dFric > 0.0) {
                    // Scale by sample rate so behavior is similar across SRs
                    double srScale = (double)sampleRate / 22050.0;
                    instFric = dFric * kBurstinessScale * srScale;
                    if (instFric > 1.0) instFric = 1.0;
                }

                // Aspiration burst (frame param)
                // Stop releases often use aspirationAmplitude, not fricationAmplitude,
                // so we need to detect bursts from aspiration changes too.
                double dAsp = frame->aspirationAmplitude - lastTargetAspAmp;
                lastTargetAspAmp = frame->aspirationAmplitude;
                double instAsp = 0.0;
                if (dAsp > 0.0) {
                    // Separate scale; aspirationAmplitude often changes more subtly than fricationAmplitude
                    double srScale = (double)sampleRate / 22050.0;
                    instAsp = dAsp * 40.0 * srScale;   // higher scale since asp changes are subtler
                    if (instAsp > 1.0) instAsp = 1.0;
                }

                // Take the max of frication and aspiration burst
                double inst = instFric;
                if (instAsp > inst) inst = instAsp;

                // Prefer burst filtering when voicing is low (classic stop burst)
                // This helps /k/, /t/, /p/ bursts get more lowpass while leaving
                // voiced fricatives like /z/ or /v/ less affected
                inst *= (1.0 - va);

                // Hold/decay envelope: this is critical!
                // Without this, burstiness only fires for 1 sample (when frame changes),
                // which is inaudible. The envelope sustains it for ~6ms (where stop
                // releases actually live).
                burstEnv *= burstEnvDecayMul;
                if (inst > burstEnv) burstEnv = inst;
                double burstiness = burstEnv;

                // ------------------------------------------------------------
                // Filter aspiration noise using the same burst envelope
                // This is the key fix: aspiration through the cascade is often
                // the real source of "sharp" stop releases
                // ------------------------------------------------------------
                double aspFilt = aspLp2.process(aspLp1.process(asp));
                // Crossfade: during bursts, use filtered aspiration; otherwise use original
                asp = asp + burstiness * (aspFilt - asp);
                double voiceForCascade = voicedOnly + asp;

                double cascadeOut = cascade.getNext(frame, voiceGenerator.glottisOpen, voiceForCascade * smoothPreGain);

                // Generate raw frication noise
                double fricNoise = fricGenerator.getNext() * kFricNoiseScale * fricAmp * bypassGain * bypassVoicedDuck * voicedFricScale;

                // Optional Klatt-style glottal-cycle AM for noise sources.
                // This is driven by the voiced pitch cycle and will be 1.0
                // when disabled/unvoiced.
                fricNoise *= voiceGenerator.getLastNoiseMod();

                // Apply adaptive lowpass filtering:
                // - Burst path (2-pole cascade at lower cutoff): removes harsh highs from stops
                // - Sustain path (2-pole cascade at higher cutoff): preserves sibilant crispness
                double fricBurst = fricBurstLp2.process(fricBurstLp1.process(fricNoise));
                double fricSustain = fricSustainLp2.process(fricSustainLp1.process(fricNoise));

                // Crossfade based on burstiness: 
                // burstiness=1 -> use burst (darker), burstiness=0 -> use sustain (brighter)
                double fric = fricSustain + burstiness * (fricBurst - fricSustain);

                double parallelOut=parallel.getNext(frame,voiceGenerator.glottisOpen,fric*smoothPreGain);
                double out=(cascadeOut+parallelOut)*frame->outputGain;

                // DC blocking
                double filteredOut=out-lastInput+0.9995*lastOutput;
                lastInput=out;
                lastOutput=filteredOut;

                // Apply high-shelf EQ with burst-aware ducking
                // Full shelf output (what we already had)
                double shelved = applyHighShelf(filteredOut);

                // Duck the shelf only during bursts.
                // burstEnv is already 0..1, and holds ~6ms.
                // kShelfDuckMax: 0.0 = no change, 0.7 means at burstEnv=1 you keep 30% of the shelf.
                const double kShelfDuckMax = 0.90;

                // Also respect voicing amount so vowels keep full shelf.
                // This keeps shelf strong when va is high even if burstEnv flickers.
                double vaGate = va;              // va already clamped 0..1 above
                double burstGate = burstEnv;     // 0..1

                // Target mix: mostly 1.0, dips during bursts when voicing is low.
                double targetShelfMix = 1.0 - kShelfDuckMax * burstGate * (1.0 - vaGate);

                // Smooth it to avoid clicks
                shelfMix += (targetShelfMix - shelfMix) * shelfMixAlpha;

                // Crossfade between unshelved and shelved
                double bright = filteredOut + shelfMix * (shelved - filteredOut);

                // Store for fade-out tail on interrupt
                lastBrightOut = bright;

                double scaled = bright * 6000.0;
                const double limit = 32767.0;
                if(scaled > limit) scaled = limit;
                if(scaled < -limit) scaled = -limit;
                sampleBuf[i].value = (int)scaled;
            } else {
                // No frame available - handle stop/interrupt with fade-out to avoid click
                
                // If we were mid-speech, fade out for a few ms to avoid click on interrupt
                if (!wasSilence) {
                    if (stopFadeTotal == 0) {
                        stopFadeTotal = (int)(sampleRate * 0.004); // 4 ms
                        if (stopFadeTotal < 16) stopFadeTotal = 16;
                        stopFadeRemaining = stopFadeTotal;
                    }
                    if (stopFadeRemaining > 0) {
                        double t = (double)stopFadeRemaining / (double)stopFadeTotal; // 1..0
                        double tail = lastBrightOut * t;
                        double scaled = tail * 6000.0;
                        const double limit = 32767.0;
                        if (scaled > limit) scaled = limit;
                        if (scaled < -limit) scaled = -limit;
                        sampleBuf[i].value = (int)scaled;
                        stopFadeRemaining--;
                        // Keep going; we might fill the rest of the buffer with the tail
                        continue;
                    }
                }
                // Fade finished (or we were already silent)
                wasSilence = true;
                stopFadeTotal = 0;
                stopFadeRemaining = 0;
                return i;
            }
        }
        return sampleCount;
    }

    void setFrameManager(FrameManager* frameManager) {
        this->frameManager=frameManager;
    }

    void setVoicingTone(const speechPlayer_voicingTone_t* tone) {
        // Legacy ABI: the original voicingTone struct was just 7 doubles.
        // We still accept that layout when the magic header doesn't match.
        struct speechPlayer_voicingTone_v1_t {
            double voicingPeakPos;
            double voicedPreEmphA;
            double voicedPreEmphMix;
            double highShelfGainDb;
            double highShelfFcHz;
            double highShelfQ;
            double voicedTiltDbPerOct;
        };

        speechPlayer_voicingTone_t merged = speechPlayer_getDefaultVoicingTone();

        if (tone) {
            // Accept v2+ structs: check magic and that structSize at least covers the header.
            // This allows future v3/v4 structs (with appended fields) to work without
            // falling back to the legacy 7-double path.
            const bool looksLikeHeader = (tone->magic == SPEECHPLAYER_VOICINGTONE_MAGIC &&
                                          tone->structSize >= sizeof(uint32_t) * 4);

            if (looksLikeHeader) {
                // Caller is providing the v2 struct layout.
                size_t copySize = (size_t)tone->structSize;

                // If the caller forgot to set structSize, but did set the header,
                // assume they're passing the full struct.
                if (copySize < sizeof(uint32_t) * 4) copySize = sizeof(speechPlayer_voicingTone_t);

                // Basic sanity clamps in case a mismatched caller passes garbage.
                if (copySize > sizeof(speechPlayer_voicingTone_t)) copySize = sizeof(speechPlayer_voicingTone_t);

                // Start from defaults so missing tail fields get sane values.
                memcpy(&merged, tone, copySize);
            } else {
                // Legacy (v1) layout: 7 doubles, no header.
                const speechPlayer_voicingTone_v1_t* v1 = (const speechPlayer_voicingTone_v1_t*)tone;
                merged.voicingPeakPos = v1->voicingPeakPos;
                merged.voicedPreEmphA = v1->voicedPreEmphA;
                merged.voicedPreEmphMix = v1->voicedPreEmphMix;
                merged.highShelfGainDb = v1->highShelfGainDb;
                merged.highShelfFcHz = v1->highShelfFcHz;
                merged.highShelfQ = v1->highShelfQ;
                merged.voicedTiltDbPerOct = v1->voicedTiltDbPerOct;
                // newer fields stay at default (0.0)
            }
        }

        // Always normalize the header values to what this build implements.
        merged.magic = SPEECHPLAYER_VOICINGTONE_MAGIC;
        merged.structSize = (uint32_t)sizeof(speechPlayer_voicingTone_t);
        merged.structVersion = SPEECHPLAYER_VOICINGTONE_VERSION;
        merged.dspVersion = SPEECHPLAYER_DSP_VERSION;

        currentTone = merged;

        voiceGenerator.setVoicingParams(
            currentTone.voicingPeakPos,
            currentTone.voicedPreEmphA,
            currentTone.voicedPreEmphMix,
            currentTone.voicedTiltDbPerOct,
            currentTone.noiseGlottalModDepth,
            currentTone.speedQuotient
        );

        // Update high-shelf coefficients (do not reset state)
        initHighShelf(currentTone.highShelfFcHz, currentTone.highShelfGainDb, currentTone.highShelfQ);
        
        // Update pitch-sync F1 modulation params
        cascade.setPitchSyncParams(currentTone.pitchSyncF1DeltaHz, currentTone.pitchSyncB1DeltaHz);
    }

    void getVoicingTone(speechPlayer_voicingTone_t* tone) {
        if (!tone) return;

        // Legacy ABI: if the caller didn't set magic, assume they're using
        // the old 7-double layout and only write those fields.
        struct speechPlayer_voicingTone_v1_t {
            double voicingPeakPos;
            double voicedPreEmphA;
            double voicedPreEmphMix;
            double highShelfGainDb;
            double highShelfFcHz;
            double highShelfQ;
            double voicedTiltDbPerOct;
        };

        // Accept v2+ callers: check magic and that structSize at least covers the header
        const bool callerWantsHeader = (tone->magic == SPEECHPLAYER_VOICINGTONE_MAGIC &&
                                        tone->structSize >= sizeof(uint32_t) * 4);

        if (!callerWantsHeader) {
            speechPlayer_voicingTone_v1_t* v1 = (speechPlayer_voicingTone_v1_t*)tone;
            v1->voicingPeakPos = currentTone.voicingPeakPos;
            v1->voicedPreEmphA = currentTone.voicedPreEmphA;
            v1->voicedPreEmphMix = currentTone.voicedPreEmphMix;
            v1->highShelfGainDb = currentTone.highShelfGainDb;
            v1->highShelfFcHz = currentTone.highShelfFcHz;
            v1->highShelfQ = currentTone.highShelfQ;
            v1->voicedTiltDbPerOct = currentTone.voicedTiltDbPerOct;
            return;
        }

        // V2/V3: respect the caller-provided buffer size.
        speechPlayer_voicingTone_t tmp = currentTone;
        tmp.magic = SPEECHPLAYER_VOICINGTONE_MAGIC;
        tmp.structSize = (uint32_t)sizeof(speechPlayer_voicingTone_t);
        tmp.structVersion = SPEECHPLAYER_VOICINGTONE_VERSION;
        tmp.dspVersion = SPEECHPLAYER_DSP_VERSION;

        size_t writeSize = sizeof(speechPlayer_voicingTone_t);
        if (tone->structSize >= sizeof(uint32_t) * 4 && tone->structSize < writeSize) {
            writeSize = (size_t)tone->structSize;
        }
        memcpy(tone, &tmp, writeSize);
    }
};

SpeechWaveGenerator* SpeechWaveGenerator::create(int sampleRate) {return new SpeechWaveGeneratorImpl(sampleRate); }
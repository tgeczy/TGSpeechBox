/*
TGSpeechBox — LF glottal source with tilt, breathiness, and tremor.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#ifndef TGSPEECHBOX_VOICEGENERATOR_H
#define TGSPEECHBOX_VOICEGENERATOR_H

#include "dspCommon.h"
#include "pitchModel.h"
#include "frame.h"
#include "voicingTone.h"

class VoiceGenerator {
private:
    int sampleRate;
    FrequencyGenerator pitchGen;
    FrequencyGenerator vibratoGen;
    FrequencyGenerator tremorGen;  // Slow LFO for elderly/shaky voice (~5.5 Hz)
    NoiseGenerator aspirationGen;

    // Optional Fujisaki-Bartman pitch contour model (DSP v6+)
    FujisakiBartmanPitch fujisakiPitch;
    bool fujisakiWasEnabled;
    double lastFujisakiReset;
    double lastFujisakiPhraseAmp;
    double lastFujisakiAccentAmp;
    double lastFlow;
    double lastVoicedIn;
    double lastVoicedOut;
    double lastVoicedSrc;
    double lastAspOut;  // for exposing aspiration to caller

    // Optional noise AM on the glottal cycle (aspiration + frication).
    double noiseGlottalModDepth;
    double lastNoiseMod;

    // Tremor: slow amplitude modulation for shaky/elderly voice
    double tremorDepth;
    double tremorDepthSmooth;  // Smoothed to prevent clicks on slider change
    double lastTremorSin;      // Stored sin value for both pitch and amp modulation

    // Smooth aspiration gain to avoid clicks when aspirationAmplitude changes quickly.
    double smoothAspAmp;
    bool smoothAspAmpInit;
    double aspAttackCoeff;
    double aspReleaseCoeff;

    // Voiced anti-alias lowpass: prevents harmonic energy near Nyquist from
    // exciting the resonator bank into BLT-warped ringing.  2-pole (12 dB/oct),
    // sample-rate-dependent cutoff.  Bypassed at 44100+ Hz where warping is negligible.
    OnePoleLowpass voicedAntiAliasLp1;
    OnePoleLowpass voicedAntiAliasLp2;
    bool voicedAntiAliasActive;  // false at high SRs where it's not needed

    // Per-frame voice-quality modulation (DSP v5+)
    double lastCyclePos;
    double jitterMul;
    double shimmerMul;
    FastRandom jitterShimmerRng;  // dedicated PRNG for jitter/shimmer

    double voicingPeakPos;
    double voicedPreEmphA;
    double voicedPreEmphMix;

    // Speed quotient: glottal pulse asymmetry (V3 voicingTone)
    double speedQuotient;

    // Spectral tilt (Bipolar) for voiced signal
    double tiltTargetTlDb;
    double tiltTlDb;

    double tiltPole;
    double tiltPoleTarget;
    double tiltState;

    double tiltTlAlpha;
    double tiltPoleAlpha;

    double tiltRefHz;
    double tiltLastTlForTargets;

    // Per-frame tilt offset from breathiness (stacks with global tilt)
    double perFrameTiltOffset;        // current smoothed value
    double perFrameTiltOffsetTarget;  // target from current frame's breathiness
    double perFrameTiltOffsetAlpha;   // smoothing coefficient

    // Aspiration/frication tilt (LP/HP crossfade for noise color)
    double aspTiltTargetDb;      // target from slider
    double aspTiltSmoothedDb;    // smoothed value (prevents clicks)
    double aspTiltSmoothAlpha;   // smoothing coefficient
    double aspLpState;           // lowpass state for aspiration tilt filter
    double fricLpState;          // lowpass state for frication tilt (same tilt value)
    
    // Per-frame aspiration tilt offset from breathiness (makes noise softer too)
    double perFrameAspTiltOffset;
    double perFrameAspTiltOffsetTarget;
    double perFrameAspTiltOffsetAlpha;

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

        // RADIATION MIX — models lip radiation (+6 dB/oct differentiator).
        //
        // Additive mode: the derivative is ADDED to flow, not crossfaded.
        // This preserves all bass warmth while layering in upper presence.
        // Baseline at tilt=0: flow + 0.30*derivative (at 16 kHz+).
        //
        // Negative tilt (brightening): ramp boost toward 1.0.
        // Full derivative adds ~+6 dB/oct on top of flow — bright AND warm.
        //
        // Positive tilt (darkening): fade boost toward zero.
        // Pure flow at -12 dB/oct — very dark, no presence.

        // Scale mix by sample rate: at low SR, fewer harmonics exist above F2,
        // so derivative energy crowds near Nyquist and sounds swirly/airy.
        // 16 kHz+ gets full 0.30.  11025 Hz gets ~0.21 (close to old crossfade).
        const double kBaseRadiationMixMax = 0.30;
        const double kRadiationMixSrRef = 16000.0;
        const double kBaseRadiationMix = kBaseRadiationMixMax
            * std::min(1.0, (double)sampleRate / kRadiationMixSrRef);

        if (tl < 0.0) {
            // Brighten: ramp boost from baseline to 1.0 over 10 dB.
            // With additive mode, this adds presence WITHOUT subtracting warmth.
            // At tilt=-10: flow + 1.0*deriv (very bright, still warm).
            double bright = -tl / 10.0;  // 0..1 over -10..-20 dB range
            radiationMix = clampDouble(
                kBaseRadiationMix + bright * (1.0 - kBaseRadiationMix),
                kBaseRadiationMix, 1.0);
        } else {
            // Darken: fade boost to 0 over 12 dB.
            // At tilt=+12: pure flow, no derivative = very dark.
            radiationMix = clampDouble(
                kBaseRadiationMix * (1.0 - tl / 12.0),
                0.0, kBaseRadiationMix);
        }
    }

    double applyTilt(double in) {
        // Smooth the per-frame tilt offset (prevents clicks when breathiness changes)
        perFrameTiltOffset += (perFrameTiltOffsetTarget - perFrameTiltOffset) * perFrameTiltOffsetAlpha;
        
        // Effective tilt = global (speaker identity) + per-frame offset (phonation state)
        double effectiveTilt = tiltTargetTlDb + perFrameTiltOffset;
        
        tiltTlDb += (effectiveTilt - tiltTlDb) * tiltTlAlpha;
        if (fabs(tiltTlDb - tiltLastTlForTargets) > 0.01) {
            updateTiltTargets(tiltTlDb);
            tiltLastTlForTargets = tiltTlDb;
        }
        tiltPole += (tiltPoleTarget - tiltPole) * tiltPoleAlpha;
        double out = (1.0 - tiltPole) * in + tiltPole * tiltState;
        tiltState = out;
        return out;
    }

    // Helper: compute one-pole lowpass alpha from cutoff frequency
    double onePoleAlphaFromFc(double fcHz) const {
        double fc = fcHz;
        double nyq = 0.5 * (double)sampleRate;
        if (fc < 20.0) fc = 20.0;
        if (fc > nyq * 0.95) fc = nyq * 0.95;
        return exp(-PITWO * fc / (double)sampleRate);
    }

    // Aspiration/frication tilt: LP/HP crossfade for noise color
    // Negative = darker, Positive = brighter
    // Uses smoothing to prevent clicks on parameter changes
    void setAspirationTiltDbPerOct(double tiltDb) {
        aspTiltTargetDb = clampDouble(tiltDb, -24.0, 24.0);
    }

    double applyAspirationTilt(double x) {
        // Smooth the per-frame aspiration tilt offset (from breathiness)
        perFrameAspTiltOffset += (perFrameAspTiltOffsetTarget - perFrameAspTiltOffset) * perFrameAspTiltOffsetAlpha;
        
        // Smooth the global tilt parameter (prevents clicks from instant slider changes)
        aspTiltSmoothedDb += (aspTiltTargetDb - aspTiltSmoothedDb) * aspTiltSmoothAlpha;
        
        // Effective tilt = global (speaker setting) + per-frame (breathiness)
        double t = aspTiltSmoothedDb + perFrameAspTiltOffset;

        // Effect amount 0..1, with perceptual curve
        double amt = clampDouble(fabs(t) / 18.0, 0.0, 1.0);
        amt = pow(amt, 0.65);

        // Cutoff based on magnitude only (continuous at t=0, no jump)
        double fc = 6000.0 - 4500.0 * amt;  // 6k -> 1.5k as amt rises
        double a = onePoleAlphaFromFc(fc);

        // Always update filter state (prevents state freeze clicks)
        aspLpState = (1.0 - a) * x + a * aspLpState;
        double lp = aspLpState;
        double hp = x - lp;

        // Branchless: darken subtracts hp, brighten adds hp
        double brightAmt = (t > 0.0) ? amt : 0.0;
        double darkAmt   = (t < 0.0) ? amt : 0.0;
        const double kBright = 1.25;
        return x + hp * (kBright * brightAmt - darkAmt);
    }

public:
    bool glottisOpen;

    // Frication tilt: same algorithm, separate state, shares smoothed tilt value
    double applyFricationTilt(double x) {
        // Use the already-smoothed tilt value from aspiration
        double t = aspTiltSmoothedDb;

        double amt = clampDouble(fabs(t) / 18.0, 0.0, 1.0);
        amt = pow(amt, 0.65);

        double fc = 6000.0 - 4500.0 * amt;
        double a = onePoleAlphaFromFc(fc);

        // Always update filter state
        fricLpState = (1.0 - a) * x + a * fricLpState;
        double lp = fricLpState;
        double hp = x - lp;

        double brightAmt = (t > 0.0) ? amt : 0.0;
        double darkAmt   = (t < 0.0) ? amt : 0.0;
        const double kBright = 1.25;
        return x + hp * (kBright * brightAmt - darkAmt);
    }

    VoiceGenerator(int sr): sampleRate(sr), pitchGen(sr), vibratoGen(sr), tremorGen(sr), aspirationGen(),
        fujisakiPitch(sr), fujisakiWasEnabled(false),
        lastFujisakiReset(0.0), lastFujisakiPhraseAmp(0.0), lastFujisakiAccentAmp(0.0),
        lastFlow(0.0), lastVoicedIn(0.0), lastVoicedOut(0.0), lastVoicedSrc(0.0), lastAspOut(0.0),
        noiseGlottalModDepth(0.0), lastNoiseMod(1.0),
        tremorDepth(0.0), tremorDepthSmooth(0.0), lastTremorSin(0.0),
        smoothAspAmp(0.0), smoothAspAmpInit(false),
        aspAttackCoeff(0.0), aspReleaseCoeff(0.0),
        voicedAntiAliasLp1(sr), voicedAntiAliasLp2(sr), voicedAntiAliasActive(false),
        lastCyclePos(0.0), jitterMul(1.0), shimmerMul(1.0), jitterShimmerRng(98765),
        glottisOpen(false),
        voicingPeakPos(0.91), voicedPreEmphA(0.92), voicedPreEmphMix(0.35),
        tiltTargetTlDb(0.0), tiltTlDb(0.0),
        tiltPole(0.0), tiltPoleTarget(0.0), tiltState(0.0),
        tiltTlAlpha(0.0), tiltPoleAlpha(0.0),
        tiltRefHz(3000.0),
        tiltLastTlForTargets(1e9),
        perFrameTiltOffset(0.0), perFrameTiltOffsetTarget(0.0), perFrameTiltOffsetAlpha(0.0),
        aspTiltTargetDb(0.0), aspTiltSmoothedDb(0.0), aspTiltSmoothAlpha(0.0),
        aspLpState(0.0), fricLpState(0.0),
        perFrameAspTiltOffset(0.0), perFrameAspTiltOffsetTarget(0.0), perFrameAspTiltOffsetAlpha(0.0),
        radiationDerivGain(1.0),
        radiationMix(0.0) {

        const double tlSmoothMs = 8.0;
        const double poleSmoothMs = 5.0;

        tiltTlAlpha = 1.0 - exp(-1.0 / (sampleRate * (tlSmoothMs * 0.001)));
        tiltPoleAlpha = 1.0 - exp(-1.0 / (sampleRate * (poleSmoothMs * 0.001)));
        
        // Per-frame tilt offset smoothing (for breathiness on both voice and aspiration)
        perFrameTiltOffsetAlpha = 1.0 - exp(-1.0 / (sampleRate * (kBreathinessTiltSmoothMs * 0.001)));
        perFrameAspTiltOffsetAlpha = 1.0 - exp(-1.0 / (sampleRate * (kBreathinessTiltSmoothMs * 0.001)));

        // Aspiration tilt smoothing (10ms removes clicks without feeling laggy)
        const double aspTiltSmoothMs = 10.0;
        aspTiltSmoothAlpha = 1.0 - exp(-1.0 / (sampleRate * (aspTiltSmoothMs * 0.001)));

        // Aspiration gain smoothing (attack/release in ms).
        // This helps avoid random clicks when aspirationAmplitude changes quickly.
        const double kAspAmpAttackMs = 1.0;
        const double kAspAmpReleaseMs = 12.0;
        aspAttackCoeff = 1.0 - exp(-1.0 / (0.001 * kAspAmpAttackMs * sampleRate));
        aspReleaseCoeff = 1.0 - exp(-1.0 / (0.001 * kAspAmpReleaseMs * sampleRate));

        // Voiced anti-alias lowpass: sample-rate-dependent cutoff.
        // Prevents harmonic energy near Nyquist from exciting resonators
        // into BLT-warped ringing (trapezoidal SVF has same warping as BLT).
        // At 44100+ Hz the warping is negligible, so we bypass entirely.
        if (sampleRate < 44100) {
            voicedAntiAliasActive = true;
            double aaFc;
            if (sampleRate <= 11025) {
                aaFc = 4000.0;       // aggressive — Nyquist is only 5512
            } else if (sampleRate <= 16000) {
                double t = (double)(sampleRate - 11025) / (16000.0 - 11025.0);
                aaFc = 4000.0 + t * 1000.0;   // 4000 -> 5000
            } else {
                double t = (double)(sampleRate - 16000) / (22050.0 - 16000.0);
                aaFc = 5000.0 + t * 1500.0;   // 5000 -> 6500
                if (t > 1.0) aaFc = 6500.0;
            }
            voicedAntiAliasLp1.setCutoffHz(aaFc);
            voicedAntiAliasLp2.setCutoffHz(aaFc);
        } else {
            voicedAntiAliasActive = false;
        }

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
        setAspirationTiltDbPerOct(defaults.aspirationTiltDbPerOct);

        tiltTlDb = tiltTargetTlDb;
        updateTiltTargets(tiltTlDb);
        tiltPole = tiltPoleTarget;
        tiltLastTlForTargets = tiltTlDb;
    }

    void reset() {
        pitchGen.reset();
        vibratoGen.reset();
        aspirationGen.reset();

        // Reset Fujisaki pitch model state so new utterances start clean.
        fujisakiPitch.resetPast();
        fujisakiWasEnabled = false;
        lastFujisakiReset = 0.0;
        lastFujisakiPhraseAmp = 0.0;
        lastFujisakiAccentAmp = 0.0;

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
        aspLpState = 0.0;
        fricLpState = 0.0;
        voicedAntiAliasLp1.reset();
        voicedAntiAliasLp2.reset();
        aspTiltSmoothedDb = aspTiltTargetDb;  // Snap to target on reset
        tiltState = 0.0;  // Reset voiced tilt IIR state to prevent transient
        perFrameTiltOffset = 0.0;
        perFrameTiltOffsetTarget = 0.0;
        perFrameAspTiltOffset = 0.0;
        perFrameAspTiltOffsetTarget = 0.0;
    }

    void setTiltDbPerOct(double tiltVal) {
        tiltTargetTlDb = clampDouble(tiltVal, -24.0, 24.0);
    }

    void setVoicingParams(double peakPos, double preEmphA, double preEmphMix, double tiltDb, double noiseModDepth, double sq, double aspTiltDb) {
        voicingPeakPos = peakPos;
        voicedPreEmphA = preEmphA;
        voicedPreEmphMix = preEmphMix;
        noiseGlottalModDepth = clampDouble(noiseModDepth, 0.0, 1.0);
        speedQuotient = clampDouble(sq, 0.5, 4.0);
        setTiltDbPerOct(tiltDb);
        setAspirationTiltDbPerOct(aspTiltDb);
    }

    void getVoicingParams(double* peakPos, double* preEmphA, double* preEmphMix, double* tiltDb, double* noiseModDepth, double* sq, double* aspTiltDb) const {
        if (peakPos) *peakPos = voicingPeakPos;
        if (preEmphA) *preEmphA = voicedPreEmphA;
        if (preEmphMix) *preEmphMix = voicedPreEmphMix;
        if (tiltDb) *tiltDb = tiltTargetTlDb;
        if (noiseModDepth) *noiseModDepth = noiseGlottalModDepth;
        if (sq) *sq = speedQuotient;
        if (aspTiltDb) *aspTiltDb = aspTiltTargetDb;
    }

    void setSpeedQuotient(double sq) {
        speedQuotient = clampDouble(sq, 0.5, 4.0);
    }

    double getSpeedQuotient() const {
        return speedQuotient;
    }

    void setTremorDepth(double depth) {
        tremorDepth = clampDouble(depth, 0.0, 0.5);
    }

    double getTremorDepth() const {
        return tremorDepth;
    }

    double getLastNoiseMod() const { return lastNoiseMod; }

    double getNext(const speechPlayer_frame_t* frame, const speechPlayer_frameEx_t* frameEx) {
        // Optional per-frame voice quality (DSP v5+). If frameEx is NULL, all effects are disabled.
        double creakiness = 0.0;
        double breathiness = 0.0;
        double jitter = 0.0;
        double shimmer = 0.0;
        double frameExSharpness = 0.0;  // 0 = use SR default, >0 = override
        if (frameEx) {
            creakiness = frameEx->creakiness;
            breathiness = frameEx->breathiness;
            jitter = frameEx->jitter;
            shimmer = frameEx->shimmer;
            frameExSharpness = frameEx->sharpness;

            if (!std::isfinite(creakiness)) creakiness = 0.0;
            if (!std::isfinite(breathiness)) breathiness = 0.0;
            if (!std::isfinite(jitter)) jitter = 0.0;
            if (!std::isfinite(shimmer)) shimmer = 0.0;
            if (!std::isfinite(frameExSharpness)) frameExSharpness = 0.0;

            creakiness = clampDouble(creakiness, 0.0, 1.0);
            breathiness = clampDouble(breathiness, 0.0, 1.0);
            jitter = clampDouble(jitter, 0.0, 1.0);
            shimmer = clampDouble(shimmer, 0.0, 1.0);
            frameExSharpness = clampDouble(frameExSharpness, 0.0, 15.0);  // Allow up to 15 for extreme effects
            
            // Perceptual curve for breathiness: makes 0.2–0.6 slider range useful
            if (breathiness > 0.0) {
                breathiness = pow(breathiness, 0.55);
            }
            
            // Breathiness drives per-frame tilt offset (softer highs = airy quality)
            // VOICED: Positive tilt = darker/softer. breathiness 1.0 -> +6 dB/oct darker
            perFrameTiltOffsetTarget = breathiness * kBreathinessTiltMaxDb;
            
            // ASPIRATION/NOISE: Negative tilt = darker. breathiness 1.0 -> -8 dB/oct darker
            // This makes the breath noise spectrally match the softened voice
            perFrameAspTiltOffsetTarget = breathiness * kBreathinessAspTiltMaxDb;
        } else {
            // No frameEx: reset tilt offsets to zero
            perFrameTiltOffsetTarget = 0.0;
            perFrameAspTiltOffsetTarget = 0.0;
        }

        // ------------------------------------------------------------
        // Pitch (F0)
        // ------------------------------------------------------------
        // Base pitch comes from the frame (and can still be linearly ramped via
        // endVoicePitch in FrameManager). Optionally, we can modulate that base
        // pitch with the Fujisaki-Bartman pitch contour model.

        double basePitchHz = frame->voicePitch;
        if (!std::isfinite(basePitchHz) || basePitchHz < 0.0) basePitchHz = 0.0;

        // Fujisaki-Bartman pitch contour (optional)
        double pitchContourMul = 1.0;
        bool useFujisaki = false;
        if (frameEx) {
            double en = frameEx->fujisakiEnabled;
            if (std::isfinite(en) && en > 0.5) {
                useFujisaki = true;
            }
        }

        if (useFujisaki) {
            // Reset model state on rising edge.
            double resetVal = frameEx ? frameEx->fujisakiReset : 0.0;
            if (!std::isfinite(resetVal)) resetVal = 0.0;
            if (resetVal > 0.5 && lastFujisakiReset <= 0.5) {
                fujisakiPitch.resetPast();
                lastFujisakiPhraseAmp = 0.0;
                lastFujisakiAccentAmp = 0.0;
            }
            lastFujisakiReset = resetVal;

            // Phrase trigger: rising edge of fujisakiPhraseAmp.
            double phraseAmp = frameEx ? frameEx->fujisakiPhraseAmp : 0.0;
            if (!std::isfinite(phraseAmp)) phraseAmp = 0.0;
            if (phraseAmp > 0.0 && lastFujisakiPhraseAmp <= 0.0) {
                double pl = frameEx ? frameEx->fujisakiPhraseLen : 0.0;
                if (!std::isfinite(pl)) pl = 0.0;
                int plSamples = (pl > 0.0) ? (int)floor(pl + 0.5) : 0;
                fujisakiPitch.phrase(phraseAmp, plSamples);
            }
            lastFujisakiPhraseAmp = phraseAmp;

            // Accent trigger: rising edge of fujisakiAccentAmp.
            double accentAmp = frameEx ? frameEx->fujisakiAccentAmp : 0.0;
            if (!std::isfinite(accentAmp)) accentAmp = 0.0;
            if (accentAmp > 0.0 && lastFujisakiAccentAmp <= 0.0) {
                double d = frameEx ? frameEx->fujisakiAccentDur : 0.0;
                double al = frameEx ? frameEx->fujisakiAccentLen : 0.0;
                if (!std::isfinite(d)) d = 0.0;
                if (!std::isfinite(al)) al = 0.0;
                int dSamples = (d > 0.0) ? (int)floor(d + 0.5) : 0;
                int alSamples = (al > 0.0) ? (int)floor(al + 0.5) : 0;
                fujisakiPitch.accent(accentAmp, dSamples, alSamples);
            }
            lastFujisakiAccentAmp = accentAmp;

            pitchContourMul = fujisakiPitch.processMultiplier();
            if (!std::isfinite(pitchContourMul) || pitchContourMul <= 0.0) {
                pitchContourMul = 1.0;
            }
            fujisakiWasEnabled = true;
        } else {
            // If the model was previously enabled and is now disabled, clear state so
            // the next enable starts from a clean history.
            if (fujisakiWasEnabled) {
                fujisakiPitch.resetPast();
                fujisakiWasEnabled = false;
                lastFujisakiReset = 0.0;
                lastFujisakiPhraseAmp = 0.0;
                lastFujisakiAccentAmp = 0.0;
            }
        }

        // Vibrato (fraction of a semitone)
        double vibrato=(sin(vibratoGen.getNext(frame->vibratoSpeed)*PITWO)*0.06*frame->vibratoPitchOffset)+1;

        // Tremor: modulation for elderly/shaky voice (4-12 Hz range).
        // Research shows tremor involves F0, amplitude, AND formant instability.
        // Use fast smoothing (only for slider changes, not the tremor itself!)
        const double tremorSmoothAlpha = 0.01;  // Fast: ~6ms at 16kHz (was 0.0002 = 300ms!)
        tremorDepthSmooth += (tremorDepth - tremorDepthSmooth) * tremorSmoothAlpha;
        double tremorPitchMod = 1.0;
        if (tremorDepthSmooth > 0.001) {
            // 5 Hz - slower so each wobble is distinct (research: 4-6 Hz typical)
            double tremorPhase = tremorGen.getNext(5.0);
            lastTremorSin = sin(tremorPhase * PITWO);
            // Add slight irregularity using the jitter RNG for organic feel
            double irregularity = 1.0 + (jitterShimmerRng.nextBipolar()) * 0.15 * tremorDepthSmooth;
            // Pitch tremor: ±35% F0 at full depth - MAXIMUM elderly voice shake!
            tremorPitchMod = 1.0 + (tremorDepthSmooth * 0.70 * lastTremorSin * irregularity);
        } else {
            lastTremorSin = 0.0;
        }

        double pitchHz = basePitchHz * pitchContourMul * vibrato * tremorPitchMod;
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
                double r = jitterShimmerRng.nextBipolar();
                jitterMul = 1.0 + (r * jitterRel);
                if (jitterMul < 0.2) jitterMul = 0.2;
            } else {
                jitterMul = 1.0;
            }

            double shimmerRel = (shimmer * 0.70) + (creakiness * 0.12);
            if (shimmerRel > 0.0) {
                double r = jitterShimmerRng.nextBipolar();
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

        // Aspiration noise: use WHITE noise (flat spectrum) so tilt filter can shape it
        // Base gain 0.1, breathiness lifts it up to 0.25
        double aspBase = 0.10 + (0.15 * breathiness);
        double aspiration = aspirationGen.white() * aspBase * noiseMod;
        
        // Apply tilt filter to aspiration (color the noise)
        aspiration = applyAspirationTilt(aspiration);

        double effectiveOQ = frame->glottalOpenQuotient;
        if (effectiveOQ <= 0.0) effectiveOQ = 0.4;
        if (effectiveOQ < 0.10) effectiveOQ = 0.10;
        if (effectiveOQ > 0.95) effectiveOQ = 0.95;

        // Tremor: modulate open quotient for "voice bending" quality change.
        // When vocal fold tension trembles, OQ oscillates between
        // slightly pressed (shorter open) and slightly breathy (longer open).
        // This creates the characteristic tremor "wobble in voice character".
        if (tremorDepthSmooth > 0.001) {
            // At full depth: ±0.15 OQ variation - strong voice quality wobble
            double oqTremorMod = tremorDepthSmooth * 0.30 * lastTremorSin;
            effectiveOQ += oqTremorMod;
            // Keep in valid range
            if (effectiveOQ < 0.10) effectiveOQ = 0.10;
            if (effectiveOQ > 0.95) effectiveOQ = 0.95;
        }

        // Creakiness: shorter open phase (more closed time) in this model.
        if (creakiness > 0.0) {
            effectiveOQ += 0.10 * creakiness;
            if (effectiveOQ > 0.95) effectiveOQ = 0.95;
        }
        
        // Breathiness: much longer open phase (glottis barely closes)
        // True breathy voice = glottis open 85-95% of cycle, not just 70%
        if (breathiness > 0.0) {
            // Push OQ down toward 0.05 at full breathiness
            // From 0.4 default: 0.4 - (0.35 * 1.0) = 0.05
            effectiveOQ -= 0.35 * breathiness;
            // Allow very low OQ for breathiness (nearly always open)
            if (effectiveOQ < 0.05) effectiveOQ = 0.05;
        }

        glottisOpen = (pitchHz > 0.0) && (cyclePos >= effectiveOQ);

        double flow = 0.0;
        if(glottisOpen) {
            double openLen = 1.0 - effectiveOQ;
            if (openLen < 0.0001) openLen = 0.0001;

            // Per-frame voice quality tweaks to pulse shape:
            // - breathiness nudges the peak later (softer/relaxed)
            // - creakiness nudges the peak earlier (tenser/pressed)
            // - speedQuotient shifts peak position (the real LF model effect!)
            //
            // In Fant's LF model, SQ determines where the flow peaks within
            // the open phase: peakPos = SQ / (1 + SQ).  Our voicingPeakPos
            // (default 0.91) was tuned as if SQ ≈ 10, so we treat SQ=2.0
            // as neutral (no shift) and map deviations to a peak delta:
            //
            //   SQ=0.5  → peak shifts ~-0.20 (more symmetric, softer, breathy)
            //   SQ=1.0  → peak shifts ~-0.10
            //   SQ=2.0  → peak shift  = 0.0  (default, backward compatible)
            //   SQ=3.0  → peak shifts ~+0.05
            //   SQ=4.0  → peak shifts ~+0.08
            //
            // The nonlinear mapping uses SQ/(1+SQ) so the effect is stronger
            // on the "softer" end (where it matters perceptually) and gentle
            // on the "pressed" end (where we're already near the limit).
            double sqPeakDelta = 0.0;
            if (speedQuotient != 2.0) {
                double refPeak = 2.0 / 3.0;                           // 0.6667
                double sqPeak = speedQuotient / (1.0 + speedQuotient); // LF model
                double rawDelta = sqPeak - refPeak;  // negative for SQ<2, positive for SQ>2
                // Scale 0.6: maps LF range into ~±0.20 around voicingPeakPos
                sqPeakDelta = rawDelta * 0.6;
            }
            double peakPos = voicingPeakPos + sqPeakDelta
                           + (0.02 * breathiness) - (0.05 * creakiness);

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
            // Speed quotient now acts in three ways:
            //   1. Peak position shift (above) — the dominant LF model effect
            //   2. Opening curve steepness (below) — secondary reinforcement
            //   3. Closing sharpness modulation (below) — secondary reinforcement
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
                
                // FrameEx sharpness is a MULTIPLIER (0.5 to 2.0), not absolute.
                // This keeps the slider SR-agnostic: "1.0" always means "default for this SR".
                // A value of 0 means "use default" (no FrameEx override).
                if (frameExSharpness > 0.0) {
                    baseSharpness *= frameExSharpness;
                    // Clamp to safe range: too low = no closure, too high = just harsh
                    baseSharpness = clampDouble(baseSharpness, 1.0, 15.0);
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


            // Scale LF mixing with user-facing glottal sharpness (frameExSharpness) while keeping
            // the neutral/default behavior unchanged.
            //
            // - frameExSharpness == 0.0: use the sample-rate default LF blend (backward compatible)
            // - frameExSharpness  < 1.0: smoother (less LF)
            // - frameExSharpness  > 1.0: sharper (more LF), capped per sample rate to avoid aliasy crunch
            const double lfBlendBase = lfBlend;

            const double sharpMul = (frameExSharpness > 0.0) ? frameExSharpness : 1.0;
            const double sharpClamped = clampDouble(sharpMul, 0.25, 3.0);
            const double lfScale = pow(sharpClamped, 0.25); // gentle curve: 0.5->~0.84, 2.0->~1.19

            double lfCap = 1.0;
            if (sampleRate <= 11025) lfCap = 0.35;
            else if (sampleRate < 16000) lfCap = 0.85;

            lfBlend = clampDouble(lfBlendBase * lfScale, 0.0, lfCap);


            flow = (1.0 - lfBlend) * flowCosine + lfBlend * flowLF;
        }

        const double flowScale = 1.6;
        flow *= flowScale;

        double dFlow = flow - lastFlow;
        lastFlow = flow;

        // ------------------------------------------------------------
        // Radiation Characteristic (Additive):
        // ------------------------------------------------------------
        // Real lip radiation adds +6 dB/oct to the source — it doesn't
        // subtract low frequencies.  The old crossfade replaced flow
        // energy with derivative energy, making brightness and warmth
        // a zero-sum game.  Additive mode keeps ALL the flow (warmth)
        // and layers derivative energy (presence) on top.
        //
        // radiationMix now controls how much derivative is ADDED:
        //   0.0  = pure flow (-12 dB/oct, very dark)
        //   0.3  = gentle presence (natural conversational speech)
        //   0.5  = clear presence (broadcast speech)
        //   1.0  = full derivative added (very bright, still warm)
        // The limiter catches any peaks from the summed signal.

        double srcDeriv = dFlow * radiationDerivGain;

        // Soft-limit the derivative to tame glottal closure transients.
        // Steady-state harmonics (small dFlow) pass through linearly — they
        // carry the +6 dB/oct spectral tilt we want for presence.
        // Closure spikes (large dFlow) get squashed by tanh — prevents
        // additive radiation from amplifying glottal sharpness.
        const double kDerivSaturation = 0.6;
        srcDeriv = kDerivSaturation * tanh(srcDeriv / kDerivSaturation);

        // Energy compensation: adding derivative increases total energy.
        // Scale down gently so negative tilt brightens without pumping volume.
        // At mix=0.15: divisor=1.075 (barely audible).
        // At mix=1.0:  divisor=1.5 (keeps full-bright from clipping).
        double voicedSrc = (flow + radiationMix * srcDeriv) / (1.0 + radiationMix * 0.5);

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
            // Moderate turbulence increase (glottal-gated noise is the key breathy component)
            // Reduced from 1.0 to 0.5 - we want "weak airy voice" not "noise drowning voice"
            voiceTurbAmp = clampDouble(voiceTurbAmp + (0.5 * breathiness), 0.0, 1.0);
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
            // TRUE breathy voice: the voiced component nearly disappears.
            // At full breathiness, only 15% of voice remains (85% reduction).
            // This makes turbulent noise the PRIMARY sound, not an additive layer.
            voiceAmp *= (1.0 - (0.98 * breathiness));
        }
        voiceAmp *= shimmerMul;

        // Tremor amplitude modulation - subtle, let pitch and OQ do the heavy lifting
        // The "shake" should come from voice quality changes, not volume pumping
        if (tremorDepthSmooth > 0.001) {
            // Reduced: ±25% amplitude at full depth (was ±60%)
            double ampIrregularity = 1.0 + (jitterShimmerRng.nextBipolar()) * 0.1 * tremorDepthSmooth;
            double tremorAmpMod = 1.0 + (tremorDepthSmooth * 0.5 * lastTremorSin * ampIrregularity);
            voiceAmp *= tremorAmpMod;
        }

        // CRITICAL: Apply voiceAmp ONLY to the voiced pulse, NOT to turbulence.
        // For breathiness: voice gets quiet while turbulence stays strong.
        // OLD: (voicedSrc + turbulence) * voiceAmp  <-- killed turbulence too!
        // NEW: (voicedSrc * voiceAmp) + turbulence  <-- turbulence independent
        double voicedIn = (voicedSrc * voiceAmp) + turbulence;
        const double dcPole = 0.9995;
        double voiced = voicedIn - lastVoicedIn + (dcPole * lastVoicedOut);
        lastVoicedIn = voicedIn;
        lastVoicedOut = voiced;

        // Anti-alias lowpass on voiced signal: attenuates harmonics near Nyquist
        // that would cause BLT warping artifacts in the resonator bank.
        // Applied after DC block, before combining with aspiration (noise doesn't alias).
        if (voicedAntiAliasActive) {
            voiced = voicedAntiAliasLp2.process(voicedAntiAliasLp1.process(voiced));
        }

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

#endif // TGSPEECHBOX_VOICEGENERATOR_H
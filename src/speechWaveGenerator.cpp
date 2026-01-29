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
const double kFricBurstFc_16k   = 5200.0;
const double kFricSustainFc_16k = 7200.0;
const double kFricBurstFc_22k   = 6500.0;
const double kFricSustainFc_22k = 9500.0;
const double kFricBurstFc_44k   = 9000.0;
const double kFricSustainFc_44k = 14000.0;

// Sample-rate-aware cutoff frequencies for aspiration burst LP
// More aggressive than frication since aspiration through cascade is often
// the real culprit for "sharp" stop releases
const double kAspBurstFc_16k = 3200.0;
const double kAspBurstFc_22k = 3800.0;
const double kAspBurstFc_44k = 4500.0;

// Burstiness detection sensitivity (higher = more sensitive to fast rises)
const double kBurstinessScale = 18.0;

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

    double voicingPeakPos;
    double voicedPreEmphA;
    double voicedPreEmphMix;

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
        lastFlow(0.0), lastVoicedIn(0.0), lastVoicedOut(0.0), lastVoicedSrc(0.0), lastAspOut(0.0), glottisOpen(false),
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

        double nyq = 0.5 * (double)sampleRate;
        if (tiltRefHz > nyq * 0.95) tiltRefHz = nyq * 0.95;
        if (tiltRefHz < 500.0) tiltRefHz = 500.0;

        radiationDerivGain = kRadiationDerivGainBase * ((double)sampleRate / kRadiationDerivGainRefSr);

        speechPlayer_voicingTone_t defaults = SPEECHPLAYER_VOICINGTONE_DEFAULTS;
        voicingPeakPos = defaults.voicingPeakPos;
        voicedPreEmphA = defaults.voicedPreEmphA;
        voicedPreEmphMix = defaults.voicedPreEmphMix;
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
        glottisOpen=false;
    }

    void setTiltDbPerOct(double tiltVal) {
        tiltTargetTlDb = clampDouble(tiltVal, -24.0, 24.0);
    }

    void setVoicingParams(double peakPos, double preEmphA, double preEmphMix, double tiltDb) {
        voicingPeakPos = peakPos;
        voicedPreEmphA = preEmphA;
        voicedPreEmphMix = preEmphMix;
        setTiltDbPerOct(tiltDb);
    }

    void getVoicingParams(double* peakPos, double* preEmphA, double* preEmphMix, double* tiltDb) const {
        if (peakPos) *peakPos = voicingPeakPos;
        if (preEmphA) *preEmphA = voicedPreEmphA;
        if (preEmphMix) *preEmphMix = voicedPreEmphMix;
        if (tiltDb) *tiltDb = tiltTargetTlDb;
    }

    double getNext(const speechPlayer_frame_t* frame) {
        double vibrato=(sin(vibratoGen.getNext(frame->vibratoSpeed)*PITWO)*0.06*frame->vibratoPitchOffset)+1;
        double pitchHz = frame->voicePitch * vibrato;
        double cyclePos = pitchGen.getNext(pitchHz > 0.0 ? pitchHz : 0.0);

        double aspiration=aspirationGen.getNext()*0.1;

        double effectiveOQ = frame->glottalOpenQuotient;
        if (effectiveOQ <= 0.0) effectiveOQ = 0.4;
        if (effectiveOQ < 0.10) effectiveOQ = 0.10;
        if (effectiveOQ > 0.95) effectiveOQ = 0.95;

        glottisOpen = (pitchHz > 0.0) && (cyclePos >= effectiveOQ);

        double flow = 0.0;
        if(glottisOpen) {
            double openLen = 1.0 - effectiveOQ;
            if (openLen < 0.0001) openLen = 0.0001;
            double peakPos = voicingPeakPos;
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

            if (phase < peakPos) {
                flow = 0.5 * (1.0 - cos(phase * M_PI / peakPos));
            } else {
                flow = 0.5 * (1.0 + cos((phase - peakPos) * M_PI / (1.0 - peakPos)));
            }
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

        double turbulence = aspiration * frame->voiceTurbulenceAmplitude;
        if(glottisOpen) {
            double flow01 = flow / flowScale;
            if(flow01 < 0.0) flow01 = 0.0;
            if(flow01 > 1.0) flow01 = 1.0;
            turbulence *= pow(flow01, kTurbulenceFlowPower);
        } else {
            turbulence = 0.0;
        }

        double voicedIn = (voicedSrc + turbulence) * frame->voiceAmplitude;
        const double dcPole = 0.9995;
        double voiced = voicedIn - lastVoicedIn + (dcPole * lastVoicedOut);
        lastVoicedIn = voicedIn;
        lastVoicedOut = voiced;

        double aspOut = aspiration * frame->aspirationAmplitude;
        lastAspOut = aspOut;
        return aspOut + voiced;
    }

    double getLastAspOut() const { return lastAspOut; }
};
// ... Resonator, Cascade, Parallel, SpeechWaveGeneratorImpl ...
// (These classes are unchanged from your last stable version)
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

            double effectiveBandwidth = bandwidth;

            double r=exp(-M_PI/sampleRate*effectiveBandwidth);
            c=-(r*r);
            b=r*cos(PITWO/sampleRate*-frequency)*2.0;
            a=1.0-b-c;
            if(anti&&frequency!=0) {
                a=1.0/a;
                c*=-a;
                b*=-a;
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

class CascadeFormantGenerator { 
private:
    int sampleRate;
    Resonator r1, r2, r3, r4, r5, r6, rN0, rNP;

public:
    CascadeFormantGenerator(int sr): sampleRate(sr), r1(sr), r2(sr), r3(sr), r4(sr), r5(sr), r6(sr), rN0(sr,true), rNP(sr) {}

    void reset() {
        r1.reset(); r2.reset(); r3.reset(); r4.reset(); r5.reset(); r6.reset(); rN0.reset(); rNP.reset();
    }

    double getNext(const speechPlayer_frame_t* frame, bool glottisOpen, double input) {
        input/=2.0;
        (void)glottisOpen;
        double n0Output=rN0.resonate(input,frame->cfN0,frame->cbN0);
        double output=calculateValueAtFadePosition(input,rNP.resonate(n0Output,frame->cfNP,frame->cbNP),frame->caNP);
        output=r6.resonate(output,frame->cf6,frame->cb6);
        output=r5.resonate(output,frame->cf5,frame->cb5);
        output=r4.resonate(output,frame->cf4,frame->cb4);
        output=r3.resonate(output,frame->cf3,frame->cb3);
        output=r2.resonate(output,frame->cf2,frame->cb2);
        output=r1.resonate(output,frame->cf1,frame->cb1);
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
        if (sampleRate <= 16000) {
            fricBurstFc = kFricBurstFc_16k;
            fricSustainFc = kFricSustainFc_16k;
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
        if (sampleRate <= 16000) {
            aspBurstFc = kAspBurstFc_16k;
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
            const speechPlayer_frame_t* frame=frameManager->getCurrentFrame();
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

                double voice=voiceGenerator.getNext(frame);

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
                const double kShelfDuckMax = 0.70;

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
        if (tone) {
            currentTone = *tone;
        } else {
            currentTone = speechPlayer_getDefaultVoicingTone();
        }

        voiceGenerator.setVoicingParams(
            currentTone.voicingPeakPos,
            currentTone.voicedPreEmphA,
            currentTone.voicedPreEmphMix,
            currentTone.voicedTiltDbPerOct
        );

        // Update high-shelf coefficients (do not reset state)
        initHighShelf(currentTone.highShelfFcHz, currentTone.highShelfGainDb, currentTone.highShelfQ);
    }

    void getVoicingTone(speechPlayer_voicingTone_t* tone) {
        if (tone) {
            *tone = currentTone;
        }
    }
};

SpeechWaveGenerator* SpeechWaveGenerator::create(int sampleRate) {return new SpeechWaveGeneratorImpl(sampleRate); }
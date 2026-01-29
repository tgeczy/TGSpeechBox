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
            double a = (-B + sqrtDisc) / denom;
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
        lastFlow(0.0), lastVoicedIn(0.0), lastVoicedOut(0.0), lastVoicedSrc(0.0), glottisOpen(false),
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
        return aspOut + voiced;
    }
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
    SpeechWaveGeneratorImpl(int sr): sampleRate(sr), voiceGenerator(sr), fricGenerator(), cascade(sr), parallel(sr), frameManager(NULL), lastInput(0.0), lastOutput(0.0), wasSilence(true), smoothPreGain(0.0), preGainAttackAlpha(0.0), preGainReleaseAlpha(0.0), smoothFricAmp(0.0), fricAttackAlpha(0.0), fricReleaseAlpha(0.0), hsIn1(0), hsIn2(0), hsOut1(0), hsOut2(0) {
        const double attackMs = 1.0;
        const double releaseMs = 0.5;
        preGainAttackAlpha = 1.0 - exp(-1.0 / (sampleRate * (attackMs * 0.001)));
        preGainReleaseAlpha = 1.0 - exp(-1.0 / (sampleRate * (releaseMs * 0.001)));

        // Frication smoothing (ms)
        const double fricAttackMs = 0.8;
        const double fricReleaseMs = 1.2;
        fricAttackAlpha = 1.0 - exp(-1.0 / (sampleRate * (fricAttackMs * 0.001)));
        fricReleaseAlpha = 1.0 - exp(-1.0 / (sampleRate * (fricReleaseMs * 0.001)));

        // Initialize with default voicing tone
        currentTone = speechPlayer_getDefaultVoicingTone();

        // High shelf: use defaults from voicing tone
        initHighShelf(currentTone.highShelfFcHz, currentTone.highShelfGainDb, currentTone.highShelfQ);
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
                    // NOTE: Do NOT reset hsIn1/hsIn2/hsOut1/hsOut2 here!
                    wasSilence=false;
                }

                double targetPreGain = frame->preFormantGain;
                double alpha = (targetPreGain > smoothPreGain) ? preGainAttackAlpha : preGainReleaseAlpha;
                smoothPreGain += (targetPreGain - smoothPreGain) * alpha;

                double voice=voiceGenerator.getNext(frame);

                double cascadeOut=cascade.getNext(frame,voiceGenerator.glottisOpen,voice*smoothPreGain);

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

                double fric=fricGenerator.getNext()*kFricNoiseScale*fricAmp*bypassGain*bypassVoicedDuck*voicedFricScale;
                double parallelOut=parallel.getNext(frame,voiceGenerator.glottisOpen,fric*smoothPreGain);
                double out=(cascadeOut+parallelOut)*frame->outputGain;

                // DC blocking
                double filteredOut=out-lastInput+0.9995*lastOutput;
                lastInput=out;
                lastOutput=filteredOut;

                // Apply high-shelf EQ
                double bright = applyHighShelf(filteredOut);

                double scaled = bright * 6000.0;
                const double limit = 32767.0;
                if(scaled > limit) scaled = limit;
                if(scaled < -limit) scaled = -limit;
                sampleBuf[i].value = (int)scaled;
            } else {
                wasSilence=true;
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
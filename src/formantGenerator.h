/*
TGSpeechBox — Cascade and parallel formant filter topologies.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#ifndef TGSPEECHBOX_FORMANTGENERATOR_H
#define TGSPEECHBOX_FORMANTGENERATOR_H

#include "dspCommon.h"
#include "resonator.h"
#include "frame.h"
#include "utils.h"

class CascadeFormantGenerator { 
private:
    int sampleRate;
    PitchSyncResonator r1;  // F1 gets pitch-sync treatment
    Resonator r2, r3, r4, r5, r6, rN0, rNP;
    
    // Pitch-sync params from voicingTone
    double pitchSyncF1Delta;
    double pitchSyncB1Delta;
    double bwScale;  // Global cascade bandwidth multiplier from voicingTone

public:
    CascadeFormantGenerator(int sr): sampleRate(sr), r1(sr), r2(sr), r3(sr), r4(sr), r5(sr), r6(sr), rN0(sr,true), rNP(sr),
        pitchSyncF1Delta(0.0), pitchSyncB1Delta(0.0), bwScale(1.0) {}

    void reset() {
        r1.reset(); r2.reset(); r3.reset(); r4.reset(); r5.reset(); r6.reset(); rN0.reset(); rNP.reset();
    }

    void decay(double factor) {
        r1.decay(factor); r2.decay(factor); r3.decay(factor);
        r4.decay(factor); r5.decay(factor); r6.decay(factor);
        rN0.decay(factor); rNP.decay(factor);
    }

    void setPitchSyncParams(double f1DeltaHz, double b1DeltaHz) {
        pitchSyncF1Delta = f1DeltaHz;
        pitchSyncB1Delta = b1DeltaHz;
        r1.setPitchSyncParams(f1DeltaHz, b1DeltaHz);
    }

void setCascadeBwScale(double scale) {
        // Clamp to safe range: too narrow risks instability, too wide loses vowel identity
        if (scale < 0.3) scale = 0.3;
        if (scale > 2.0) scale = 2.0;
        bwScale = scale;
    }
    double getNext(const speechPlayer_frame_t* frame, const speechPlayer_frameEx_t* frameEx, bool glottisOpen, double input) {
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

        // During within-phoneme formant sweeps (diphthongs), widen bandwidth as needed
        // to keep resonators from becoming ultra-high-Q as formants move upward.
        double cb1 = frame->cb1;
        double cb2 = frame->cb2;
        double cb3 = frame->cb3;
        if (frameEx) {
            if (std::isfinite(frameEx->endCf1)) cb1 = bandwidthForSweep(frame->cf1, cb1, kSweepQMaxF1, kSweepBwMinF1, kSweepBwMax);
            if (std::isfinite(frameEx->endCf2)) cb2 = bandwidthForSweep(frame->cf2, cb2, kSweepQMaxF2, kSweepBwMinF2, kSweepBwMax);
            if (std::isfinite(frameEx->endCf3)) cb3 = bandwidthForSweep(frame->cf3, cb3, kSweepQMaxF3, kSweepBwMinF3, kSweepBwMax);
        }

        // --- Global cascade bandwidth scaling ---
        // Multiplier < 1.0 = narrower bandwidths = sharper/ringy-er formant peaks (Eloquence-like)
        // Multiplier > 1.0 = wider bandwidths = softer/warmer blended formants (DECTalk-like)
        // This changes the fundamental resonance character of the entire instrument.
        const double cascadeBwScale = bwScale;
        cb1 *= cascadeBwScale;
        cb2 *= cascadeBwScale;
        cb3 *= cascadeBwScale;
        double cb4 = frame->cb4 * cascadeBwScale;
        double cb5 = frame->cb5 * cascadeBwScale;
        double cb6 = frame->cb6 * cascadeBwScale;
        // --- Nyquist-proximity fade for upper cascade formants ---
        // At low sample rates (e.g. 11025 Hz, Nyquist = 5512 Hz), the cascade
        // resonators for F5/F6 sit close to Nyquist and amplify harmonic energy
        // by 12-21 dB at the folding frequency.  Because voiced sounds are
        // periodic, this aliased energy creates audible beating ("swirly" /
        // "cell phone" artifacts).
        //
        // Critically, this is ONLY applied to the CASCADE path (voiced sounds).
        // The PARALLEL path (fricatives) is left untouched because fricative
        // noise is aperiodic — aliased noise is still noise, with no beating.
        // This is why DECTalk sounds clean at 11025: its cascade has only 5
        // formants (no F6), and its parallel branch has independent gains.
        //
        // Fade: ratio = cf/nyquist.  <0.65 → full, >0.85 → bypass, linear between.
        // At 22050+ Hz all fades are 1.0 → zero cost / unchanged behaviour.
        const double nyquist = 0.5 * (double)sampleRate;
        auto cascadeFade = [&](double cf) -> double {
            if (cf <= 0.0 || !std::isfinite(cf)) return 1.0;
            double ratio = cf / nyquist;
            if (ratio < 0.65) return 1.0;
            if (ratio > 0.85) return 0.0;
            return 1.0 - (ratio - 0.65) / 0.20;
        };

        double preR6 = output;
        output = r6.resonate(output, frame->cf6, cb6);
        double fade6 = cascadeFade(frame->cf6);
        output = preR6 + fade6 * (output - preR6);

        double preR5 = output;
        output = r5.resonate(output, frame->cf5, cb5);
        double fade5 = cascadeFade(frame->cf5);
        output = preR5 + fade5 * (output - preR5);

        double preR4 = output;
        output = r4.resonate(output, frame->cf4, cb4);
        double fade4 = cascadeFade(frame->cf4);
        output = preR4 + fade4 * (output - preR4);
        output = r3.resonate(output, frame->cf3, cb3);
        output = r2.resonate(output, frame->cf2, cb2);
        // F1 uses pitch-synchronous resonator without Fujisaki compensation (dropped as we don't have F1 spikes it worked with.)
        output = r1.resonate(output, frame->cf1, cb1, glottisOpen);
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

    void decay(double factor) {
        r1.decay(factor); r2.decay(factor); r3.decay(factor);
        r4.decay(factor); r5.decay(factor); r6.decay(factor);
    }

    double getNext(const speechPlayer_frame_t* frame, const speechPlayer_frameEx_t* frameEx, bool glottisOpen, double input) {
        input/=2.0;
        (void)glottisOpen;
        double output=0;

        // Same Q-capping logic for parallel formants when their frequencies are swept.
        double pb1 = frame->pb1;
        double pb2 = frame->pb2;
        double pb3 = frame->pb3;
        if (frameEx) {
            if (std::isfinite(frameEx->endPf1)) pb1 = bandwidthForSweep(frame->pf1, pb1, kSweepQMaxF1, kSweepBwMinF1, kSweepBwMax);
            if (std::isfinite(frameEx->endPf2)) pb2 = bandwidthForSweep(frame->pf2, pb2, kSweepQMaxF2, kSweepBwMinF2, kSweepBwMax);
            if (std::isfinite(frameEx->endPf3)) pb3 = bandwidthForSweep(frame->pf3, pb3, kSweepQMaxF3, kSweepBwMinF3, kSweepBwMax);
        }

        output+=(r1.resonate(input,frame->pf1,pb1)-input)*frame->pa1;
        output+=(r2.resonate(input,frame->pf2,pb2)-input)*frame->pa2;
        output+=(r3.resonate(input,frame->pf3,pb3)-input)*frame->pa3;
        output+=(r4.resonate(input,frame->pf4,frame->pb4)-input)*frame->pa4;
        output+=(r5.resonate(input,frame->pf5,frame->pb5)-input)*frame->pa5;
        output+=(r6.resonate(input,frame->pf6,frame->pb6)-input)*frame->pa6;
        return calculateValueAtFadePosition(output,input,frame->parallelBypass);
    }
};

#endif // TGSPEECHBOX_FORMANTGENERATOR_H

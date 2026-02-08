/*
TGSpeechBox — Main speech wave generator (orchestrator).

SpeechWaveGeneratorImpl ties together the voice source, resonator
filter bank, and formant generators.  It handles frame management,
adaptive frication/aspiration filtering, high-shelf EQ, and
fade-in/fade-out for click-free start/stop.

The DSP modules are split into separate headers for readability:
  dspCommon.h         — tuning constants, utility classes (PRNG, lowpass, etc.)
  pitchModel.h        — Fujisaki-Bartman pitch contour model
  voiceGenerator.h    — LF glottal source with tilt, breathiness, tremor
  resonator.h         — All-pole resonator + pitch-sync F1 resonator
  formantGenerator.h  — Cascade and parallel formant topologies

All headers are implementation-private (not installed as public API).
*/

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include "debug.h"
#include "utils.h"
#include "speechWaveGenerator.h"

// Internal DSP modules (header-only, no Makefile changes needed)
#include "dspCommon.h"
#include "pitchModel.h"
#include "voiceGenerator.h"
#include "resonator.h"
#include "formantGenerator.h"

using namespace std;

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
    int startFadeRemaining;    // samples left in fade-in (prevents pop on speech start)
    int startFadeTotal;        // total fade-in length

    // Cascade duck smoother: prevents gain discontinuity at stop→vowel boundary.
    // The raw cascadeDuck can snap from 0.3→1.0 when va rises, causing a click.
    // Smoothing mirrors what shelfMix already does for the shelf duck.
    double smoothCascadeDuck;      // current smoothed duck value (1.0 = no duck)
    double cascadeDuckAlpha;       // per-sample smoothing coefficient

    // Peak limiter: catches amplitude spikes before they reach the OS audio system.
    // Fast attack (~0.1ms) catches transients, slow release (~50ms) recovers smoothly.
    // This prevents Windows/PulseAudio volume ducking on stop bursts mid-sentence.
    double limiterGain;        // current gain reduction (1.0 = no reduction)
    double limiterAttackAlpha; // per-sample attack coefficient
    double limiterReleaseAlpha;// per-sample release coefficient
    double limiterThreshold;   // signal level above which limiting kicks in

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
    SpeechWaveGeneratorImpl(int sr): sampleRate(sr), voiceGenerator(sr), fricGenerator(), cascade(sr), parallel(sr), frameManager(NULL), lastInput(0.0), lastOutput(0.0), wasSilence(true), smoothPreGain(0.0), preGainAttackAlpha(0.0), preGainReleaseAlpha(0.0), smoothFricAmp(0.0), fricAttackAlpha(0.0), fricReleaseAlpha(0.0), hsIn1(0), hsIn2(0), hsOut1(0), hsOut2(0), fricBurstLp1(sr), fricBurstLp2(sr), fricSustainLp1(sr), fricSustainLp2(sr), lastTargetFricAmp(0.0), lastTargetAspAmp(0.0), fricBurstFc(0.0), fricSustainFc(0.0), burstEnv(0.0), burstEnvDecayMul(1.0), aspLp1(sr), aspLp2(sr), aspBurstFc(0.0), shelfMix(1.0), shelfMixAlpha(0.0), lastBrightOut(0.0), stopFadeRemaining(0), stopFadeTotal(0), startFadeRemaining(0), startFadeTotal(0), limiterGain(1.0), limiterAttackAlpha(0.0), limiterReleaseAlpha(0.0), limiterThreshold(0.0), smoothCascadeDuck(1.0), cascadeDuckAlpha(0.0) {
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

        // Peak limiter: catches transient spikes from stop bursts
        // Attack: ~0.1ms (instant catch), Release: ~50ms (smooth recovery)
        // Threshold: -3dB below nominal peak (~3.86 in pre-scaled units)
        const double limiterAttackMs = 0.1;
        const double limiterReleaseMs = 50.0;
        limiterAttackAlpha = 1.0 - exp(-1.0 / (sampleRate * (limiterAttackMs * 0.001)));
        limiterReleaseAlpha = 1.0 - exp(-1.0 / (sampleRate * (limiterReleaseMs * 0.001)));
        // 32767 / 6000 = ~5.46 full scale; -3dB = 0.707 * 5.46 = ~3.86
        limiterThreshold = 3.86;

        // Cascade duck smoother: ~3ms time constant.
        // Fast enough to engage during a burst (~6ms hold), slow enough
        // that the release back to 1.0 doesn't snap when va rises.
        const double cascadeDuckMs = 3.0;
        cascadeDuckAlpha = 1.0 - exp(-1.0 / (sampleRate * (cascadeDuckMs * 0.001)));

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
        
        // Check if a purge happened — if so, trigger a fade-in to prevent pop
        // Note: We intentionally do NOT reset resonators here because that can cause
        // its own transient. The fade-in should mask any discontinuity.
        if(frameManager->checkAndClearPurgeFlag()) {
            // Only reset stateless things and trigger fade
            hsIn1 = 0.0; hsIn2 = 0.0; hsOut1 = 0.0; hsOut2 = 0.0;
            lastInput = 0.0;
            lastOutput = 0.0;
            // Trigger fade-in
            startFadeTotal = (int)(sampleRate * 0.004); // 6 ms - longer for higher sample rates
            if (startFadeTotal < 64) startFadeTotal = 64;
            startFadeRemaining = startFadeTotal;
            // Force full reset on next frame (clears Fujisaki IIR filter state)
            wasSilence = true;
        }
        
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
                    smoothCascadeDuck = 1.0;  // Reset duck (no ducking)
                    limiterGain = 1.0;  // Reset limiter (no gain reduction)
                    // Reset stop fade-out state
                    stopFadeTotal = 0;
                    stopFadeRemaining = 0;
                    // Reset high-shelf filter state to prevent pops from residual energy
                    hsIn1 = 0.0; hsIn2 = 0.0; hsOut1 = 0.0; hsOut2 = 0.0;
                    // Start a short fade-in to prevent pops on speech start
                    startFadeTotal = (int)(sampleRate * 0.002); // 2 ms
                    if (startFadeTotal < 16) startFadeTotal = 16;
                    startFadeRemaining = startFadeTotal;
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

                double cascadeOut = cascade.getNext(frame, frameEx, voiceGenerator.glottisOpen, voiceForCascade * smoothPreGain);

                // Generate raw frication noise
                double fricNoise = fricGenerator.getNext() * kFricNoiseScale * fricAmp * bypassGain * bypassVoicedDuck * voicedFricScale;

                // Apply tilt filter to frication (same tilt as aspiration for now)
                fricNoise = voiceGenerator.applyFricationTilt(fricNoise);

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

                double parallelOut=parallel.getNext(frame, frameEx, voiceGenerator.glottisOpen, fric*smoothPreGain);

                // Duck cascade residual during stop bursts to prevent energy addition.
                // Mid-sentence, cascade resonators still ring from the previous vowel
                // when the parallel burst fires — the sum causes an amplitude spike.
                // burstEnv high + va low = voiceless stop burst = duck cascade by 70%.
                // Nasal-aware: when caNP > 0, cascade carries wanted nasal murmur —
                // don't duck it.  caNP fades to 0 during nasal→stop transition, so
                // by the time the burst fires the full duck applies naturally.
                double nasalProtect = 1.0 - frame->caNP;
                double targetCascadeDuck = 1.0 - 0.7 * burstEnv * (1.0 - va) * nasalProtect;
                // Smooth the duck to prevent gain snap at stop→vowel boundary.
                // Without this, cascadeDuck jumps from 0.3→1.0 when va rises,
                // tripling the cascade ringing amplitude in one sample → click.
                // (The shelf duck at line ~453 already does this via shelfMixAlpha.)
                smoothCascadeDuck += cascadeDuckAlpha * (targetCascadeDuck - smoothCascadeDuck);
                double out=(cascadeOut*smoothCascadeDuck+parallelOut)*frame->outputGain;

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

                // Apply start fade-in if active (prevents pop on speech start)
                if (startFadeRemaining > 0) {
                    double fadeIn = 1.0 - ((double)startFadeRemaining / (double)startFadeTotal);
                    bright *= fadeIn;
                    startFadeRemaining--;
                }

                // Peak limiter: prevent amplitude spikes from triggering OS
                // volume ducking.  Fast attack grabs transients, slow release
                // recovers smoothly so normal speech is unaffected.
                {
                    double absBright = fabs(bright);
                    if (absBright > limiterThreshold) {
                        double targetGain = limiterThreshold / absBright;
                        limiterGain += limiterAttackAlpha * (targetGain - limiterGain);
                    } else {
                        limiterGain += limiterReleaseAlpha * (1.0 - limiterGain);
                    }
                    bright *= limiterGain;
                }

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
                        // Fade from 1.0 to 0.0 inclusive (last sample is exactly 0)
                        double t = (double)(stopFadeRemaining - 1) / (double)(stopFadeTotal - 1);
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
                // Reset high-shelf biquad state so next utterance starts clean
                hsIn1 = hsIn2 = hsOut1 = hsOut2 = 0.0;
                // Zero-fill remainder of buffer to prevent garbage audio if caller
                // plays the full buffer regardless of return value
                if (i < sampleCount) {
                    memset(&sampleBuf[i], 0, (sampleCount - i) * sizeof(sample));
                }
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
            currentTone.speedQuotient,
            currentTone.aspirationTiltDbPerOct
        );

        // Update high-shelf coefficients (do not reset state)
        initHighShelf(currentTone.highShelfFcHz, currentTone.highShelfGainDb, currentTone.highShelfQ);
        
        // Update pitch-sync F1 modulation params
        cascade.setPitchSyncParams(currentTone.pitchSyncF1DeltaHz, currentTone.pitchSyncB1DeltaHz);
        cascade.setCascadeBwScale(currentTone.cascadeBwScale);
        
        // Update tremor depth for elderly/shaky voice
        voiceGenerator.setTremorDepth(currentTone.tremorDepth);
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
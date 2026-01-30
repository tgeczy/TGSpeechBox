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
 * Enhanced frame interpolation based on Klatt 1980 and Hertz 1999 (ETI-Eloquence).
 * 
 * Key improvements over simple linear interpolation:
 * 
 * 1. AMPLITUDE PARAMETERS use asymmetric attack/release curves
 *    - Fast attack for crisp consonant onsets
 *    - Slower release for smooth vowel tails
 *    - Reference: Klatt 1980 "AV only takes effect at glottal pulse onset"
 * 
 * 2. FORMANT FREQUENCIES use smooth S-curve (ease-in-out)
 *    - Prevents "stepping" artifacts at phoneme boundaries
 *    - Mimics natural coarticulation
 *    - Reference: Hertz 1999 "non-steady-state targets as interpolation inflection points"
 * 
 * 3. FORMANT BANDWIDTHS lead frequencies slightly
 *    - Bandwidth widens BEFORE frequency shifts (natural articulation)
 *    - Creates smoother perceived transitions
 * 
 * 4. NASAL PARAMETERS (caNP) use slower curves
 *    - Nasal coupling doesn't change instantly in real speech
 *    - Prevents "clicky" nasal consonants
 */

#include <queue>
#include <cstring>
#include <cmath>
#include "utils.h"
#include "frame.h"

using namespace std;

// ============================================================================
// Interpolation curve functions
// ============================================================================

// Linear (original behavior)
static inline double curveLinear(double t) {
    return t;
}

// Smooth S-curve (ease-in-out) - good for formant frequencies
// Creates natural-sounding coarticulation
static inline double curveSmooth(double t) {
    // Attempt 1: smoothstep (3t² - 2t³)
    // return t * t * (3.0 - 2.0 * t);
    // Attempt 2: Ken Perlin's smoother step (6t⁵ - 15t⁴ + 10t³) - even smoother
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

// Fast attack, slow release - good for amplitude parameters
// Attack in first 30% of time, release over remaining 70%
static inline double curveAmplitude(double t, double oldVal, double newVal) {
    if (newVal > oldVal) {
        // Attack: fast rise (quadratic ease-out)
        double attackT = fmin(t / 0.3, 1.0);
        return 1.0 - (1.0 - attackT) * (1.0 - attackT);
    } else {
        // Release: slow fall (quadratic ease-in)
        return t * t;
    }
}

// Lead curve - reaches target slightly early
// Good for bandwidths that should "prepare" for frequency changes
static inline double curveLead(double t) {
    // Reaches ~90% at t=0.7, then eases to 100%
    if (t < 0.7) {
        return (t / 0.7) * 0.9;
    } else {
        return 0.9 + ((t - 0.7) / 0.3) * 0.1;
    }
}

// Slow curve for nasal - prevents clicky nasal consonants
static inline double curveNasal(double t) {
    // Even smoother than smoothstep - cubic ease-in-out with plateau
    if (t < 0.2) return 0.0;
    if (t > 0.8) return 1.0;
    double nt = (t - 0.2) / 0.6;  // normalize to 0-1 over middle 60%
    return nt * nt * (3.0 - 2.0 * nt);
}

// ============================================================================
// Parameter classification
// ============================================================================

// Parameter indices (must match frame.h order!)
enum ParamIndex {
    P_voicePitch = 0,
    P_vibratoPitchOffset,
    P_vibratoSpeed,
    P_voiceTurbulenceAmplitude,
    P_glottalOpenQuotient,
    P_voiceAmplitude,
    P_aspirationAmplitude,
    // Cascade formant frequencies
    P_cf1, P_cf2, P_cf3, P_cf4, P_cf5, P_cf6, P_cfN0, P_cfNP,
    // Cascade formant bandwidths
    P_cb1, P_cb2, P_cb3, P_cb4, P_cb5, P_cb6, P_cbN0, P_cbNP,
    // Nasal amplitude
    P_caNP,
    // Frication
    P_fricationAmplitude,
    // Parallel formant frequencies
    P_pf1, P_pf2, P_pf3, P_pf4, P_pf5, P_pf6,
    // Parallel formant bandwidths
    P_pb1, P_pb2, P_pb3, P_pb4, P_pb5, P_pb6,
    // Parallel formant amplitudes
    P_pa1, P_pa2, P_pa3, P_pa4, P_pa5, P_pa6,
    // Bypass and gains
    P_parallelBypass,
    P_preFormantGain,
    P_outputGain,
    P_endVoicePitch
};

enum CurveType {
    CURVE_LINEAR,
    CURVE_SMOOTH,      // S-curve for formant frequencies
    CURVE_AMPLITUDE,   // Fast attack, slow release
    CURVE_LEAD,        // Reaches target early (for bandwidths)
    CURVE_NASAL        // Slow curve for nasal params
};

static CurveType getParamCurveType(int paramIndex) {
    switch (paramIndex) {
        // Amplitude parameters: fast attack, slow release
        case P_voiceAmplitude:
        case P_aspirationAmplitude:
        case P_fricationAmplitude:
        case P_preFormantGain:
        case P_pa1: case P_pa2: case P_pa3: case P_pa4: case P_pa5: case P_pa6:
            return CURVE_AMPLITUDE;
        
        // Formant frequencies: smooth S-curve
        case P_cf1: case P_cf2: case P_cf3: case P_cf4: case P_cf5: case P_cf6:
        case P_pf1: case P_pf2: case P_pf3: case P_pf4: case P_pf5: case P_pf6:
            return CURVE_SMOOTH;
        
        // Formant bandwidths: lead curve (prepares for freq change)
        case P_cb1: case P_cb2: case P_cb3: case P_cb4: case P_cb5: case P_cb6:
        case P_pb1: case P_pb2: case P_pb3: case P_pb4: case P_pb5: case P_pb6:
            return CURVE_LEAD;
        
        // Nasal parameters: slow curve
        case P_cfN0: case P_cfNP: case P_cbN0: case P_cbNP: case P_caNP:
            return CURVE_NASAL;
        
        // Everything else: linear
        default:
            return CURVE_LINEAR;
    }
}

// ============================================================================
// Frame interpolation with parameter-specific curves
// ============================================================================

static double interpolateParam(int paramIndex, double oldVal, double newVal, double t) {
    CurveType curve = getParamCurveType(paramIndex);
    double curvedT;
    
    switch (curve) {
        case CURVE_SMOOTH:
            curvedT = curveSmooth(t);
            break;
        case CURVE_AMPLITUDE:
            curvedT = curveAmplitude(t, oldVal, newVal);
            break;
        case CURVE_LEAD:
            curvedT = curveLead(t);
            break;
        case CURVE_NASAL:
            curvedT = curveNasal(t);
            break;
        case CURVE_LINEAR:
        default:
            curvedT = t;
            break;
    }
    
    return calculateValueAtFadePosition(oldVal, newVal, curvedT);
}

// ============================================================================
// Frame Manager Implementation
// ============================================================================

struct frameRequest_t {
    unsigned int minNumSamples;
    unsigned int numFadeSamples;
    bool NULLFrame;
    speechPlayer_frame_t frame;
    double voicePitchInc; 
    int userIndex;
};

class FrameManagerImpl: public FrameManager {
    private:
    LockableObject frameLock;
    queue<frameRequest_t*> frameRequestQueue;
    frameRequest_t* oldFrameRequest;
    frameRequest_t* newFrameRequest;
    speechPlayer_frame_t curFrame;
    bool curFrameIsNULL;
    unsigned int sampleCounter;
    int lastUserIndex;

    void updateCurrentFrame() {
        sampleCounter++;
        if(newFrameRequest) {
            if(sampleCounter>(newFrameRequest->numFadeSamples)) {
                delete oldFrameRequest;
                oldFrameRequest=newFrameRequest;
                newFrameRequest=NULL;
                // Ensure curFrame is updated even when numFadeSamples==0.
                memcpy(&curFrame, &(oldFrameRequest->frame), sizeof(speechPlayer_frame_t));
            } else {
                double curFadeRatio=(double)sampleCounter/(newFrameRequest->numFadeSamples);
                
                // Use parameter-specific interpolation curves
                for(int i=0;i<speechPlayer_frame_numParams;++i) {
                    double oldVal = ((speechPlayer_frameParam_t*)&(oldFrameRequest->frame))[i];
                    double newVal = ((speechPlayer_frameParam_t*)&(newFrameRequest->frame))[i];
                    ((speechPlayer_frameParam_t*)&curFrame)[i] = interpolateParam(i, oldVal, newVal, curFadeRatio);
                }
            }
        } else if(sampleCounter>(oldFrameRequest->minNumSamples)) {
            if(!frameRequestQueue.empty()) {
                bool wasFromSilence = curFrameIsNULL || oldFrameRequest->NULLFrame;
                curFrameIsNULL=false;
                newFrameRequest=frameRequestQueue.front();
                frameRequestQueue.pop();
                if(newFrameRequest->NULLFrame) {
                    memcpy(&(newFrameRequest->frame),&(oldFrameRequest->frame),sizeof(speechPlayer_frame_t));
                    newFrameRequest->frame.preFormantGain=0;
                    newFrameRequest->frame.voicePitch=curFrame.voicePitch;
                    newFrameRequest->voicePitchInc=0;
                } else if(oldFrameRequest->NULLFrame) {
                    memcpy(&(oldFrameRequest->frame),&(newFrameRequest->frame),sizeof(speechPlayer_frame_t));
                    oldFrameRequest->frame.preFormantGain=0;
                    // FIX: We are transitioning from silence to real audio.
                    // Mark the old request as non-NULL so subsequent transitions don't keep
                    // taking the "from silence" path with stale state.
                    oldFrameRequest->NULLFrame=false;
                }
                if(newFrameRequest) {
                    if(newFrameRequest->userIndex!=-1) lastUserIndex=newFrameRequest->userIndex;
                    sampleCounter=0;
                    // Process the start of the transition immediately (sample 0), so the
                    // first sample of a new segment can't use stale/garbage parameters.
                    if(wasFromSilence) {
                        memcpy(&curFrame, &(oldFrameRequest->frame), sizeof(speechPlayer_frame_t));
                    }
                    newFrameRequest->frame.voicePitch+=(newFrameRequest->voicePitchInc*newFrameRequest->numFadeSamples);
                }
            } else {
                curFrameIsNULL=true;
                // FIX: We have run out of frames. Mark the old request as NULL (Silence).
                // This ensures that when a new frame eventually arrives, the engine treats it
                // as a "Start from Silence" (triggering the 0-gain fade-in logic) rather than
                // trying to interpolate from the stale state of the last utterance.
                oldFrameRequest->NULLFrame = true;
            }
        } else {
            curFrame.voicePitch+=oldFrameRequest->voicePitchInc;
            oldFrameRequest->frame.voicePitch=curFrame.voicePitch;
        }
    }


    public:

    FrameManagerImpl(): curFrame(), curFrameIsNULL(true), sampleCounter(0), newFrameRequest(NULL), lastUserIndex(-1)  {
        // speechPlayer_frame_t is a plain C struct; ensure it starts from a known state.
        memset(&curFrame, 0, sizeof(speechPlayer_frame_t));
        oldFrameRequest=new frameRequest_t();
        oldFrameRequest->minNumSamples=0;
        oldFrameRequest->numFadeSamples=0;
        oldFrameRequest->NULLFrame=true;
        memset(&(oldFrameRequest->frame), 0, sizeof(speechPlayer_frame_t));
        oldFrameRequest->voicePitchInc=0;
        oldFrameRequest->userIndex=-1;
    }

    void queueFrame(speechPlayer_frame_t* frame, unsigned int minNumSamples, unsigned int numFadeSamples, int userIndex, bool purgeQueue) {
        frameLock.acquire();
        frameRequest_t* frameRequest=new frameRequest_t;
        frameRequest->minNumSamples=minNumSamples; //max(minNumSamples,1);
        frameRequest->numFadeSamples=numFadeSamples; //max(numFadeSamples,1);
        if(frame) {
            frameRequest->NULLFrame=false;
            memcpy(&(frameRequest->frame),frame,sizeof(speechPlayer_frame_t));
            frameRequest->voicePitchInc=(frameRequest->minNumSamples>0)?((frame->endVoicePitch-frame->voicePitch)/frameRequest->minNumSamples):0;
        } else {
            frameRequest->NULLFrame=true;
            memset(&(frameRequest->frame), 0, sizeof(speechPlayer_frame_t));
            frameRequest->voicePitchInc=0;
        }
        frameRequest->userIndex=userIndex;
        if(purgeQueue) {
            for(;!frameRequestQueue.empty();frameRequestQueue.pop()) delete frameRequestQueue.front();
            sampleCounter=oldFrameRequest->minNumSamples;
            // Always snapshot curFrame to preserve current audio state for smooth transitions.
            // This ensures we fade FROM the current state, not from stale/garbage parameters.
            // Must happen regardless of whether newFrameRequest exists.
            if(!curFrameIsNULL) {
                oldFrameRequest->NULLFrame=false;
                memcpy(&(oldFrameRequest->frame),&curFrame,sizeof(speechPlayer_frame_t));
            }
            if(newFrameRequest) {
                delete newFrameRequest;
                newFrameRequest=NULL;
            }
        }
        frameRequestQueue.push(frameRequest);
        frameLock.release();
    }

    const int getLastIndex() {
        return lastUserIndex;
    }

    const speechPlayer_frame_t* const getCurrentFrame() {
        frameLock.acquire();
        updateCurrentFrame();
        frameLock.release();
        return curFrameIsNULL?NULL:&curFrame;
    }

    ~FrameManagerImpl() {
        if(oldFrameRequest) delete oldFrameRequest;
        if(newFrameRequest) delete newFrameRequest;
    }

};

FrameManager* FrameManager::create() { return new FrameManagerImpl(); }

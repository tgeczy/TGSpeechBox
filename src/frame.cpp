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

#include <queue>
#include <cstring>
#include <cstddef>  // offsetof
#include "utils.h"
#include "frame.h"

using namespace std;

// Identifies which speechPlayer_frame_t parameter indices represent Hz frequencies.
// These get log-domain interpolation; everything else gets linear.
// Uses offsetof so it stays correct if the struct layout ever changes.
static inline bool isFrequencyParam(int idx) {
	const int szP = sizeof(speechPlayer_frameParam_t);
	// voicePitch and endVoicePitch
	if(idx == (int)(offsetof(speechPlayer_frame_t, voicePitch) / szP)) return true;
	if(idx == (int)(offsetof(speechPlayer_frame_t, endVoicePitch) / szP)) return true;
	// Cascade formant frequencies: cf1 through cfNP
	int cfFirst = (int)(offsetof(speechPlayer_frame_t, cf1) / szP);
	int cfLast  = (int)(offsetof(speechPlayer_frame_t, cfNP) / szP);
	if(idx >= cfFirst && idx <= cfLast) return true;
	// Parallel formant frequencies: pf1 through pf6
	int pfFirst = (int)(offsetof(speechPlayer_frame_t, pf1) / szP);
	int pfLast  = (int)(offsetof(speechPlayer_frame_t, pf6) / szP);
	if(idx >= pfFirst && idx <= pfLast) return true;
	return false;
}

struct frameRequest_t {
	unsigned int minNumSamples;
	unsigned int numFadeSamples;
	bool NULLFrame;

	// Optional per-frame voice quality params (DSP v5+)
	bool hasFrameEx;
	speechPlayer_frameEx_t frameEx;

	speechPlayer_frame_t frame;
	double voicePitchInc;
	
	// Formant end targets for exponential smoothing (DECTalk-style transitions)
	// NAN = no ramping for that formant
	double endCf1, endCf2, endCf3;  // Cascade formant targets (Hz)
	double endPf1, endPf2, endPf3;  // Parallel formant targets (Hz)
	double formantAlpha;            // Exponential smoothing coefficient
	
	int userIndex;
};

class FrameManagerImpl: public FrameManager {
	private:
	LockableObject frameLock;
	queue<frameRequest_t*> frameRequestQueue;
	frameRequest_t* oldFrameRequest;
	frameRequest_t* newFrameRequest;
	speechPlayer_frame_t curFrame;
	speechPlayer_frameEx_t curFrameEx;
	bool curFrameIsNULL;
	bool curHasFrameEx;
	unsigned int sampleCounter;
	int lastUserIndex;
	bool purgeFlag;  // Set on purge, cleared when checked

	void updateCurrentFrame() {
		sampleCounter++;
		if(newFrameRequest) {
			if(sampleCounter>(newFrameRequest->numFadeSamples)) {
				delete oldFrameRequest;
				oldFrameRequest=newFrameRequest;
				newFrameRequest=NULL;
				// Ensure curFrame is updated even when numFadeSamples==0.
				memcpy(&curFrame, &(oldFrameRequest->frame), sizeof(speechPlayer_frame_t));
				memcpy(&curFrameEx, &(oldFrameRequest->frameEx), sizeof(speechPlayer_frameEx_t));
				curHasFrameEx = oldFrameRequest->hasFrameEx;
			} else {
				double linearRatio=(double)sampleCounter/(newFrameRequest->numFadeSamples);
				// Cosine ease-in/ease-out for spectral parameters only.
				// Amplitude/gain parameters stay linear so that energy crossfades
				// are monotonic — the S-curve can create brief energy dips at
				// source transitions (e.g. voiced stop → aspiration) that sound
				// like pops.
				double cosineRatio = cosineSmooth(linearRatio);
				for(int i=0;i<speechPlayer_frame_numParams;++i) {
					double oldVal = ((speechPlayer_frameParam_t*)&(oldFrameRequest->frame))[i];
					double newVal = ((speechPlayer_frameParam_t*)&(newFrameRequest->frame))[i];
					if(isFrequencyParam(i)) {
						((speechPlayer_frameParam_t*)&curFrame)[i]=calculateFreqAtFadePosition(oldVal, newVal, cosineRatio);
					} else {
						((speechPlayer_frameParam_t*)&curFrame)[i]=calculateValueAtFadePosition(oldVal, newVal, linearRatio);
					}
				}
				if(oldFrameRequest->hasFrameEx || newFrameRequest->hasFrameEx) {
					curHasFrameEx = true;

					// Some FrameEx fields are *command-like* and must not be interpolated.
					// In particular, the Fujisaki pitch model triggers (amp/len/dur) must be
					// applied with their exact values at the start of a transition; otherwise
					// fades would scale them down and cause incorrect trigger timing.
					const int pitchStartIdx = (int)(offsetof(speechPlayer_frameEx_t, fujisakiEnabled) / sizeof(double));
					for(int i=0;i<speechPlayer_frameEx_numParams;++i) {
						if (i >= pitchStartIdx) {
							// Step to the NEW values immediately (no interpolation).
							((double*)&curFrameEx)[i]=((double*)&(newFrameRequest->frameEx))[i];
						} else {
							((double*)&curFrameEx)[i]=calculateValueAtFadePosition(((double*)&(oldFrameRequest->frameEx))[i],((double*)&(newFrameRequest->frameEx))[i],linearRatio);
						}
					}
					} else {
						curHasFrameEx = false;
						curFrameEx = speechPlayer_frameEx_defaults;
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

					// Carry frameEx through silence fades so transitions stay smooth.
					memcpy(&(newFrameRequest->frameEx),&(oldFrameRequest->frameEx),sizeof(speechPlayer_frameEx_t));
					newFrameRequest->hasFrameEx = oldFrameRequest->hasFrameEx;
				} else if(oldFrameRequest->NULLFrame) {
					memcpy(&(oldFrameRequest->frame),&(newFrameRequest->frame),sizeof(speechPlayer_frame_t));
					oldFrameRequest->frame.preFormantGain=0;
					// FIX: We are transitioning from silence to real audio.
					// Mark the old request as non-NULL so subsequent transitions don't keep
					// taking the "from silence" path with stale state.
					oldFrameRequest->NULLFrame=false;

					memcpy(&(oldFrameRequest->frameEx),&(newFrameRequest->frameEx),sizeof(speechPlayer_frameEx_t));
					oldFrameRequest->hasFrameEx = newFrameRequest->hasFrameEx;
				}
				if(newFrameRequest) {
					if(newFrameRequest->userIndex!=-1) lastUserIndex=newFrameRequest->userIndex;
					sampleCounter=0;
					// Process the start of the transition immediately (sample 0), so the
					// first sample of a new segment can't use stale/garbage parameters.
					if(wasFromSilence) {
						memcpy(&curFrame, &(oldFrameRequest->frame), sizeof(speechPlayer_frame_t));
						memcpy(&curFrameEx, &(oldFrameRequest->frameEx), sizeof(speechPlayer_frameEx_t));
						curHasFrameEx = oldFrameRequest->hasFrameEx;
					}
					newFrameRequest->frame.voicePitch+=(newFrameRequest->voicePitchInc*newFrameRequest->numFadeSamples);
				}
			} else {
				curFrameIsNULL=true;
				curHasFrameEx=false;
				curFrameEx = speechPlayer_frameEx_defaults;
				// FIX: We have run out of frames. Mark the old request as NULL (Silence).
				// This ensures that when a new frame eventually arrives, the engine treats it
				// as a "Start from Silence" (triggering the 0-gain fade-in logic) rather than
				// trying to interpolate from the stale state of the last utterance.
				oldFrameRequest->NULLFrame = true;
			}
		} else {
			// Per-sample pitch ramping (linear)
			curFrame.voicePitch+=oldFrameRequest->voicePitchInc;
			oldFrameRequest->frame.voicePitch=curFrame.voicePitch;
			
			// Per-sample formant ramping with exponential smoothing
			// This mimics articulatory inertia - fast initial movement, gentle settling
			double alpha = oldFrameRequest->formantAlpha;
			if(std::isfinite(oldFrameRequest->endCf1)) {
				curFrame.cf1 += alpha * (oldFrameRequest->endCf1 - curFrame.cf1);
				oldFrameRequest->frame.cf1 = curFrame.cf1;
			}
			if(std::isfinite(oldFrameRequest->endCf2)) {
				curFrame.cf2 += alpha * (oldFrameRequest->endCf2 - curFrame.cf2);
				oldFrameRequest->frame.cf2 = curFrame.cf2;
			}
			if(std::isfinite(oldFrameRequest->endCf3)) {
				curFrame.cf3 += alpha * (oldFrameRequest->endCf3 - curFrame.cf3);
				oldFrameRequest->frame.cf3 = curFrame.cf3;
			}
			if(std::isfinite(oldFrameRequest->endPf1)) {
				curFrame.pf1 += alpha * (oldFrameRequest->endPf1 - curFrame.pf1);
				oldFrameRequest->frame.pf1 = curFrame.pf1;
			}
			if(std::isfinite(oldFrameRequest->endPf2)) {
				curFrame.pf2 += alpha * (oldFrameRequest->endPf2 - curFrame.pf2);
				oldFrameRequest->frame.pf2 = curFrame.pf2;
			}
			if(std::isfinite(oldFrameRequest->endPf3)) {
				curFrame.pf3 += alpha * (oldFrameRequest->endPf3 - curFrame.pf3);
				oldFrameRequest->frame.pf3 = curFrame.pf3;
			}
		}
	}


	public:

	FrameManagerImpl(): curFrame(), curFrameEx(), curFrameIsNULL(true), curHasFrameEx(false), sampleCounter(0), newFrameRequest(NULL), lastUserIndex(-1), purgeFlag(false)  {
		// speechPlayer_frame_t is a plain C struct; ensure it starts from a known state.
		memset(&curFrame, 0, sizeof(speechPlayer_frame_t));
		curFrameEx = speechPlayer_frameEx_defaults;

		oldFrameRequest=new frameRequest_t();
		oldFrameRequest->minNumSamples=0;
		oldFrameRequest->numFadeSamples=0;
		oldFrameRequest->NULLFrame=true;
		oldFrameRequest->hasFrameEx=false;
		memset(&(oldFrameRequest->frame), 0, sizeof(speechPlayer_frame_t));
		oldFrameRequest->frameEx = speechPlayer_frameEx_defaults;
		oldFrameRequest->voicePitchInc=0;
		oldFrameRequest->endCf1=NAN;
		oldFrameRequest->endCf2=NAN;
		oldFrameRequest->endCf3=NAN;
		oldFrameRequest->endPf1=NAN;
		oldFrameRequest->endPf2=NAN;
		oldFrameRequest->endPf3=NAN;
		oldFrameRequest->formantAlpha=0;
		oldFrameRequest->userIndex=-1;
	}

	void queueFrame(speechPlayer_frame_t* frame, unsigned int minNumSamples, unsigned int numFadeSamples, int userIndex, bool purgeQueue) override {
		queueFrameEx(frame, NULL, 0, minNumSamples, numFadeSamples, userIndex, purgeQueue);
	}

	void queueFrameEx(speechPlayer_frame_t* frame, const speechPlayer_frameEx_t* frameEx, unsigned int frameExSize, unsigned int minNumSamples, unsigned int numFadeSamples, int userIndex, bool purgeQueue) override {
		frameLock.acquire();
		frameRequest_t* frameRequest=new frameRequest_t;
		frameRequest->minNumSamples=minNumSamples;
		// Enforce minimum of 1 to prevent divide-by-zero in updateCurrentFrame().
		// This makes the class self-protecting rather than relying on callers.
		frameRequest->numFadeSamples=numFadeSamples > 0 ? numFadeSamples : 1;
		if(frame) {
			frameRequest->NULLFrame=false;
			memcpy(&(frameRequest->frame),frame,sizeof(speechPlayer_frame_t));
			frameRequest->voicePitchInc=(frameRequest->minNumSamples>0)?((frame->endVoicePitch-frame->voicePitch)/frameRequest->minNumSamples):0;
		} else {
			frameRequest->NULLFrame=true;
			memset(&(frameRequest->frame), 0, sizeof(speechPlayer_frame_t));
			frameRequest->voicePitchInc=0;
		}
		
		// Initialize formant end targets to NAN (no ramping by default)
		frameRequest->endCf1 = NAN;
		frameRequest->endCf2 = NAN;
		frameRequest->endCf3 = NAN;
		frameRequest->endPf1 = NAN;
		frameRequest->endPf2 = NAN;
		frameRequest->endPf3 = NAN;
		frameRequest->formantAlpha = 0.0;

		// Copy frameEx safely: start with defaults, then overlay caller's data.
		// This allows older callers with smaller structs to work with newer DLLs,
		// and ensures new parameters get sensible defaults (not just zeros).
		if(frameEx && frameExSize > 0) {
			frameRequest->hasFrameEx=true;
			frameRequest->frameEx = speechPlayer_frameEx_defaults;
			unsigned int copySize = frameExSize < sizeof(speechPlayer_frameEx_t) ? frameExSize : sizeof(speechPlayer_frameEx_t);
			memcpy(&(frameRequest->frameEx), frameEx, copySize);
			
			// Store formant end targets for exponential smoothing
			// Tau of ~10ms gives smooth articulatory movement that mimics real speech
			// At 22050 Hz: tau=220 samples; at 44100 Hz: tau=440 samples
			// We use a fixed alpha that works well across sample rates
			const double kFormantAlpha = 0.004;  // ~10-15ms time constant
			
			bool hasAnyFormantTarget = false;
			if(std::isfinite(frameRequest->frameEx.endCf1)) {
				frameRequest->endCf1 = frameRequest->frameEx.endCf1;
				hasAnyFormantTarget = true;
			}
			if(std::isfinite(frameRequest->frameEx.endCf2)) {
				frameRequest->endCf2 = frameRequest->frameEx.endCf2;
				hasAnyFormantTarget = true;
			}
			if(std::isfinite(frameRequest->frameEx.endCf3)) {
				frameRequest->endCf3 = frameRequest->frameEx.endCf3;
				hasAnyFormantTarget = true;
			}
			if(std::isfinite(frameRequest->frameEx.endPf1)) {
				frameRequest->endPf1 = frameRequest->frameEx.endPf1;
				hasAnyFormantTarget = true;
			}
			if(std::isfinite(frameRequest->frameEx.endPf2)) {
				frameRequest->endPf2 = frameRequest->frameEx.endPf2;
				hasAnyFormantTarget = true;
			}
			if(std::isfinite(frameRequest->frameEx.endPf3)) {
				frameRequest->endPf3 = frameRequest->frameEx.endPf3;
				hasAnyFormantTarget = true;
			}
			
			if(hasAnyFormantTarget) {
				frameRequest->formantAlpha = kFormantAlpha;
			}
		} else {
			frameRequest->hasFrameEx=false;
			frameRequest->frameEx = speechPlayer_frameEx_defaults;
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
				oldFrameRequest->hasFrameEx=curHasFrameEx;
				memcpy(&(oldFrameRequest->frameEx),&curFrameEx,sizeof(speechPlayer_frameEx_t));
			}
			if(newFrameRequest) {
				delete newFrameRequest;
				newFrameRequest=NULL;
			}
			purgeFlag = true;  // Signal to wave generator that a purge happened
		}
		frameRequestQueue.push(frameRequest);
		frameLock.release();
	}

	const int getLastIndex() override {
		return lastUserIndex;
	}

	bool checkAndClearPurgeFlag() override {
		frameLock.acquire();
		bool wasPurged = purgeFlag;
		purgeFlag = false;
		frameLock.release();
		return wasPurged;
	}

	const speechPlayer_frame_t* const getCurrentFrameWithEx(const speechPlayer_frameEx_t** outFrameEx) override {
		frameLock.acquire();
		updateCurrentFrame();
		if(outFrameEx) {
			if(curFrameIsNULL || !curHasFrameEx) *outFrameEx=NULL;
			else *outFrameEx=&curFrameEx;
		}
		frameLock.release();
		return curFrameIsNULL?NULL:&curFrame;
	}

	~FrameManagerImpl() override {
		// Acquire lock during teardown to ensure audio thread isn't mid-read.
		// Caller should have stopped audio callbacks first, but this is defensive.
		frameLock.acquire();
		if(oldFrameRequest) delete oldFrameRequest;
		if(newFrameRequest) delete newFrameRequest;
		while(!frameRequestQueue.empty()) {
			delete frameRequestQueue.front();
			frameRequestQueue.pop();
		}
		frameLock.release();
	}

};

FrameManager* FrameManager::create() { return new FrameManagerImpl(); }
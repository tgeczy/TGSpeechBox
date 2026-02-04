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
#include "utils.h"
#include "frame.h"

using namespace std;

struct frameRequest_t {
	unsigned int minNumSamples;
	unsigned int numFadeSamples;
	bool NULLFrame;

	// Optional per-frame voice quality params (DSP v5+)
	bool hasFrameEx;
	speechPlayer_frameEx_t frameEx;

	speechPlayer_frame_t frame;
	double voicePitchInc;
	
	// Formant increments for within-frame ramping (like voicePitchInc but for formants)
	// These are calculated from frameEx endCf1/2/3, endPf1/2/3
	double cf1Inc, cf2Inc, cf3Inc;  // Cascade formant increments (Hz per sample)
	double pf1Inc, pf2Inc, pf3Inc;  // Parallel formant increments (Hz per sample)
	
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
				double curFadeRatio=(double)sampleCounter/(newFrameRequest->numFadeSamples);
				for(int i=0;i<speechPlayer_frame_numParams;++i) {
					((speechPlayer_frameParam_t*)&curFrame)[i]=calculateValueAtFadePosition(((speechPlayer_frameParam_t*)&(oldFrameRequest->frame))[i],((speechPlayer_frameParam_t*)&(newFrameRequest->frame))[i],curFadeRatio);
				}
				if(oldFrameRequest->hasFrameEx || newFrameRequest->hasFrameEx) {
					curHasFrameEx = true;
					for(int i=0;i<speechPlayer_frameEx_numParams;++i) {
						((double*)&curFrameEx)[i]=calculateValueAtFadePosition(((double*)&(oldFrameRequest->frameEx))[i],((double*)&(newFrameRequest->frameEx))[i],curFadeRatio);
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
			// Per-sample pitch ramping
			curFrame.voicePitch+=oldFrameRequest->voicePitchInc;
			oldFrameRequest->frame.voicePitch=curFrame.voicePitch;
			
			// Per-sample formant ramping (DECTalk-style within-frame transitions)
			curFrame.cf1+=oldFrameRequest->cf1Inc;
			curFrame.cf2+=oldFrameRequest->cf2Inc;
			curFrame.cf3+=oldFrameRequest->cf3Inc;
			curFrame.pf1+=oldFrameRequest->pf1Inc;
			curFrame.pf2+=oldFrameRequest->pf2Inc;
			curFrame.pf3+=oldFrameRequest->pf3Inc;
			// Update stored frame values so crossfades start from correct position
			oldFrameRequest->frame.cf1=curFrame.cf1;
			oldFrameRequest->frame.cf2=curFrame.cf2;
			oldFrameRequest->frame.cf3=curFrame.cf3;
			oldFrameRequest->frame.pf1=curFrame.pf1;
			oldFrameRequest->frame.pf2=curFrame.pf2;
			oldFrameRequest->frame.pf3=curFrame.pf3;
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
		oldFrameRequest->cf1Inc=0;
		oldFrameRequest->cf2Inc=0;
		oldFrameRequest->cf3Inc=0;
		oldFrameRequest->pf1Inc=0;
		oldFrameRequest->pf2Inc=0;
		oldFrameRequest->pf3Inc=0;
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
		
		// Initialize formant increments to 0 (no ramping by default)
		frameRequest->cf1Inc = 0.0;
		frameRequest->cf2Inc = 0.0;
		frameRequest->cf3Inc = 0.0;
		frameRequest->pf1Inc = 0.0;
		frameRequest->pf2Inc = 0.0;
		frameRequest->pf3Inc = 0.0;

		// Copy frameEx safely: start with defaults, then overlay caller's data.
		// This allows older callers with smaller structs to work with newer DLLs,
		// and ensures new parameters get sensible defaults (not just zeros).
		if(frameEx && frameExSize > 0) {
			frameRequest->hasFrameEx=true;
			frameRequest->frameEx = speechPlayer_frameEx_defaults;
			unsigned int copySize = frameExSize < sizeof(speechPlayer_frameEx_t) ? frameExSize : sizeof(speechPlayer_frameEx_t);
			memcpy(&(frameRequest->frameEx), frameEx, copySize);
			
			// Calculate formant increments if end targets are set (not NAN)
			// This enables DECTalk-style within-frame formant ramping
			if(frame && minNumSamples > 0) {
				if(!std::isnan(frameRequest->frameEx.endCf1)) {
					frameRequest->cf1Inc = (frameRequest->frameEx.endCf1 - frame->cf1) / minNumSamples;
				}
				if(!std::isnan(frameRequest->frameEx.endCf2)) {
					frameRequest->cf2Inc = (frameRequest->frameEx.endCf2 - frame->cf2) / minNumSamples;
				}
				if(!std::isnan(frameRequest->frameEx.endCf3)) {
					frameRequest->cf3Inc = (frameRequest->frameEx.endCf3 - frame->cf3) / minNumSamples;
				}
				if(!std::isnan(frameRequest->frameEx.endPf1)) {
					frameRequest->pf1Inc = (frameRequest->frameEx.endPf1 - frame->pf1) / minNumSamples;
				}
				if(!std::isnan(frameRequest->frameEx.endPf2)) {
					frameRequest->pf2Inc = (frameRequest->frameEx.endPf2 - frame->pf2) / minNumSamples;
				}
				if(!std::isnan(frameRequest->frameEx.endPf3)) {
					frameRequest->pf3Inc = (frameRequest->frameEx.endPf3 - frame->pf3) / minNumSamples;
				}
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
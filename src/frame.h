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

#ifndef SPEECHPLAYER_FRAME_H
#define SPEECHPLAYER_FRAME_H

#include "lock.h"

typedef double speechPlayer_frameParam_t;

typedef struct {
	// voicing and cascaide
	speechPlayer_frameParam_t voicePitch; //  fundermental frequency of voice (phonation) in hz
	speechPlayer_frameParam_t vibratoPitchOffset; // pitch is offset up or down in fraction of a semitone
	speechPlayer_frameParam_t vibratoSpeed; // Speed of vibrato in hz
	speechPlayer_frameParam_t voiceTurbulenceAmplitude; // amplitude of voice breathiness from 0 to 1 
	speechPlayer_frameParam_t glottalOpenQuotient; // fraction between 0 and 1 of a voice cycle that the glottis is open (allows voice turbulance, alters f1...)
	speechPlayer_frameParam_t voiceAmplitude; // amplitude of voice (phonation) source between 0 and 1.
	speechPlayer_frameParam_t aspirationAmplitude; // amplitude of aspiration (voiceless h, whisper) source between 0 and 1.
	speechPlayer_frameParam_t cf1, cf2, cf3, cf4, cf5, cf6, cfN0, cfNP; // frequencies of standard cascaide formants, nasal (anti) 0 and nasal pole in hz
	speechPlayer_frameParam_t cb1, cb2, cb3, cb4, cb5, cb6, cbN0, cbNP; // bandwidths of standard cascaide formants, nasal (anti) 0 and nasal pole in hz
	speechPlayer_frameParam_t caNP; // amplitude from 0 to 1 of cascade nasal pole formant
	// fricatives and parallel
	speechPlayer_frameParam_t fricationAmplitude; // amplitude of frication noise from 0 to 1.
	speechPlayer_frameParam_t pf1, pf2, pf3, pf4, pf5, pf6; // parallel formants in hz
	speechPlayer_frameParam_t pb1, pb2, pb3, pb4, pb5, pb6; // parallel formant bandwidths in hz
	speechPlayer_frameParam_t pa1, pa2, pa3, pa4, pa5, pa6; // amplitude of parallel formants between 0 and 1
	speechPlayer_frameParam_t parallelBypass; // amount of signal which should bypass parallel resonators from 0 to 1
	speechPlayer_frameParam_t preFormantGain; // amplitude from 0 to 1 of all vocal tract sound (voicing, frication) before entering formant resonators. Useful for stopping/starting speech
	speechPlayer_frameParam_t outputGain; // amplitude from 0 to 1 of final output (master volume) 
	speechPlayer_frameParam_t endVoicePitch; //  pitch of voice at the end of the frame length 
} speechPlayer_frame_t;

const int speechPlayer_frame_numParams=sizeof(speechPlayer_frame_t)/sizeof(speechPlayer_frameParam_t);

/* ============================================================================
 * Optional per-frame voice quality extensions (DSP v5+)
 * ============================================================================
 *
 * These parameters are intentionally kept out of speechPlayer_frame_t so the
 * original 47-parameter ABI stays stable.
 *
 * All fields are expected to be in the range [0.0, 1.0] unless documented
 * otherwise. Values outside that range may be clamped by the DSP.
 */
typedef struct {
	double creakiness;      // laryngealization / creaky voice (e.g. Danish stød)
	double breathiness;     // breath noise mixed into voicing
	double jitter;          // pitch period variation (irregular F0)
	double shimmer;         // amplitude variation (irregular loudness)
	// room for more...
} speechPlayer_frameEx_t;

const int speechPlayer_frameEx_numParams=sizeof(speechPlayer_frameEx_t)/sizeof(double);


class FrameManager {
	public:
	static FrameManager* create(); //factory function

	// Core frame queue (legacy)
	virtual void queueFrame(speechPlayer_frame_t* frame, unsigned int minNumSamples, unsigned int numFadeSamples, int userIndex, bool purgeQueue)=0;

	// Extended frame queue (DSP v5+): optional per-frame voice quality params.
	// If frameEx is NULL or frameExSize is 0, behavior must match queueFrame() exactly.
	// frameExSize allows safe extension of frameEx struct in future versions.
	virtual void queueFrameEx(speechPlayer_frame_t* frame, const speechPlayer_frameEx_t* frameEx, unsigned int frameExSize, unsigned int minNumSamples, unsigned int numFadeSamples, int userIndex, bool purgeQueue)=0;

	// Fetch the current frame (and optional extended params) for the next output sample.
	// The returned pointers are owned by the FrameManager and remain valid until the next call.
	// If there is no active frame (silence), returns NULL and sets *outFrameEx to NULL.
	virtual const speechPlayer_frame_t* const getCurrentFrameWithEx(const speechPlayer_frameEx_t** outFrameEx)=0;

	// Back-compat convenience wrapper.
	virtual const speechPlayer_frame_t* const getCurrentFrame() { return getCurrentFrameWithEx(NULL); }

	virtual const int getLastIndex()=0;

	// Check if a purge happened since last check (and clear the flag).
	// This allows the wave generator to detect interrupts even when frames continue.
	virtual bool checkAndClearPurgeFlag()=0;

	// Pure virtual, but still needs a definition.
	virtual ~FrameManager()=0;
};



// MSVC accepts `=0 {}` in-class, but GCC/Clang reject it.
// Keep the same ABI/intent while staying standard-compliant.
inline FrameManager::~FrameManager() {}

#endif

/*
TGSpeechBox — Speech wave generator header.

Originally part of the NV Speech Player project by NV Access Limited (2014).
Extended 2025-2026 by Tamas Geczy.
Licensed under GNU General Public License version 2.0.
*/

#ifndef TGSPEECHBOX_SPEECHWAVEGENERATOR_H
#define TGSPEECHBOX_SPEECHWAVEGENERATOR_H

#include "frame.h"
#include "waveGenerator.h"
#include "voicingTone.h"

class SpeechWaveGenerator: public WaveGenerator {
	public:
	static SpeechWaveGenerator* create(int sampleRate); 
	virtual void setFrameManager(FrameManager* frameManager)=0;
	
	/**
	 * Set voicing tone parameters for DSP-level voice quality adjustments.
	 * This is an optional API extension - if never called, defaults are used.
	 * 
	 * @param tone  Pointer to voicing tone parameters. If NULL, resets to defaults.
	 */
	virtual void setVoicingTone(const speechPlayer_voicingTone_t* tone)=0;
	
	/**
	 * Get current voicing tone parameters.
	 * 
	 * @param tone  Output pointer to receive current parameters.
	 */
	virtual void getVoicingTone(speechPlayer_voicingTone_t* tone)=0;
};

#endif

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

#ifndef SPEECHPLAYERSPEECHWAVEGENERATOR_H
#define SPEECHPLAYERSPEECHWAVEGENERATOR_H

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

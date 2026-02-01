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

#ifndef SPEECHPLAYER_H
#define SPEECHPLAYER_H

#include "frame.h"
#include "sample.h"
#include "voicingTone.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void* speechPlayer_handle_t;

/* ============================================================================
 * Core API (unchanged for ABI compatibility)
 * ============================================================================ */

speechPlayer_handle_t speechPlayer_initialize(int sampleRate);
void speechPlayer_queueFrame(speechPlayer_handle_t playerHandle, speechPlayer_frame_t* framePtr, unsigned int minFrameDuration, unsigned int fadeDuration, int userIndex, bool purgeQueue);
void speechPlayer_queueFrameEx(speechPlayer_handle_t playerHandle, speechPlayer_frame_t* framePtr, const speechPlayer_frameEx_t* frameExPtr, unsigned int frameExSize, unsigned int minFrameDuration, unsigned int fadeDuration, int userIndex, bool purgeQueue);
int speechPlayer_synthesize(speechPlayer_handle_t playerHandle, unsigned int sampleCount, sample* sampleBuf); 
int speechPlayer_getLastIndex(speechPlayer_handle_t playerHandle);
void speechPlayer_terminate(speechPlayer_handle_t playerHandle);

/* ============================================================================
 * Extended API (safe ABI extension - old drivers won't call these)
 * ============================================================================ */

/**
 * Set voicing tone parameters for DSP-level voice quality adjustments.
 * 
 * This is an optional API extension. Old drivers that never call this function
 * will get identical behavior to before (defaults are used).
 * 
 * New frontends/tools can call this to adjust:
 *   - Glottal pulse shape (crispness)
 *   - Voiced pre-emphasis (clarity)
 *   - High-shelf EQ (brightness)
 * 
 * @param playerHandle  Handle returned by speechPlayer_initialize()
 * @param tone          Pointer to voicing tone parameters, or NULL to reset to defaults
 */
void speechPlayer_setVoicingTone(speechPlayer_handle_t playerHandle, const speechPlayer_voicingTone_t* tone);

/**
 * Get current voicing tone parameters.
 * 
 * @param playerHandle  Handle returned by speechPlayer_initialize()
 * @param tone          Output pointer to receive current parameters
 */
void speechPlayer_getVoicingTone(speechPlayer_handle_t playerHandle, speechPlayer_voicingTone_t* tone);

/**
 * Get the DSP version implemented by this DLL.
 *
 * This is intended for frontends/drivers that want to detect whether a newer
 * DSP feature-set is available (or avoid calling APIs that would misbehave on
 * an older build).
 */
unsigned int speechPlayer_getDspVersion(void);

#ifdef __cplusplus
}
#endif

#endif

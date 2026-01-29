/*
This file is a part of the NV Speech Player project. 
URL: https://bitbucket.org/nvaccess/speechplayer
Copyright 2014-2025 NV Access Limited.
This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License version 2.0, as published by
the Free Software Foundation.
This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
This license can be found at:
http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
*/

#ifndef SPEECHPLAYER_VOICINGTONE_H
#define SPEECHPLAYER_VOICINGTONE_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Voicing tone parameters for DSP-level voice quality adjustments.
 * 
 * These parameters allow tuning the "brightness" and "crispness" of the
 * synthesized voice without modifying per-frame data or breaking ABI.
 * 
 * All fields have sensible defaults matching the original hardcoded behavior,
 * so existing drivers that never call the setter will sound identical.
 */
typedef struct {
    /**
     * Glottal pulse peak position (0.0 to 1.0, typically 0.85-0.95).
     * Higher values => faster closing portion => more high-frequency harmonic energy ("crisper").
     * Lower values => smoother, more muffled voice.
     * Default: 0.91
     */
    double voicingPeakPos;

    /**
     * Voiced-only pre-emphasis filter coefficient (0.0 to ~0.97).
     * Higher values => more high-frequency boost on voiced sounds.
     * Default: 0.92
     */
    double voicedPreEmphA;

    /**
     * Mix amount for voiced pre-emphasis (0.0 to 1.0).
     * 0.0 = no pre-emphasis, 1.0 = full pre-emphasis.
     * Default: 0.35
     */
    double voicedPreEmphMix;

    /**
     * High-shelf EQ gain in dB (can be negative for cut, positive for boost).
     * Applied to the final output for overall brightness adjustment.
     * Default: 4.0 dB
     */
    double highShelfGainDb;

    /**
     * High-shelf EQ corner frequency in Hz.
     * Frequencies above this are boosted/cut by highShelfGainDb.
     * Default: 2000.0 Hz
     */
    double highShelfFcHz;

    /**
     * High-shelf EQ Q factor (resonance/bandwidth).
     * Higher Q => sharper transition. Typical range: 0.5 to 2.0
     * Default: 0.7
     */
    double highShelfQ;

    /**
     * Spectral tilt applied to voiced signal, in dB per octave.
     * 
     * This provides a natural-sounding gradual roll-off that increases with
     * frequency, mimicking the natural harmonic decay of real glottal sources.
     * Unlike a lowpass filter (which has a sharp "knee"), tilt is smoother
     * and more voice-like.
     * 
     * Typical values:
     *   - Adult male:  -4 to -6 dB/oct (brighter, buzzier)
     *   - Female:      -7 to -10 dB/oct (smoother)
     *   - Child/soft:  -9 to -12 dB/oct (very smooth/muffled)
     *   - 0 dB/oct:    No tilt (brightest, most synthetic)
     * 
     * Negative values = darker/smoother (normal for speech)
     * Positive values = brighter (unusual, may sound harsh)
     * 
     * Default: 0.0 (no additional tilt, preserves original DSP behavior)
     */
    double voicedTiltDbPerOct;

} speechPlayer_voicingTone_t;

/**
 * Default values matching the original hardcoded constants.
 * Use this to initialize a voicingTone struct before modifying specific fields.
 */
#define SPEECHPLAYER_VOICINGTONE_DEFAULTS { \
    0.91,   /* voicingPeakPos */ \
    0.92,   /* voicedPreEmphA */ \
    0.35,   /* voicedPreEmphMix */ \
    4.0,    /* highShelfGainDb */ \
    2000.0, /* highShelfFcHz */ \
    0.7,    /* highShelfQ */ \
    0.0     /* voicedTiltDbPerOct (no tilt by default) */ \
}

/**
 * Helper function to get default voicing tone parameters.
 * Useful for C code or when the macro isn't convenient.
 */
static inline speechPlayer_voicingTone_t speechPlayer_getDefaultVoicingTone(void) {
    speechPlayer_voicingTone_t tone = SPEECHPLAYER_VOICINGTONE_DEFAULTS;
    return tone;
}

#ifdef __cplusplus
}
#endif

#endif /* SPEECHPLAYER_VOICINGTONE_H */

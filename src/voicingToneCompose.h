/*
TGSpeechBox — Composing a voice profile's voice source with listener settings.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#ifndef TGSPEECHBOX_VOICINGTONECOMPOSE_H
#define TGSPEECHBOX_VOICINGTONECOMPOSE_H

#include "voicingTone.h"

/*
 * The contract (3.10 beta 10): a voice profile's stored voicingTone is the
 * base, with the frontend's defaults for keys it leaves out
 * (nvspFrontend_getVoicingTone).  Each listener setting at its neutral
 * position leaves the base value alone; a moved setting composes with it,
 * so returning the setting to neutral restores the profile's value.
 *
 * The listener values are the absolute values a host's slider maps to, the
 * same numbers hosts pass for a built-in voice.  At the neutral positions
 * they are: tilt offset 0, noise modulation 0, pitch-sync 0 / 0, speed
 * quotient 2.0, aspiration tilt 0, cascade bandwidth 1.0, tremor 0, nasal
 * bandwidth 1.0, F4 (head size) 1.0, nasal gain 1.0.
 *
 *   additive:       tilt, noise modulation, pitch-sync F1/B1, aspiration tilt, tremor
 *   multiplicative: speed quotient (/2.0), cascade bandwidth, nasal bandwidth,
 *                   F4 frequency scale, nasal gain
 *
 * Chorus is a listener-only setting (profiles do not carry it) and is set
 * by the host as before.  Built-in voices without a profile keep the hosts'
 * absolute mapping; only a profile with its own voicingTone block composes.
 * The NVDA driver implements the same rule in voicing_tone.py.
 */

static inline double speechPlayer_composeClamp(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline void speechPlayer_composeListenerSettings(
    speechPlayer_voicingTone_t* tone,
    double voicedTiltOffsetDbPerOct,
    double noiseGlottalModDepth,
    double pitchSyncF1DeltaHz,
    double pitchSyncB1DeltaHz,
    double speedQuotient,
    double aspirationTiltDbPerOct,
    double cascadeBwScale,
    double tremorDepth,
    double nasalBwScale,
    double f4FreqScale,
    double nasalGainScale)
{
    if (!tone) return;
    tone->voicedTiltDbPerOct = speechPlayer_composeClamp(tone->voicedTiltDbPerOct + voicedTiltOffsetDbPerOct, -24.0, 24.0);
    tone->noiseGlottalModDepth = speechPlayer_composeClamp(tone->noiseGlottalModDepth + noiseGlottalModDepth, 0.0, 1.0);
    tone->pitchSyncF1DeltaHz += pitchSyncF1DeltaHz;
    tone->pitchSyncB1DeltaHz += pitchSyncB1DeltaHz;
    tone->speedQuotient = speechPlayer_composeClamp(tone->speedQuotient * (speedQuotient / 2.0), 0.5, 4.0);
    tone->aspirationTiltDbPerOct = speechPlayer_composeClamp(tone->aspirationTiltDbPerOct + aspirationTiltDbPerOct, -24.0, 24.0);
    tone->cascadeBwScale = speechPlayer_composeClamp(tone->cascadeBwScale * cascadeBwScale, 0.3, 2.0);
    tone->tremorDepth = speechPlayer_composeClamp(tone->tremorDepth + tremorDepth, 0.0, 0.5);
    tone->nasalBwScale = speechPlayer_composeClamp(tone->nasalBwScale * nasalBwScale, 0.25, 4.0);
    tone->f4FreqScale = speechPlayer_composeClamp(tone->f4FreqScale * f4FreqScale, 0.7, 1.5);
    tone->nasalGainScale = speechPlayer_composeClamp(tone->nasalGainScale * nasalGainScale, 0.25, 4.0);
}

#endif /* TGSPEECHBOX_VOICINGTONECOMPOSE_H */

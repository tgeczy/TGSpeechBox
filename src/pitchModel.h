/*
TGSpeechBox — Fujisaki-Bartman pitch contour model for the DSP.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#ifndef TGSPEECHBOX_PITCHMODEL_H
#define TGSPEECHBOX_PITCHMODEL_H

#include "dspCommon.h"

class FujisakiBartmanPitch {
private:
    // Filter coefficients
    double pa, pb, pc;
    double aa, ab, ac;

    // Past output samples
    double px1, px2;
    double ax1, ax2;

    // Impulse variables
    double phr;
    double acc;
    int countdown;

    // Defaults (scaled for sample rate to preserve timing in seconds)
    int defaultPhraseLen;
    int defaultAccentLen;
    int defaultAccentDur;

    static inline int clampInt(int v, int lo, int hi) {
        if (v < lo) return lo;
        if (v > hi) return hi;
        return v;
    }

    void designPhrase(int l) {
        // l is length to reach the peak (in samples)
        if (l < 1) l = 1;
        const double nf = -1.0 / (double)l;
        const double r = exp(nf);
        const double c = -(r * r);
        const double b = 2.0 * r;
        const double p = exp(exp(1.0) * nf); // gain compensation
        const double a = 1.0 - b * p - c * p;
        pa = a; pb = b; pc = c;
    }

    void designAccent(int l) {
        // l is length to reach the peak (in samples)
        if (l < 1) l = 1;
        const double nf = -1.0 / (double)l;
        const double r = exp(nf);
        const double c = -(r * r);
        const double b = 2.0 * r;
        const double a = 1.0 - b - c;
        aa = a; ab = b; ac = c;
    }

public:
    explicit FujisakiBartmanPitch(int sampleRate)
        : pa(0.0), pb(0.0), pc(0.0), aa(0.0), ab(0.0), ac(0.0),
          px1(0.0), px2(0.0), ax1(0.0), ax2(0.0),
          phr(0.0), acc(0.0), countdown(0),
          defaultPhraseLen(4250), defaultAccentLen(1024), defaultAccentDur(7500) {

        // Reference values come from a DECTalk-style model tuned around 22050 Hz.
        // Scale defaults so that the same *time* (seconds) is preserved across SR.
        const double refSr = 22050.0;
        if (sampleRate > 0) {
            const double scale = (double)sampleRate / refSr;
            defaultPhraseLen = (int)floor(0.5 + 4250.0 * scale);
            defaultAccentLen = (int)floor(0.5 + 1024.0 * scale);
            defaultAccentDur = (int)floor(0.5 + 7500.0 * scale);
        }
        // Keep in sane ranges.
        defaultPhraseLen = clampInt(defaultPhraseLen, 1, 200000);
        defaultAccentLen = clampInt(defaultAccentLen, 1, 200000);
        defaultAccentDur = clampInt(defaultAccentDur, 1, 200000);

        designPhrase(defaultPhraseLen);
        designAccent(defaultAccentLen);
    }

    void resetPast() {
        px1 = 0.0; px2 = 0.0;
        ax1 = 0.0; ax2 = 0.0;
        phr = 0.0;
        acc = 0.0;
        countdown = 0;
    }

    void phrase(double a, int phraseLenSamples) {
        // Trigger phrase command (one-sample impulse)
        if (!(a > 0.0)) return;
        phr = a;
        if (phraseLenSamples > 0) {
            designPhrase(phraseLenSamples);
        }
    }

    void accent(double a, int durationSamples, int accentLenSamples) {
        // Trigger accent command (rectangular pulse)
        if (!(a > 0.0)) return;
        acc = a;
        countdown = (durationSamples > 0) ? durationSamples : defaultAccentDur;
        if (accentLenSamples > 0) {
            designAccent(accentLenSamples);
        }
    }

    double processMultiplier() {
        // Phrase command
        const double y1 = pa * phr + pb * px1 + pc * px2;
        px2 = px1;
        px1 = y1;
        phr = 0.0;

        // Accent command
        double aimp = 0.0;
        if (countdown > 0) {
            aimp = acc;
            countdown -= 1;
        }
        const double y2 = aa * aimp + ab * ax1 + ac * ax2;
        ax2 = ax1;
        ax1 = y2;

        // Multiplier = exp(y1 + y2)
        const double e = y1 + y2;
        // Avoid overflow/inf if someone feeds insane command magnitudes.
        const double eClamped = clampDouble(e, -24.0, 24.0);
        return exp(eClamped);
    }
};

#endif // TGSPEECHBOX_PITCHMODEL_H

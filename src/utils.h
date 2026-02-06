/*
TGSpeechBox — Shared utility functions and math helpers.

Originally part of the NV Speech Player project by NV Access Limited (2014).
Extended 2025-2026 by Tamas Geczy.
Licensed under GNU General Public License version 2.0.
*/

#ifndef TGSPEECHBOX_UTILS_H
#define TGSPEECHBOX_UTILS_H

// MSVC requires this before <cmath> to define M_PI, M_E, etc.
#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <cmath>

// Fallback: if M_PI still isn't defined (e.g. <cmath> was included
// earlier without _USE_MATH_DEFINES), define it directly.
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// _isnan is MSVC-specific. For Linux/Android use std::isnan.
#if defined(_MSC_VER)
  #include <float.h>
  inline bool nvsp_isnan(double v) { return _isnan(v) != 0; }
#else
  inline bool nvsp_isnan(double v) { return std::isnan(v); }
#endif

inline double calculateValueAtFadePosition(double oldVal, double newVal, double curFadeRatio) {
	if(nvsp_isnan(newVal)) return oldVal;
	return oldVal + ((newVal - oldVal) * curFadeRatio);
}

// Cosine ease-in/ease-out: maps linear [0,1] to an S-curve.
// Eliminates the abrupt start/stop of linear fades, mimicking how
// articulators physically accelerate and decelerate.
inline double cosineSmooth(double t) {
	return 0.5 * (1.0 - cos(M_PI * t));
}

// Log-domain interpolation for frequency parameters.
// Frequencies are perceptually logarithmic — a 300→2400 Hz sweep should
// pass through ~849 Hz at midpoint (geometric mean), not 1350 Hz (arithmetic).
// Falls back to linear for zero/negative values (e.g. disabled formants).
inline double calculateFreqAtFadePosition(double oldVal, double newVal, double curFadeRatio) {
	if(nvsp_isnan(newVal)) return oldVal;
	if(oldVal <= 0.0 || newVal <= 0.0) {
		return oldVal + ((newVal - oldVal) * curFadeRatio);
	}
	double logOld = log(oldVal);
	double logNew = log(newVal);
	return exp(logOld + (logNew - logOld) * curFadeRatio);
}

#endif
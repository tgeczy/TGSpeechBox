// DSP v9: frameEx.endVoiceAmplitude ramps voiceAmplitude across a frame.
//
// A finite end target makes the frame manager walk voiceAmplitude linearly
// from the frame's start value to the target over minNumSamples, with the
// same bookkeeping endVoicePitch has had since v1.  NAN (the default) holds
// the amplitude flat, so every existing caller renders bit-identically, and
// an older caller that passes a FrameEx too short to contain the field gets
// the NAN default padded in and stays flat too.

#include "doctest.h"

#include "speechPlayer.h"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

speechPlayer_frame_t makeVoicedFrame(double amp) {
	speechPlayer_frame_t f;
	memset(&f, 0, sizeof(f));
	f.voicePitch = 110.0;
	f.endVoicePitch = 110.0;
	f.glottalOpenQuotient = 0.5;
	f.voiceAmplitude = amp;
	f.cf1 = 500.0;  f.cb1 = 80.0;
	f.cf2 = 1500.0; f.cb2 = 90.0;
	f.cf3 = 2500.0; f.cb3 = 150.0;
	f.cf4 = 3300.0; f.cb4 = 250.0;
	f.cf5 = 3750.0; f.cb5 = 200.0;
	f.cf6 = 4900.0; f.cb6 = 1000.0;
	f.preFormantGain = 1.0;
	f.outputGain = 1.0;
	return f;
}

std::vector<sample> drain(speechPlayer_handle_t h) {
	std::vector<sample> out;
	sample buf[4096];
	for (int call = 0; call < 400; ++call) {
		int n = speechPlayer_synthesize(h, 4096, buf);
		if (n <= 0) break;
		out.insert(out.end(), buf, buf + n);
	}
	return out;
}

// RMS level (dB re full scale) of the window [fromMs, toMs).
double rmsDb(const std::vector<sample>& pcm, int sr, double fromMs, double toMs) {
	size_t a = (size_t)(fromMs * sr / 1000.0);
	size_t b = (size_t)(toMs * sr / 1000.0);
	if (b > pcm.size()) b = pcm.size();
	if (a >= b) return -200.0;
	double acc = 0.0;
	for (size_t i = a; i < b; ++i) {
		double v = pcm[i].value / 32768.0;
		acc += v * v;
	}
	return 10.0 * log10(acc / (double)(b - a) + 1e-20);
}

// One 500 ms voiced frame (start amplitude 0.6) followed by 100 ms of
// silence.  frameExSize lets a test pretend to be an older caller.
std::vector<sample> renderOne(int sr, double endVa, unsigned int frameExSize) {
	speechPlayer_handle_t h = speechPlayer_initialize(sr);
	REQUIRE(h);
	speechPlayer_frame_t f = makeVoicedFrame(0.6);
	speechPlayer_frameEx_t fx = speechPlayer_frameEx_defaults;
	fx.endVoiceAmplitude = endVa;
	speechPlayer_queueFrameEx(h, &f, &fx, frameExSize, (unsigned)(sr / 2), 22, -1, false);
	speechPlayer_queueFrameEx(h, NULL, NULL, 0, (unsigned)(sr / 10), 22, -1, false);
	std::vector<sample> pcm = drain(h);
	speechPlayer_terminate(h);
	return pcm;
}

}  // namespace

TEST_CASE("endVoiceAmplitude: a finite target ramps the voice linearly across the frame") {
	const int sr = 22050;
	// 0.6 -> 0.15 over 500 ms: amp(t) = 0.6 - 0.45 * t / 500.
	// Window centres 85 ms (0.5235) and 465 ms (0.1815): 9.2 dB apart.
	std::vector<sample> pcm = renderOne(sr, 0.15, sizeof(speechPlayer_frameEx_t));
	REQUIRE(pcm.size() > (size_t)(sr / 2));
	const double early = rmsDb(pcm, sr, 60.0, 110.0);
	const double late = rmsDb(pcm, sr, 440.0, 490.0);
	const double expected = 20.0 * log10(0.5235 / 0.1815);
	INFO("early " << early << " dB, late " << late << " dB, drop " << (early - late)
	     << " dB, expected " << expected);
	CHECK(early > -40.0);              // the voice is there
	CHECK(fabs((early - late) - expected) < 2.0);
	// Monotonic: the middle sits between the ends.
	const double mid = rmsDb(pcm, sr, 250.0, 300.0);
	CHECK(mid < early);
	CHECK(mid > late);
}

TEST_CASE("endVoiceAmplitude: NAN holds the amplitude flat") {
	const int sr = 22050;
	std::vector<sample> pcm = renderOne(sr, NAN, sizeof(speechPlayer_frameEx_t));
	const double early = rmsDb(pcm, sr, 60.0, 110.0);
	const double late = rmsDb(pcm, sr, 440.0, 490.0);
	INFO("early " << early << " dB, late " << late << " dB");
	CHECK(early > -40.0);
	CHECK(fabs(early - late) < 0.5);
}

TEST_CASE("endVoiceAmplitude: an older caller with a shorter FrameEx stays flat") {
	const int sr = 22050;
	// Pass a struct that ends just before the new field; the DLL pads the
	// rest with speechPlayer_frameEx_defaults, where the field is NAN.
	const unsigned int oldSize = (unsigned int)offsetof(speechPlayer_frameEx_t, endVoiceAmplitude);
	std::vector<sample> pcm = renderOne(sr, 0.0, oldSize);  // 0.0 would be a ramp to silence if it were read
	const double early = rmsDb(pcm, sr, 60.0, 110.0);
	const double late = rmsDb(pcm, sr, 440.0, 490.0);
	INFO("early " << early << " dB, late " << late << " dB");
	CHECK(early > -40.0);
	CHECK(fabs(early - late) < 0.5);
}

TEST_CASE("amplitudeOnsetMs: the voice glides in from the previous frame's level instead of stepping") {
	const int sr = 22050;
	// A quiet frame (0.15, 200 ms) then a loud frame (0.6, 300 ms) with and
	// without a 30 ms onset glide.  Without it the loud frame is at full level
	// a few ms in; with it the first 10 ms sit well below the level at 60+ ms.
	auto run = [&](double onsetMs) {
		speechPlayer_handle_t h = speechPlayer_initialize(sr);
		REQUIRE(h);
		speechPlayer_frame_t a = makeVoicedFrame(0.15);
		speechPlayer_frame_t b = makeVoicedFrame(0.6);
		speechPlayer_frameEx_t fx = speechPlayer_frameEx_defaults;
		speechPlayer_queueFrameEx(h, &a, &fx, sizeof(fx), (unsigned)(sr / 5), 22, -1, false);
		fx.amplitudeOnsetMs = onsetMs;
		speechPlayer_queueFrameEx(h, &b, &fx, sizeof(fx), (unsigned)(sr * 3 / 10), 22, -1, false);
		speechPlayer_queueFrameEx(h, NULL, NULL, 0, (unsigned)(sr / 10), 22, -1, false);
		std::vector<sample> pcm = drain(h);
		speechPlayer_terminate(h);
		return pcm;
	};
	// Frame B is playing from ~205 ms.  Windows are two pitch periods
	// (18 ms at 110 Hz) so the pulse ripple averages out.
	const double bStart = 208.0;  // ms
	std::vector<sample> step = run(0.0);
	std::vector<sample> glide = run(30.0);
	const double stepEarly = rmsDb(step, sr, bStart, bStart + 18.0);
	const double stepLate = rmsDb(step, sr, bStart + 60.0, bStart + 120.0);
	const double glideEarly = rmsDb(glide, sr, bStart, bStart + 18.0);
	const double glideMid = rmsDb(glide, sr, bStart + 18.0, bStart + 36.0);
	const double glideLate = rmsDb(glide, sr, bStart + 60.0, bStart + 120.0);
	INFO("step early " << stepEarly << " late " << stepLate << " | glide early " << glideEarly
	     << " mid " << glideMid << " late " << glideLate);
	CHECK(fabs(stepEarly - stepLate) < 2.5);       // legacy: at level almost at once
	CHECK(glideEarly < stepEarly - 3.0);           // glide: still well below early on
	CHECK(glideMid > glideEarly);                  // ...and climbing
	CHECK(glideMid < glideLate + 0.5);
	CHECK(fabs(glideLate - stepLate) < 0.5);       // same destination
	// The quiet frame before it is unaffected.
	CHECK(fabs(rmsDb(step, sr, 100.0, 180.0) - rmsDb(glide, sr, 100.0, 180.0)) < 0.2);
}

TEST_CASE("endVoiceAmplitude: the ramp never drives the source negative") {
	const int sr = 22050;
	// Target far below zero: the ramp clamps at 0 and the tail is silent,
	// not a re-growing inverted voice.
	std::vector<sample> pcm = renderOne(sr, -2.0, sizeof(speechPlayer_frameEx_t));
	const double early = rmsDb(pcm, sr, 60.0, 110.0);
	const double late = rmsDb(pcm, sr, 440.0, 490.0);
	INFO("early " << early << " dB, late " << late << " dB");
	CHECK(early > -40.0);
	// The line reaches 0 at ~115 ms and stays there: the tail is the
	// synthesizer's silent floor, ~20 dB under the flat render's tail.
	CHECK(late < early - 15.0);
	CHECK(late < -45.0);
}

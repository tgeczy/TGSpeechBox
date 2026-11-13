// A frame's pitch ramp ends on endVoicePitch even when its crossfade is
// longer than the frame (#222).
//
// The per-sample pitch slope is (endVoicePitch - voicePitch) / duration.  When
// a frame starts, its crossfade target is moved along that slope by the
// length of the crossfade, and the hold phase covers the rest of the frame.
// With a crossfade longer than the frame there is no hold phase, and the
// target went past endVoicePitch: en-us "a way" has an 8 ms frame from 121.2
// to 111.1 Hz with a 22.5 ms crossfade, which fell to about 93 Hz before the
// next frame started again at 121.2.  The voice amplitude ramp
// (frameEx.endVoiceAmplitude) used the same arithmetic.
//
// These tests step the frame manager one sample at a time and read the pitch
// and amplitude the DSP is handed, at 11025, 16000, 22050 and 44100 Hz and at
// speeds 0.5, 1.0, 1.6, 2.0 and 3.0.

#include "doctest.h"
#include "pack_fixture.h"

#include "frame.h"
#include "nvspFrontend.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

const int kSampleRates[] = {11025, 16000, 22050, 44100};
const double kSpeeds[] = {0.5, 1.0, 1.6, 2.0, 3.0};

unsigned int msToSamples(double ms, int sr) {
	const double s = std::ceil(ms * (double)sr / 1000.0);
	return (unsigned int)std::max(1.0, s);
}

speechPlayer_frame_t voicedFrame(double pitch, double endPitch, double amp) {
	speechPlayer_frame_t f;
	memset(&f, 0, sizeof(f));
	f.voicePitch = pitch;
	f.endVoicePitch = endPitch;
	f.glottalOpenQuotient = 0.5;
	f.voiceAmplitude = amp;
	f.cf1 = 500.0;  f.cb1 = 80.0;
	f.cf2 = 1500.0; f.cb2 = 90.0;
	f.cf3 = 2500.0; f.cb3 = 150.0;
	f.preFormantGain = 1.0;
	f.outputGain = 1.0;
	return f;
}

// What the DSP was handed, one entry per sample, while frames were playing.
struct Track {
	std::vector<double> pitch;
	std::vector<double> amp;
	std::vector<int> newest;  // index of the newest frame that has started
};

Track play(FrameManager& fm) {
	Track t;
	for (int i = 0; i < 2000000; ++i) {
		const speechPlayer_frame_t* f = fm.getCurrentFrame();
		if (!f) {
			if (!t.pitch.empty()) break;
			continue;
		}
		t.pitch.push_back(f->voicePitch);
		t.amp.push_back(f->voiceAmplitude);
		t.newest.push_back(fm.getLastIndex());
	}
	return t;
}

// lead (steady at the ramp's start), the short ramp, tail (steady at its end).
Track playShortRamp(int sr, double speed, double fromHz, double toHz,
                    double durMs, double fadeMs) {
	std::unique_ptr<FrameManager> fm(FrameManager::create(sr));
	speechPlayer_frame_t lead = voicedFrame(fromHz, fromHz, 0.8);
	speechPlayer_frame_t ramp = voicedFrame(fromHz, toHz, 0.8);
	speechPlayer_frame_t tail = voicedFrame(toHz, toHz, 0.8);
	fm->queueFrame(&lead, msToSamples(60.0 / speed, sr), msToSamples(10.0 / speed, sr), 0, false);
	fm->queueFrame(&ramp, msToSamples(durMs / speed, sr), msToSamples(fadeMs / speed, sr), 1, false);
	fm->queueFrame(&tail, msToSamples(60.0 / speed, sr), msToSamples(10.0 / speed, sr), 2, false);
	return play(*fm);
}

struct CapturedFrame {
	bool silence;
	speechPlayer_frame_t frame;
	speechPlayer_frameEx_t frameEx;
	double durationMs;
	double fadeMs;
};

void captureCallback(void* userData, const nvspFrontend_Frame* f, const nvspFrontend_FrameEx* fEx,
                     double durationMs, double fadeMs, int /*userIndex*/) {
	auto* out = static_cast<std::vector<CapturedFrame>*>(userData);
	CapturedFrame c;
	c.silence = (f == nullptr);
	memset(&c.frame, 0, sizeof(c.frame));
	c.frameEx = speechPlayer_frameEx_defaults;
	if (f) memcpy(&c.frame, f, sizeof(c.frame));
	if (fEx) memcpy(&c.frameEx, fEx, sizeof(c.frameEx));
	c.durationMs = durationMs;
	c.fadeMs = fadeMs;
	out->push_back(c);
}

} // namespace

TEST_CASE("#222: a falling pitch ramp with a crossfade longer than its frame stops at its end pitch") {
	for (int sr : kSampleRates) {
		for (double speed : kSpeeds) {
			CAPTURE(sr);
			CAPTURE(speed);
			// 120 -> 110 Hz in 8 ms, 24 ms crossfade: fell to about 90 Hz.
			const Track t = playShortRamp(sr, speed, 120.0, 110.0, 8.0, 24.0);
			REQUIRE(!t.pitch.empty());
			const double lowest = *std::min_element(t.pitch.begin(), t.pitch.end());
			const double highest = *std::max_element(t.pitch.begin(), t.pitch.end());
			CHECK(lowest >= 110.0 - 0.01);
			CHECK(highest <= 120.0 + 0.01);
			CHECK(t.pitch.back() == doctest::Approx(110.0).epsilon(1e-6));
		}
	}
}

TEST_CASE("#222: a rising pitch ramp with a crossfade longer than its frame stops at its end pitch") {
	for (int sr : kSampleRates) {
		for (double speed : kSpeeds) {
			CAPTURE(sr);
			CAPTURE(speed);
			const Track t = playShortRamp(sr, speed, 110.0, 125.0, 6.0, 30.0);
			REQUIRE(!t.pitch.empty());
			CHECK(*std::max_element(t.pitch.begin(), t.pitch.end()) <= 125.0 + 0.01);
			CHECK(*std::min_element(t.pitch.begin(), t.pitch.end()) >= 110.0 - 0.01);
		}
	}
}

TEST_CASE("#222: a pitch ramp whose crossfade fits in its frame runs as before") {
	for (int sr : kSampleRates) {
		for (double speed : kSpeeds) {
			CAPTURE(sr);
			CAPTURE(speed);
			const unsigned int durN = msToSamples(40.0 / speed, sr);
			const unsigned int fadeN = msToSamples(10.0 / speed, sr);
			REQUIRE(fadeN <= durN);
			const Track t = playShortRamp(sr, speed, 120.0, 110.0, 40.0, 10.0);
			// Where frame 1 starts, where its crossfade ends, where it ends.
			const size_t start = (size_t)(std::find(t.newest.begin(), t.newest.end(), 1) - t.newest.begin());
			const size_t next = (size_t)(std::find(t.newest.begin(), t.newest.end(), 2) - t.newest.begin());
			REQUIRE(next < t.newest.size());
			REQUIRE(next - start == (size_t)durN + 1);
			// The crossfade lands on the line, fadeN samples along it...
			CHECK(t.pitch[start + fadeN] == doctest::Approx(120.0 - 10.0 * (double)fadeN / (double)durN).epsilon(1e-9));
			// ...and the hold phase walks the rest of it: its last sample
			// is one step short of the end pitch, as it always was.
			CHECK(t.pitch[next - 1] == doctest::Approx(120.0 - 10.0 * (double)(durN - 1) / (double)durN).epsilon(1e-9));
			CHECK(*std::min_element(t.pitch.begin(), t.pitch.end()) >= 110.0 - 1e-6);
		}
	}
}

TEST_CASE("#222: a voice amplitude ramp with a crossfade longer than its frame stops at its end amplitude") {
	for (int sr : kSampleRates) {
		for (double speed : kSpeeds) {
			CAPTURE(sr);
			CAPTURE(speed);
			std::unique_ptr<FrameManager> fm(FrameManager::create(sr));
			speechPlayer_frame_t lead = voicedFrame(110.0, 110.0, 0.8);
			speechPlayer_frame_t ramp = voicedFrame(110.0, 110.0, 0.8);
			speechPlayer_frame_t tail = voicedFrame(110.0, 110.0, 0.4);
			speechPlayer_frameEx_t flat = speechPlayer_frameEx_defaults;
			speechPlayer_frameEx_t fall = speechPlayer_frameEx_defaults;
			fall.endVoiceAmplitude = 0.4;
			fm->queueFrameEx(&lead, &flat, sizeof(flat), msToSamples(60.0 / speed, sr), msToSamples(10.0 / speed, sr), 0, false);
			// 0.8 -> 0.4 in 8 ms, 24 ms crossfade: the target went to 0.8 - 1.2,
			// clamped to 0, so the voice faded toward silence.
			fm->queueFrameEx(&ramp, &fall, sizeof(fall), msToSamples(8.0 / speed, sr), msToSamples(24.0 / speed, sr), 1, false);
			fm->queueFrameEx(&tail, &flat, sizeof(flat), msToSamples(60.0 / speed, sr), msToSamples(10.0 / speed, sr), 2, false);
			const Track t = play(*fm);
			const size_t start = (size_t)(std::find(t.newest.begin(), t.newest.end(), 1) - t.newest.begin());
			REQUIRE(start < t.amp.size());
			const double lowest = *std::min_element(t.amp.begin() + (std::ptrdiff_t)start, t.amp.end());
			CHECK(lowest >= 0.4 - 1e-6);
			CHECK(t.amp.back() == doctest::Approx(0.4).epsilon(1e-6));
		}
	}
}

// Real frames: at the end of every frame's crossfade the pitch lies between
// that frame's own start and end pitch.
TEST_CASE("#222: in English and Hungarian words no crossfade ends outside its frame's pitch range") {
	struct Item { const char* lang; const char* ipa; };
	const Item items[] = {
		{"en-us", "ɐ wˈeɪ"},
		{"en-us", "həlˈoʊ wˈɜːld"},
		{"en-gb", "ɐ wˈeɪ"},
		{"en-gb", "haʊ nˈaʊ bɹˈaʊn kˈaʊ"},
		{"hu", "ˈaː viːz tˈyːrøː"},
	};
	int shortFramesWithRamp = 0;
	for (const Item& item : items) {
		tgsb_test::HandleFixture fx(item.lang);
		REQUIRE(fx.handle);
		for (double speed : kSpeeds) {
			std::vector<CapturedFrame> frames;
			REQUIRE(nvspFrontend_queueIPA_Ex(fx.handle, item.ipa, speed, 110.0, 0.5, ".", 0,
			                                 &captureCallback, &frames) != 0);
			REQUIRE(!frames.empty());
			for (int sr : kSampleRates) {
				CAPTURE(std::string(item.lang));
				CAPTURE(std::string(item.ipa));
				CAPTURE(speed);
				CAPTURE(sr);
				std::unique_ptr<FrameManager> fm(FrameManager::create(sr));
				std::vector<unsigned int> fadeN(frames.size());
				for (size_t i = 0; i < frames.size(); ++i) {
					CapturedFrame& c = frames[i];
					const unsigned int durN = msToSamples(c.durationMs, sr);
					fadeN[i] = msToSamples(c.fadeMs, sr);
					if (!c.silence && fadeN[i] > durN && c.frame.endVoicePitch != c.frame.voicePitch)
						++shortFramesWithRamp;
					fm->queueFrameEx(c.silence ? NULL : &c.frame, c.silence ? NULL : &c.frameEx,
					                 c.silence ? 0u : (unsigned int)sizeof(c.frameEx),
					                 durN, fadeN[i], (int)i, false);
				}
				const Track t = play(*fm);
				double worstHz = 0.0;
				int worstFrame = -1;
				for (size_t s = 1; s < t.newest.size(); ++s) {
					if (t.newest[s] == t.newest[s - 1]) continue;
					const size_t i = (size_t)t.newest[s];
					if (frames[i].silence) continue;
					const size_t end = s + fadeN[i];  // the crossfade's last sample
					if (end >= t.pitch.size()) continue;
					const double lo = std::min(frames[i].frame.voicePitch, frames[i].frame.endVoicePitch);
					const double hi = std::max(frames[i].frame.voicePitch, frames[i].frame.endVoicePitch);
					const double over = std::max(lo - t.pitch[end], t.pitch[end] - hi);
					if (over > worstHz) { worstHz = over; worstFrame = (int)i; }
				}
				CAPTURE(worstFrame);
				CHECK(worstHz <= 0.01);
			}
		}
	}
	// The test means nothing unless such frames exist.
	CHECK(shortFramesWithRamp > 0);
}

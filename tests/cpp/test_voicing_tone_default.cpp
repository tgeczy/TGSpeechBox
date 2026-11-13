// A new player reports the vowel bandwidth it runs (#224).
//
// The cascade starts at bandwidth scale 1.0, and the constructor never sent
// the default tone's cascadeBwScale (0.9) to it, so a new player ran 1.0 and
// reported 0.9: reading the tone and setting it straight back changed the
// sound.  A new player now reports 1.0.
//
// What must stay as it was: the audio.  A new player still runs 1.0, and a
// host that starts from SPEECHPLAYER_VOICINGTONE_DEFAULTS and sets it (SAPI
// with the sharpness setting unset, Android and iOS before a user tone) still
// runs that macro's 0.9, as do a NULL tone, the legacy 7-double layout and a
// struct too short to hold the field.

#include "doctest.h"

#include "speechPlayer.h"

#include <cstddef>
#include <cstring>
#include <functional>
#include <vector>

namespace {

speechPlayer_frame_t makeVowelFrame() {
	speechPlayer_frame_t f;
	memset(&f, 0, sizeof(f));
	f.voicePitch = 110.0;
	f.endVoicePitch = 110.0;
	f.glottalOpenQuotient = 0.5;
	f.voiceAmplitude = 0.8;
	f.cf1 = 700.0;  f.cb1 = 80.0;
	f.cf2 = 1200.0; f.cb2 = 90.0;
	f.cf3 = 2600.0; f.cb3 = 150.0;
	f.cf4 = 3300.0; f.cb4 = 250.0;
	f.cf5 = 3750.0; f.cb5 = 200.0;
	f.cf6 = 4900.0; f.cb6 = 1000.0;
	f.preFormantGain = 1.0;
	f.outputGain = 1.0;
	return f;
}

// 300 ms of one vowel, then silence, from a new player that `setup` may
// change before anything is queued.
std::vector<short> render(int sr, const std::function<void(speechPlayer_handle_t)>& setup) {
	speechPlayer_handle_t h = speechPlayer_initialize(sr);
	REQUIRE(h);
	if (setup) setup(h);
	speechPlayer_frame_t f = makeVowelFrame();
	speechPlayer_queueFrame(h, &f, (unsigned)(sr * 3 / 10), (unsigned)(sr / 100), -1, false);
	speechPlayer_queueFrame(h, NULL, (unsigned)(sr / 10), (unsigned)(sr / 100), -1, false);
	std::vector<short> out;
	sample buf[4096];
	for (int call = 0; call < 400; ++call) {
		int n = speechPlayer_synthesize(h, 4096, buf);
		if (n <= 0) break;
		for (int i = 0; i < n; ++i) out.push_back(buf[i].value);
	}
	speechPlayer_terminate(h);
	return out;
}

// The player's tone, read the way a current caller reads it (with the header;
// without it the DSP answers in the legacy 7-double layout).
speechPlayer_voicingTone_t readTone(speechPlayer_handle_t h) {
	speechPlayer_voicingTone_t t = speechPlayer_getDefaultVoicingTone();
	t.cascadeBwScale = -1.0;  // must be overwritten by the read
	speechPlayer_getVoicingTone(h, &t);
	return t;
}

void setBw(speechPlayer_handle_t h, double bw) {
	speechPlayer_voicingTone_t t = readTone(h);
	t.cascadeBwScale = bw;
	speechPlayer_setVoicingTone(h, &t);
}

const int kRates[] = {11025, 16000, 22050, 44100};

} // namespace

TEST_CASE("#224: a new player reports the cascade bandwidth it runs") {
	speechPlayer_handle_t h = speechPlayer_initialize(22050);
	REQUIRE(h);
	const speechPlayer_voicingTone_t t = readTone(h);
	speechPlayer_terminate(h);
	CHECK(t.cascadeBwScale == doctest::Approx(1.0));
	// Every other setting is still the documented default.
	const speechPlayer_voicingTone_t d = speechPlayer_getDefaultVoicingTone();
	CHECK(t.voicingPeakPos == d.voicingPeakPos);
	CHECK(t.voicedPreEmphA == d.voicedPreEmphA);
	CHECK(t.voicedPreEmphMix == d.voicedPreEmphMix);
	CHECK(t.highShelfGainDb == d.highShelfGainDb);
	CHECK(t.highShelfFcHz == d.highShelfFcHz);
	CHECK(t.highShelfQ == d.highShelfQ);
	CHECK(t.voicedTiltDbPerOct == d.voicedTiltDbPerOct);
	CHECK(t.noiseGlottalModDepth == d.noiseGlottalModDepth);
	CHECK(t.pitchSyncF1DeltaHz == d.pitchSyncF1DeltaHz);
	CHECK(t.pitchSyncB1DeltaHz == d.pitchSyncB1DeltaHz);
	CHECK(t.speedQuotient == d.speedQuotient);
	CHECK(t.aspirationTiltDbPerOct == d.aspirationTiltDbPerOct);
	CHECK(t.tremorDepth == d.tremorDepth);
	CHECK(t.nasalBwScale == d.nasalBwScale);
	CHECK(t.f4FreqScale == d.f4FreqScale);
	CHECK(t.nasalGainScale == d.nasalGainScale);
	CHECK(t.chorusDepth == d.chorusDepth);
	CHECK(t.chorusDetuneHz == d.chorusDetuneHz);
}

TEST_CASE("#224: reading the tone and setting it straight back changes no sample") {
	for (int sr : kRates) {
		CAPTURE(sr);
		const std::vector<short> fresh = render(sr, nullptr);
		const std::vector<short> roundTrip = render(sr, [](speechPlayer_handle_t h) {
			speechPlayer_voicingTone_t t = readTone(h);
			speechPlayer_setVoicingTone(h, &t);
		});
		REQUIRE(fresh.size() > (size_t)(sr / 4));
		CHECK(fresh == roundTrip);
	}
}

TEST_CASE("#224: a new player still runs bandwidth scale 1.0") {
	for (int sr : kRates) {
		CAPTURE(sr);
		const std::vector<short> fresh = render(sr, nullptr);
		CHECK(fresh == render(sr, [](speechPlayer_handle_t h) { setBw(h, 1.0); }));
		CHECK(fresh != render(sr, [](speechPlayer_handle_t h) { setBw(h, 0.9); }));
	}
}

TEST_CASE("#224: a host that sets the defaults macro still runs and reports its 0.9") {
	REQUIRE(speechPlayer_getDefaultVoicingTone().cascadeBwScale == doctest::Approx(0.9));
	{
		speechPlayer_handle_t h = speechPlayer_initialize(22050);
		REQUIRE(h);
		speechPlayer_voicingTone_t d = speechPlayer_getDefaultVoicingTone();
		speechPlayer_setVoicingTone(h, &d);
		CHECK(readTone(h).cascadeBwScale == doctest::Approx(0.9));
		speechPlayer_terminate(h);
	}
	for (int sr : kRates) {
		CAPTURE(sr);
		const std::vector<short> explicit09 = render(sr, [](speechPlayer_handle_t h) { setBw(h, 0.9); });
		// The macro, as SAPI, Android and iOS start from it.
		CHECK(explicit09 == render(sr, [](speechPlayer_handle_t h) {
			speechPlayer_voicingTone_t d = speechPlayer_getDefaultVoicingTone();
			speechPlayer_setVoicingTone(h, &d);
		}));
		// A NULL tone resets to the macro.
		CHECK(explicit09 == render(sr, [](speechPlayer_handle_t h) {
			speechPlayer_setVoicingTone(h, NULL);
		}));
		// The legacy layout: 7 doubles and no header.
		CHECK(explicit09 == render(sr, [](speechPlayer_handle_t h) {
			const speechPlayer_voicingTone_t d = speechPlayer_getDefaultVoicingTone();
			double legacy[32] = {d.voicingPeakPos, d.voicedPreEmphA, d.voicedPreEmphMix,
			                     d.highShelfGainDb, d.highShelfFcHz, d.highShelfQ,
			                     d.voicedTiltDbPerOct};
			speechPlayer_setVoicingTone(h, (const speechPlayer_voicingTone_t*)legacy);
		}));
		// An older caller whose struct ends before cascadeBwScale.
		CHECK(explicit09 == render(sr, [](speechPlayer_handle_t h) {
			speechPlayer_voicingTone_t d = speechPlayer_getDefaultVoicingTone();
			d.cascadeBwScale = 1.7;  // beyond structSize: must not be read
			d.structSize = (uint32_t)offsetof(speechPlayer_voicingTone_t, cascadeBwScale);
			speechPlayer_setVoicingTone(h, &d);
		}));
	}
}

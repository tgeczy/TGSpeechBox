// A voice profile's voice source through the shared composition rule
// (src/voicingToneCompose.h, used by SAPI, Android and iOS; the NVDA driver
// mirrors it and has its own pytest).  With the real frontend and packs:
// Beth's stored voicingTone, composed with neutral listener settings, comes
// out exactly as stored; a moved setting follows the rule; the frontend's
// fallbacks fill keys a profile leaves out.
#include <cmath>
#include <string>

#include "doctest.h"
#include "pack_fixture.h"
#include "nvspFrontend.h"
#include "voicingToneCompose.h"

namespace {

struct EnUsHandle : tgsb_test::HandleFixture {
    EnUsHandle() : tgsb_test::HandleFixture("en-us") {}
};

speechPlayer_voicingTone_t profileBase(const nvspFrontend_VoicingTone& pt) {
    speechPlayer_voicingTone_t t = speechPlayer_getDefaultVoicingTone();
    t.voicingPeakPos = pt.voicingPeakPos;
    t.voicedPreEmphA = pt.voicedPreEmphA;
    t.voicedPreEmphMix = pt.voicedPreEmphMix;
    t.highShelfGainDb = pt.highShelfGainDb;
    t.highShelfFcHz = pt.highShelfFcHz;
    t.highShelfQ = pt.highShelfQ;
    t.voicedTiltDbPerOct = pt.voicedTiltDbPerOct;
    t.noiseGlottalModDepth = pt.noiseGlottalModDepth;
    t.pitchSyncF1DeltaHz = pt.pitchSyncF1DeltaHz;
    t.pitchSyncB1DeltaHz = pt.pitchSyncB1DeltaHz;
    t.speedQuotient = pt.speedQuotient;
    t.aspirationTiltDbPerOct = pt.aspirationTiltDbPerOct;
    t.cascadeBwScale = pt.cascadeBwScale;
    t.tremorDepth = pt.tremorDepth;
    t.nasalBwScale = pt.nasalBwScale;
    t.f4FreqScale = pt.f4FreqScale;
    t.nasalGainScale = pt.nasalGainScale;
    return t;
}

void composeNeutral(speechPlayer_voicingTone_t& t) {
    speechPlayer_composeListenerSettings(&t, 0.0, 0.0, 0.0, 0.0, 2.0, 0.0, 1.0, 0.0, 1.0, 1.0, 1.0);
}

}  // namespace

TEST_CASE_FIXTURE(EnUsHandle, "profile tone: Beth's stored voice source survives neutral listener settings") {
    REQUIRE(nvspFrontend_setVoiceProfile(handle, "Beth"));
    nvspFrontend_VoicingTone pt{};
    REQUIRE(nvspFrontend_getVoicingTone(handle, &pt) == 1);
    // packs/phonemes.yaml, Beth's voicingTone
    CHECK(pt.voicedTiltDbPerOct == doctest::Approx(-10.0));
    CHECK(pt.speedQuotient == doctest::Approx(1.60));
    CHECK(pt.f4FreqScale == doctest::Approx(1.08));
    CHECK(pt.nasalBwScale == doctest::Approx(1.30));
    CHECK(pt.nasalGainScale == doctest::Approx(1.10));
    CHECK(pt.highShelfGainDb == doctest::Approx(5.5));  // left out -> the frontend's block fallback

    speechPlayer_voicingTone_t t = profileBase(pt);
    composeNeutral(t);
    CHECK(t.voicedTiltDbPerOct == doctest::Approx(-10.0));
    CHECK(t.speedQuotient == doctest::Approx(1.60));
    CHECK(t.f4FreqScale == doctest::Approx(1.08));
    CHECK(t.nasalBwScale == doctest::Approx(1.30));
    CHECK(t.nasalGainScale == doctest::Approx(1.10));
    CHECK(t.cascadeBwScale == doctest::Approx(1.0));
    CHECK(t.highShelfGainDb == doctest::Approx(5.5));
}

TEST_CASE_FIXTURE(EnUsHandle, "profile tone: Bobby's head size and speed quotient survive neutral settings") {
    REQUIRE(nvspFrontend_setVoiceProfile(handle, "Bobby"));
    nvspFrontend_VoicingTone pt{};
    REQUIRE(nvspFrontend_getVoicingTone(handle, &pt) == 1);
    speechPlayer_voicingTone_t t = profileBase(pt);
    composeNeutral(t);
    CHECK(t.f4FreqScale == doctest::Approx(1.20));
    CHECK(t.speedQuotient == doctest::Approx(2.20));
    CHECK(t.highShelfGainDb == doctest::Approx(2.0));
    CHECK(t.voicingPeakPos == doctest::Approx(0.88));
}

TEST_CASE("profile tone: moved settings compose, neutral restores") {
    speechPlayer_voicingTone_t base = speechPlayer_getDefaultVoicingTone();
    base.voicedTiltDbPerOct = -10.0;
    base.speedQuotient = 1.6;
    base.f4FreqScale = 1.08;
    base.noiseGlottalModDepth = 0.3;
    base.cascadeBwScale = 0.8;

    speechPlayer_voicingTone_t t = base;
    // tilt +4.8, speed quotient slider 25 (1.25), head size 75 (0.925), noise +0.2, sharpness 75 (0.65)
    speechPlayer_composeListenerSettings(&t, 4.8, 0.2, 0.0, 0.0, 1.25, 0.0, 0.65, 0.0, 1.0, 0.925, 1.0);
    CHECK(t.voicedTiltDbPerOct == doctest::Approx(-5.2));
    CHECK(t.speedQuotient == doctest::Approx(1.0));
    CHECK(t.f4FreqScale == doctest::Approx(0.999));
    CHECK(t.noiseGlottalModDepth == doctest::Approx(0.5));
    CHECK(t.cascadeBwScale == doctest::Approx(0.52));

    speechPlayer_voicingTone_t back = base;
    composeNeutral(back);
    CHECK(back.speedQuotient == doctest::Approx(1.6));
    CHECK(back.f4FreqScale == doctest::Approx(1.08));
    CHECK(back.voicedTiltDbPerOct == doctest::Approx(-10.0));

    // Clamps hold at the ends.
    speechPlayer_voicingTone_t hi = base;
    speechPlayer_composeListenerSettings(&hi, 40.0, 2.0, 0, 0, 4.0, 30.0, 2.0, 1.0, 5.0, 2.0, 5.0);  // -10 + 40 -> clamp 24
    CHECK(hi.voicedTiltDbPerOct == doctest::Approx(24.0));
    CHECK(hi.noiseGlottalModDepth == doctest::Approx(1.0));
    CHECK(hi.f4FreqScale == doctest::Approx(1.5));
    CHECK(hi.tremorDepth == doctest::Approx(0.5));
}

TEST_CASE_FIXTURE(EnUsHandle, "profile tone: a profile without a voicingTone block reports none") {
    // No profile: the hosts keep their built-in mapping.
    REQUIRE(nvspFrontend_setVoiceProfile(handle, ""));
    nvspFrontend_VoicingTone pt{};
    CHECK(nvspFrontend_getVoicingTone(handle, &pt) == 0);
}

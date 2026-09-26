// Arató (BraiLab) pitch mode and the user's pitch (#136).
//
// Diego: raising the pitch flattens the Arató melody, lowering it widens the
// melody until the falls stop dead around 40 Hz; the other modes keep their
// shape.  The port added the PCF8200's pitch increments as fixed hertz to the
// base pitch, with a 40 Hz floor: the same steps are a smaller interval at a
// high pitch and a larger one at a low pitch.  TALKHUN's values are those of
// its own default pitch (103 Hz); the steps now scale with the base pitch
// from there, so the melody keeps its intervals, and at 103 Hz it is still
// the program's own, frame for frame.
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "doctest.h"
#include "pack_fixture.h"
#include "nvspFrontend.h"

namespace {

struct PitchFrame {
    double voicePitch, endVoicePitch, voiceAmplitude, durationMs;
};

void capturePitch(void* userData, const nvspFrontend_Frame* f,
                  const nvspFrontend_FrameEx* /*fEx*/,
                  double durationMs, double /*fadeMs*/, int /*userIndex*/) {
    if (!f) return;
    static_cast<std::vector<PitchFrame>*>(userData)->push_back(
        {f->voicePitch, f->endVoicePitch, f->voiceAmplitude, durationMs});
}

struct AratoFixture : tgsb_test::HandleFixture {
    AratoFixture() : tgsb_test::HandleFixture("hu") { set("legacyPitchMode", "arato_style"); }

    void set(const std::string& key, const std::string& value) {
        const std::string snippet = key + ": " + value + "\n";
        REQUIRE_MESSAGE(nvspFrontend_applySettingOverrides(handle, snippet.c_str()),
                        "applySettingOverrides '" << snippet << "' failed");
    }

    std::vector<PitchFrame> render(const char* ipa, const char* clause, double basePitch) {
        std::vector<PitchFrame> frames;
        REQUIRE(nvspFrontend_queueIPA_Ex(handle, ipa, 1.0, basePitch, 0.5, clause, 0,
                                         &capturePitch, &frames));
        return frames;
    }
};

// The melody's span over the voiced frames, in semitones, and its lowest pitch.
struct Span { double semitones, lowHz; };

Span span(const std::vector<PitchFrame>& frames) {
    double lo = 1e9, hi = 0.0;
    for (const auto& f : frames) {
        if (f.voiceAmplitude <= 0.05) continue;
        for (double p : {f.voicePitch, f.endVoicePitch}) {
            if (p <= 0.0) continue;
            lo = std::min(lo, p);
            hi = std::max(hi, p);
        }
    }
    REQUIRE(hi > 0.0);
    return {12.0 * std::log2(hi / lo), lo};
}

// A statement (declination and the final plunge) and a yes/no question (the
// rise), the two melodies with the widest spans.
const char* kStatement = "ɒ ˈkønyvɛt ˈtɒlaːltɒm ɒz ˈɒstɒlon";
const char* kQuestion = "ˈmɛɟ ɒ ˈvonɒt ˈholnɒp";

}  // namespace

TEST_CASE_FIXTURE(AratoFixture, "arato: the melody keeps its intervals at any pitch (#136)") {
    for (auto [ipa, clause] : {std::pair{kStatement, "."}, std::pair{kQuestion, "?"}}) {
        CAPTURE(clause);
        const Span low = span(render(ipa, clause, 70.0));
        const Span mid = span(render(ipa, clause, 110.0));
        const Span high = span(render(ipa, clause, 170.0));
        MESSAGE(clause << " span: " << low.semitones << " st at 70 Hz, " << mid.semitones
                       << " st at 110 Hz, " << high.semitones << " st at 170 Hz; lowest "
                       << low.lowHz << " / " << mid.lowHz << " / " << high.lowHz << " Hz");
        CHECK(std::fabs(low.semitones - mid.semitones) <= 0.75);
        CHECK(std::fabs(high.semitones - mid.semitones) <= 0.75);
        // A fall at a low pitch is the same interval, not stopped by a floor.
        CHECK(low.lowHz / 70.0 == doctest::Approx(mid.lowHz / 110.0).epsilon(0.05));
    }
}

TEST_CASE_FIXTURE(AratoFixture, "arato: at TALKHUN's own pitch the melody is the program's, unchanged") {
    // aratoReferencePitchHz 0 = the chip's fixed-hertz steps at any pitch.
    for (auto [ipa, clause] : {std::pair{kStatement, "."}, std::pair{kQuestion, "?"}}) {
        CAPTURE(clause);
        const auto scaled = render(ipa, clause, 103.0);
        set("aratoReferencePitchHz", "0");
        const auto chip = render(ipa, clause, 103.0);
        set("aratoReferencePitchHz", "103");
        REQUIRE(scaled.size() == chip.size());
        for (size_t i = 0; i < scaled.size(); ++i) {
            CAPTURE(i);
            CHECK(scaled[i].voicePitch == doctest::Approx(chip[i].voicePitch).epsilon(1e-9));
            CHECK(scaled[i].endVoicePitch == doctest::Approx(chip[i].endVoicePitch).epsilon(1e-9));
        }
    }
}

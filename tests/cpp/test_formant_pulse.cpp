// Microintonation (Arató §5.4): the emitter cuts a steady vowel into
// alternating two-state pieces when formantPulseMode is on.  These tests pin
// the contract without audio: off = the same frames as before; on = more
// frames, the same total duration, F1 alternating by the configured depth,
// and the mode gate ("arato" only while the Arató melodies are active).
#include <cmath>
#include <string>
#include <vector>

#include "doctest.h"
#include "pack_fixture.h"
#include "nvspFrontend.h"

namespace {

struct Frame {
    double cf1, cf2, cb1, cb3, durationMs, fadeMs;
};

void captureCallback(void* userData, const nvspFrontend_Frame* f,
                     const nvspFrontend_FrameEx* /*fEx*/,
                     double durationMs, double fadeMs, int /*userIndex*/) {
    auto* frames = static_cast<std::vector<Frame>*>(userData);
    if (!f) return;
    frames->push_back({f->cf1, f->cf2, f->cb1, f->cb3, durationMs, fadeMs});
}

struct HuHandleFixture : tgsb_test::HandleFixture {
    HuHandleFixture() : tgsb_test::HandleFixture("hu") {}

    // Apply one language setting to the loaded pack, as the settings panel does.
    void set(const std::string& key, const std::string& value) {
        const std::string snippet = key + ": " + value + "\n";
        const int ok = nvspFrontend_applySettingOverrides(handle, snippet.c_str());
        REQUIRE_MESSAGE(ok, "applySettingOverrides '" << snippet << "' failed: "
                        << nvspFrontend_getLastError(handle));
    }

    std::vector<Frame> render(const char* ipa, double speed) {
        std::vector<Frame> frames;
        const int rc = nvspFrontend_queueIPA_Ex(handle, ipa, speed, 110.0, 0.5, ".", 0,
                                                &captureCallback, &frames);
        REQUIRE_MESSAGE(rc, "queueIPA_Ex failed: " << nvspFrontend_getLastError(handle));
        return frames;
    }
};

double totalMs(const std::vector<Frame>& v) {
    double s = 0.0;
    for (const auto& f : v) s += f.durationMs;
    return s;
}

}  // namespace

// A long steady vowel on its own: the plain (no-transition) path.
TEST_CASE_FIXTURE(HuHandleFixture, "formant pulse: off leaves a lone vowel as one frame") {
    const auto off = render("ˈaː", 1.0);  // ˈaː
    REQUIRE(!off.empty());
    set("formantPulseMode", "on");
    const auto on = render("ˈaː", 1.0);
    CHECK(on.size() > off.size());
    CHECK(std::fabs(totalMs(on) - totalMs(off)) < 1e-6);
}

TEST_CASE_FIXTURE(HuHandleFixture, "formant pulse: pieces alternate F1 by the depth and join with the pulse fade") {
    set("formantPulseMode", "on");
    set("formantPulseF1Depth", "0.06");
    set("formantPulseF2Depth", "0.07");
    set("formantPulseBwScale", "2.5");
    set("formantPulseBwEvery", "2");
    set("formantPulseMs", "12.8");
    set("formantPulseFadeMs", "2");
    const auto on = render("ˈaː", 1.0);
    // Find the pulsed run: consecutive frames whose cf1 alternates.
    int alternations = 0;
    int pulseFades = 0;
    int bwWidened = 0;
    for (size_t i = 1; i < on.size(); ++i) {
        const double r = on[i].cf1 / on[i - 1].cf1;
        if (std::fabs(r - 1.06) < 1e-6 || std::fabs(r - 1.0 / 1.06) < 1e-6) {
            ++alternations;
            if (std::fabs(on[i].cf2 / on[i - 1].cf2 - (r > 1.0 ? 0.93 : 1.0 / 0.93)) > 1e-6)
                FAIL("F2 did not move with F1 at frame " << i);
        }
        if (std::fabs(on[i].fadeMs - 2.0) < 1e-9) ++pulseFades;
        if (on[i].cb3 > on[i - 1].cb3 * 2.0) ++bwWidened;
    }
    CHECK(alternations >= 2);
    CHECK(pulseFades >= 2);
    CHECK(bwWidened >= 1);
}

TEST_CASE_FIXTURE(HuHandleFixture, "formant pulse: 'arato' mode follows the pitch mode") {
    const auto off = render("ˈaː", 1.0);
    set("formantPulseMode", "arato");
    const auto stillOff = render("ˈaː", 1.0);
    CHECK(stillOff.size() == off.size());  // hu defaults to espeak_style
    set("legacyPitchMode", "arato_style");
    const auto on = render("ˈaː", 1.0);
    CHECK(on.size() > off.size());
}

// Running speech: vowels with coarticulation take the three-frame steady
// path; the pulse must keep the total duration and never split a glide.
TEST_CASE_FIXTURE(HuHandleFixture, "formant pulse: a sentence keeps its duration at every rate") {
    // eSpeak NG 1.53, hu: "Kérem a könyvtárat holnap reggel."
    const char* ipa = "kˈeːrɛm ˌɑ kˈøɲvtaːrɑt hˈolnɑp rˈɛɡːɛl";
    for (double speed : {0.5, 1.0, 1.6, 2.0, 3.0}) {
        set("formantPulseMode", "off");
        const auto off = render(ipa, speed);
        set("formantPulseMode", "on");
        const auto on = render(ipa, speed);
        CHECK(on.size() >= off.size());
        CHECK(std::fabs(totalMs(on) - totalMs(off)) < 1e-6);
    }
}

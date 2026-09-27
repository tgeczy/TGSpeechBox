// Chorus in a voice profile (#124, for Edu's Robot and the voices after it).
//
// The DSP has had a second, detuned glottal oscillator (voicingTone V5) since
// 3.0, but only the listener's sliders could set it: a profile's voicingTone
// block was read for 17 keys and chorus was not among them.  Now a profile
// may carry chorusDepth and chorusDetuneHz; the frontend reads them like any
// other voicingTone key, and speechPlayer_composeListenerChorus puts the
// listener's sliders on top (depth adds, detune moves by the slider's offset
// from neutral), so both sliders at neutral give the profile's chorus.
//
// Also pinned: nvspFrontend_getVoicingTone used to leave the two chorus
// fields of the caller's struct untouched (whatever was in memory); it fills
// them with the DSP's defaults when the profile doesn't set them.
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "doctest.h"
#include "pack_fixture.h"
#include "nvspFrontend.h"
#include "voicingToneCompose.h"

namespace {

namespace fs = std::filesystem;

// A copy of the repo's packs with two probe profiles added, one with chorus
// and one with a voicingTone block but no chorus.
struct ChorusPack {
    fs::path root;
    nvspFrontend_handle_t handle = nullptr;

    ChorusPack() {
        const fs::path repo = tgsb_test::findPackDir();
        REQUIRE_MESSAGE(!repo.empty(), "packs/ not found");
        root = fs::temp_directory_path() / "tgsb_chorus_pack";
        fs::remove_all(root);
        fs::create_directories(root);
        fs::copy(repo / "packs", root / "packs", fs::copy_options::recursive);

        const fs::path yaml = root / "packs" / "phonemes.yaml";
        std::ifstream in(yaml, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        std::string text = ss.str();
        in.close();
        const std::string anchor = "voiceProfiles:\n";
        const size_t at = text.find(anchor);
        REQUIRE(at != std::string::npos);
        text.insert(at + anchor.size(),
                    "  ChorusProbe:\n"
                    "    voicingTone:\n"
                    "      voicedTiltDbPerOct: -4.0\n"
                    "      chorusDepth: 0.35\n"
                    "      chorusDetuneHz: 3.0\n"
                    "  PlainProbe:\n"
                    "    voicingTone:\n"
                    "      voicedTiltDbPerOct: -4.0\n");
        std::ofstream out(yaml, std::ios::binary | std::ios::trunc);
        out << text;
        out.close();

        handle = nvspFrontend_create(root.string().c_str());
        REQUIRE(handle);
        REQUIRE(nvspFrontend_setLanguage(handle, "en-us"));
    }

    ~ChorusPack() {
        if (handle) nvspFrontend_destroy(handle);
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    nvspFrontend_VoicingTone toneOf(const char* profile, int* explicitBlock = nullptr) {
        REQUIRE(nvspFrontend_setVoiceProfile(handle, profile));
        nvspFrontend_VoicingTone t;
        std::memset(&t, 0x7f, sizeof t);  // garbage, as a caller's stack may hold
        const int r = nvspFrontend_getVoicingTone(handle, &t);
        if (explicitBlock) *explicitBlock = r;
        return t;
    }
};

speechPlayer_voicingTone_t fromProfile(const nvspFrontend_VoicingTone& pt) {
    speechPlayer_voicingTone_t t = speechPlayer_getDefaultVoicingTone();
    t.voicedTiltDbPerOct = pt.voicedTiltDbPerOct;
    t.chorusDepth = pt.chorusDepth;
    t.chorusDetuneHz = pt.chorusDetuneHz;
    return t;
}

}  // namespace

TEST_CASE("profile chorus: the frontend reads chorusDepth and chorusDetuneHz from a profile") {
    ChorusPack pack;
    int explicitBlock = 0;
    const auto t = pack.toneOf("ChorusProbe", &explicitBlock);
    CHECK(explicitBlock == 1);
    CHECK(t.chorusDepth == doctest::Approx(0.35));
    CHECK(t.chorusDetuneHz == doctest::Approx(3.0));
    CHECK(t.voicedTiltDbPerOct == doctest::Approx(-4.0));
}

TEST_CASE("profile chorus: without chorus keys the DSP's defaults come back, never garbage") {
    ChorusPack pack;
    const auto t = pack.toneOf("PlainProbe");
    CHECK(t.chorusDepth == 0.0);
    CHECK(t.chorusDetuneHz == doctest::Approx(2.0));
    // No profile at all: the same defaults.
    REQUIRE(nvspFrontend_setVoiceProfile(pack.handle, ""));
    nvspFrontend_VoicingTone none;
    std::memset(&none, 0x7f, sizeof none);
    CHECK(nvspFrontend_getVoicingTone(pack.handle, &none) == 0);
    CHECK(none.chorusDepth == 0.0);
    CHECK(none.chorusDetuneHz == doctest::Approx(2.0));
}

TEST_CASE("profile chorus: neutral listener sliders leave the profile's chorus as stored") {
    ChorusPack pack;
    auto tone = fromProfile(pack.toneOf("ChorusProbe"));
    speechPlayer_composeListenerChorus(&tone, 0.0, 0.0);
    CHECK(tone.chorusDepth == doctest::Approx(0.35));
    CHECK(tone.chorusDetuneHz == doctest::Approx(3.0));
}

TEST_CASE("profile chorus: moved listener sliders compose with the profile's chorus") {
    speechPlayer_voicingTone_t tone = speechPlayer_getDefaultVoicingTone();
    tone.chorusDepth = 0.35;
    tone.chorusDetuneHz = 3.0;
    speechPlayer_composeListenerChorus(&tone, 0.4, 1.2);
    CHECK(tone.chorusDepth == doctest::Approx(0.75));
    CHECK(tone.chorusDetuneHz == doctest::Approx(4.2));
    // Clamped to what the DSP accepts.
    speechPlayer_composeListenerChorus(&tone, 0.5, 3.0);
    CHECK(tone.chorusDepth == doctest::Approx(1.0));
    CHECK(tone.chorusDetuneHz == doctest::Approx(5.0));
}

TEST_CASE("profile chorus: a profile without chorus gives the listener's chorus unchanged") {
    ChorusPack pack;
    auto tone = fromProfile(pack.toneOf("PlainProbe"));
    speechPlayer_composeListenerChorus(&tone, 0.4, 0.9);
    CHECK(tone.chorusDepth == doctest::Approx(0.4));
    CHECK(tone.chorusDetuneHz == doctest::Approx(2.9));
}

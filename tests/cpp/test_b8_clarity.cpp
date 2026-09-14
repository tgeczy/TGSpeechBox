// b8 clarity fixes — regression pins.
//
// Four behaviors landed 2026-08-21 (issues #99, #108, #109, Vsevolod
// round 2).  Each test pins one of them the way it was measured when it
// was fixed: trace-aligned segment metrics, no ears required.
//
//   1. Croatian "uvod" (vowel-initial word, leading stress mark) rendered
//      all-zero PCM in the #99 audit.  Fixed as a side effect of the b7
//      timing work; pinned here so it stays dead.
//   2. Spanish word-initial /d/ (d_es) must open the following vowel with
//      an F2 onset above the vowel's steady state — the dental locus
//      transition (Celdrán locus equations; whisper heard "duro" as
//      "Bueno" without it).  Pins the getPlace() suffix fix + es.yaml
//      coarticulation enable together: if either regresses, F2 onset
//      collapses onto the vowel and this fails.
//   3. en-gb word-final voiced /d/ ("zed") must keep an audible release
//      after the b7 endCf ghost-guards — the voiced_word_final_release
//      pack rule.  Release-segment peak, trace-aligned.
//   4. en-gb word-final voiceless /t/ ("first") must keep its burst — the
//      en-us b7 retune ported to en-gb (fixA).

#include "doctest.h"
#include "audio_capture.h"
#include "pack_fixture.h"
#include "lpc.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using tgsb_test::HandleFixture;
using tgsb_test::synthesizeToPcmWithTrace;
using tgsb_test::readFrameTrace;

namespace {

struct HrHandleFixture : HandleFixture {
    HrHandleFixture() : HandleFixture("hr") {}
};
struct EnGbHandleFixture : HandleFixture {
    EnGbHandleFixture() : HandleFixture("en-gb") {}
};

double rmsOf(const std::vector<std::int16_t>& pcm) {
    if (pcm.empty()) return 0.0;
    double acc = 0.0;
    for (auto s : pcm) acc += double(s) * double(s);
    return std::sqrt(acc / double(pcm.size()));
}

double peakOf(const std::vector<std::int16_t>& pcm,
              std::size_t from, std::size_t to) {
    double peak = 0.0;
    to = std::min(to, pcm.size());
    for (std::size_t i = from; i < to; ++i)
        peak = std::max(peak, std::abs(double(pcm[i])));
    return peak;
}

// Sample range of the first trace entry whose key starts with `prefix`,
// scanning from the back when `fromEnd` is set (word-final segments).
struct SegRange { std::size_t begin = 0, end = 0; bool found = false; };
SegRange findSegment(nvspFrontend_handle_t h,
                     const tgsb_test::SynthesisResult& res,
                     const std::string& prefix, bool fromEnd) {
    SegRange out;
    auto trace = readFrameTrace(h);
    auto starts = [&](const std::string& key) {
        return key.rfind(prefix, 0) == 0;
    };
    int idx = -1;
    if (fromEnd) {
        for (int i = int(trace.size()) - 1; i >= 0; --i)
            if (starts(trace[std::size_t(i)].phonemeKey)) { idx = i; break; }
    } else {
        for (std::size_t i = 0; i < trace.size(); ++i)
            if (starts(trace[i].phonemeKey)) { idx = int(i); break; }
    }
    if (idx < 0) return out;
    const auto& e = trace[std::size_t(idx)];
    if (std::size_t(e.frameIndex) >= res.samplePositions.size()) return out;
    out.begin = res.samplePositions[std::size_t(e.frameIndex)];
    out.end = res.pcm.size();
    if (std::size_t(idx) + 1 < trace.size()) {
        const auto& n = trace[std::size_t(idx) + 1];
        if (std::size_t(n.frameIndex) < res.samplePositions.size())
            out.end = res.samplePositions[std::size_t(n.frameIndex)];
    }
    out.found = out.end > out.begin;
    return out;
}

double lpcF2At(const std::vector<std::int16_t>& pcm,
               std::size_t center, int sr) {
    auto r = tgsb_test::extractFormantsViaRoots(pcm, center, sr,
                                                /*windowLen*/ 512,
                                                /*lpcOrder*/ 24,
                                                /*maxBandwidthHz*/ 900.0);
    if (!r.valid || r.formants.size() < 2) return 0.0;
    // F2 = second formant in 500..2500 band for a back-vowel context.
    std::vector<double> fs;
    for (const auto& f : r.formants)
        if (f.freqHz > 200.0 && f.freqHz < 3000.0) fs.push_back(f.freqHz);
    return fs.size() >= 2 ? fs[1] : 0.0;
}

}  // namespace

TEST_CASE_FIXTURE(HrHandleFixture,
                  "hr: 'uvod' (vowel-initial + stress mark) is not silent") {
    auto res = synthesizeToPcmWithTrace(handle, "ˈuvod", 1.0, 110.0, 0.5, 22050);
    REQUIRE(!res.pcm.empty());
    // The #99 audit measured rms exactly 0.0 here; a healthy render sits
    // in the thousands.  200 is far above numeric noise, far below speech.
    CHECK(rmsOf(res.pcm) > 200.0);
}

TEST_CASE_FIXTURE(HandleFixture,
                  "es: word-initial d_es opens /u/ with a falling F2 (duro)") {
    auto res = synthesizeToPcmWithTrace(handle, "dˈuɾo", 1.0, 110.0, 0.5, 22050);
    REQUIRE(!res.pcm.empty());
    auto seg = findSegment(handle, res, "u", /*fromEnd*/ false);
    REQUIRE(seg.found);
    const std::size_t len = seg.end - seg.begin;
    REQUIRE(len > 1200);  // need room for onset + mid windows
    // F2 just after voicing onset vs steady mid-vowel.  With the locus
    // machinery dead (pre-fix) both windows measured the same ~780 Hz;
    // with it live the onset sits ≥60 Hz above the steady state.
    // Transition frames are noisy for root-finding; take the first window
    // near onset that yields a valid F2.
    double f2Onset = 0.0;
    for (std::size_t off : {std::size_t(300), std::size_t(420), std::size_t(540)}) {
        f2Onset = lpcF2At(res.pcm, seg.begin + off, res.sampleRate);
        if (f2Onset > 0.0) break;
    }
    const double f2Mid   = lpcF2At(res.pcm, seg.begin + len / 2, res.sampleRate);
    REQUIRE(f2Onset > 0.0);
    REQUIRE(f2Mid > 0.0);
    CHECK(f2Onset > f2Mid + 60.0);
}

TEST_CASE_FIXTURE(EnGbHandleFixture,
                  "en-gb: word-final voiced /d/ keeps an audible release (zed)") {
    auto res = synthesizeToPcmWithTrace(handle, "zˈɛd", 1.32, 110.0, 0.5, 22050);
    REQUIRE(!res.pcm.empty());
    auto seg = findSegment(handle, res, "d", /*fromEnd*/ true);
    REQUIRE(seg.found);
    const double wordPeak = peakOf(res.pcm, 0, res.pcm.size());
    REQUIRE(wordPeak > 0.0);
    const double relPeak = peakOf(res.pcm, seg.begin, res.pcm.size()) / wordPeak;
    // Measured 0.34–0.37 with the voiced_word_final_release rule at its
    // shipped values; the broken (guard-collateral) state halved the
    // release transient.  0.15 splits the two states with margin.
    CHECK(relPeak > 0.15);
}

TEST_CASE_FIXTURE(EnGbHandleFixture,
                  "en-gb: word-final /t/ burst survives (first)") {
    auto res = synthesizeToPcmWithTrace(handle, "fˈɜːst", 1.32, 110.0, 0.5, 22050);
    REQUIRE(!res.pcm.empty());
    auto seg = findSegment(handle, res, "t", /*fromEnd*/ true);
    REQUIRE(seg.found);
    const double wordPeak = peakOf(res.pcm, 0, res.pcm.size());
    REQUIRE(wordPeak > 0.0);
    const double relPeak = peakOf(res.pcm, seg.begin, res.pcm.size()) / wordPeak;
    // Pre-fixA the en-gb final alveolar burst sat ~9 dB under the en-us
    // target (-31 dB vs -22 dB); whisper couldn't hear "first" at NVDA
    // speed 60.  With the retune it transcribes at 1.00.
    CHECK(relPeak > 0.10);
}

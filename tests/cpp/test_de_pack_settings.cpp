// German: what follows the removed contextualRules block is still read (#176).
//
// de.yaml had a `contextualRules:` block in the middle of `settings:` that no
// code ever read (tests/test_pack_settings_are_read.py).  Taking 60 lines out
// of the middle of a yaml map is the kind of edit that can take the keys
// after it along, or leave them under the wrong parent.  These are the
// settings that came after the block, with the values de.yaml gives them;
// each differs from the frontend's default, so a key that stopped being read
// fails here.

#include "doctest.h"
#include "pack_fixture.h"

#include <algorithm>

namespace {
struct GermanPack : tgsb_test::PackFixture {
    GermanPack() : tgsb_test::PackFixture("de") {}
};
}  // namespace

TEST_CASE_FIXTURE(GermanPack, "de pack: the settings after the old contextualRules block are read (#176)") {
    const auto& lp = pack.lang;
    CHECK(lp.boundarySmoothingEnabled);
    CHECK(lp.trillModulationMs == doctest::Approx(40.0));
    CHECK(lp.singleWordTuningEnabled);
    CHECK(lp.singleWordFinalHoldMs == doctest::Approx(48.0));
    CHECK(lp.singleWordFinalFadeMs == doctest::Approx(18.0));
    CHECK(lp.microprosodyEnabled);
    CHECK(lp.microprosodyVoicedF0LowerHz == doctest::Approx(-5.0));
    CHECK(lp.thousandsSeparatorCommaToSpace);
    CHECK(std::find(lp.dictSuffixes.begin(), lp.dictSuffixes.end(), "chen") != lp.dictSuffixes.end());
}

TEST_CASE_FIXTURE(GermanPack, "de pack: the settings before the old contextualRules block are read (#176)") {
    const auto& lp = pack.lang;
    CHECK(lp.primaryStressDiv == doctest::Approx(1.30));
    CHECK(lp.stopClosureVowelGapMs == doctest::Approx(24.0));
    CHECK(lp.allophoneRulesEnabled);
}

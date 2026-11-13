// Every allophone rule a pack ships does something once it is loaded (#191).
//
// en-us had a rule, diphthong_offglide_sharpen_ɪ, whose fieldShifts was
// written as a map (cf1: -40, cf2: 150).  The loader reads fieldShifts as a
// list of {field, deltaHz | targetHz, blend} entries, as Tuning.md documents
// and every other rule writes it, so the rule loaded with no shifts and has
// changed nothing since it was added.  Its neighbour thought_pre_rhotic_close
// named two phonemes, ᴐː and ᵒː, that are not phoneme keys and could never
// match (ᴐ and ᵒ already cover the long vowels).
//
// These tests load every language pack with the real loader and look at the
// rules it ends up with: an action the allophone pass knows, something for
// that action to do, fields the frame has, and phoneme keys that exist.

#include "doctest.h"
#include "pack_fixture.h"
#include "utf8.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace {

std::vector<std::string> allLanguageTags() {
    namespace fs = std::filesystem;
    std::vector<std::string> tags;
    const fs::path langDir = fs::path(tgsb_test::findPackDir()) / "packs" / "lang";
    for (const auto& entry : fs::directory_iterator(langDir)) {
        if (entry.path().extension() == ".yaml") tags.push_back(entry.path().stem().string());
    }
    std::sort(tags.begin(), tags.end());
    return tags;
}

bool loadLanguage(const std::string& tag, nvsp_frontend::PackSet& pack) {
    std::string err;
    const bool ok = nvsp_frontend::loadPackSet(tgsb_test::findPackDir(), tag, pack, err);
    REQUIRE_MESSAGE(ok, "loadPackSet failed for '" << tag << "': " << err);
    return ok;
}

bool isKnownAction(const std::string& a) {
    return a == "replace" || a == "scale" || a == "shift" || a == "insert-before" || a == "insert-after";
}

}  // namespace

TEST_CASE("allophone rules: the packs are found, and some of them have rules") {
    // A loop over nothing would make every check below pass.
    const auto tags = allLanguageTags();
    CHECK(tags.size() >= 25);
    int rules = 0, shifts = 0;
    for (const auto& tag : tags) {
        nvsp_frontend::PackSet pack;
        loadLanguage(tag, pack);
        rules += static_cast<int>(pack.lang.allophoneRules.size());
        for (const auto& r : pack.lang.allophoneRules) shifts += (r.action == "shift");
    }
    CHECK(rules >= 40);
    CHECK(shifts > 10);
}

TEST_CASE("allophone rules: every rule has an action the pass knows and something for it to do (#191)") {
    for (const auto& tag : allLanguageTags()) {
        nvsp_frontend::PackSet pack;
        loadLanguage(tag, pack);
        for (const auto& r : pack.lang.allophoneRules) {
            CAPTURE(tag);
            CAPTURE(r.name);
            CHECK_MESSAGE(isKnownAction(r.action), "unknown action '" << r.action << "'");
            if (r.action == "shift") {
                // The en-us mistake: fieldShifts written as a map loads as no shifts.
                CHECK_MESSAGE(!r.fieldShifts.empty(), "a shift rule with no fieldShifts (is it a list of {field, deltaHz}?)");
            } else if (r.action == "scale") {
                CHECK_MESSAGE((!r.fieldScales.empty() || r.durationScale != 1.0 || r.fadeScale != 1.0),
                              "a scale rule that scales nothing");
            } else if (r.action == "insert-before" || r.action == "insert-after") {
                CHECK_MESSAGE(!r.insertPhoneme.empty(), "an insert rule with no insertPhoneme");
            }
            for (const auto& s : r.fieldShifts) {
                nvsp_frontend::FieldId fid;
                CHECK_MESSAGE(nvsp_frontend::parseFieldId(s.field, fid), "fieldShifts names no frame field: '" << s.field << "'");
            }
        }
    }
}

TEST_CASE("allophone rules: the phonemes a rule names are phoneme keys (#191)") {
    for (const auto& tag : allLanguageTags()) {
        nvsp_frontend::PackSet pack;
        loadLanguage(tag, pack);
        for (const auto& r : pack.lang.allophoneRules) {
            for (const auto& key : r.phonemes) {
                CAPTURE(tag);
                CAPTURE(r.name);
                CHECK_MESSAGE(pack.phonemes.count(key) == 1,
                              "'" << nvsp_frontend::u32ToUtf8(key) << "' is not a phoneme key, so the rule never matches it");
            }
        }
    }
}

TEST_CASE("allophone rules: en-us no longer carries the rule that did nothing (#191)") {
    nvsp_frontend::PackSet pack;
    loadLanguage("en-us", pack);
    bool closeRule = false;
    for (const auto& r : pack.lang.allophoneRules) {
        CHECK(r.name.find("diphthong_offglide_sharpen") == std::string::npos);
        if (r.name == "thought_pre_rhotic_close") {
            closeRule = true;
            // Still on ᴐ and ᵒ before r, with its four shifts.
            CHECK(r.phonemes.size() == 2);
            CHECK(r.fieldShifts.size() >= 4);
        }
    }
    CHECK(closeRule);
}

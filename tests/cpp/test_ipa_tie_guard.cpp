// A replacement rule never takes the second half out of a tied affricate,
// whether the match starts after the tie bar or on it (#179).
//
// A rule of two or more characters is matched with tie bars optional, so
// that "a͡ɪ" in a pack also matches "aɪ".  The text's tie bars are skipped
// before each character of the rule is compared, the first one included:
// the rule "ʃʃ" matched "͡ʃʃ" inside "t͡ʃʃ", starting on the tie bar.  The
// guard against splitting an affricate looked at the character before the
// match, found "t" there, and let it through; the tie bar went out with the
// match and Hungarian "bölcsség" (bˈølt͡ʃʃeːɡ) kept a plain t.
//
// These tests give normalizeIpaText() a pack with nothing in it but the one
// rule, so they pin the matcher and not a language.

#include "doctest.h"

#include "ipa_normalize.h"
#include "pack.h"
#include "utf8.h"

#include <string>

using nvsp_frontend::PackSet;
using nvsp_frontend::ReplacementRule;
using nvsp_frontend::u32ToUtf8;

// As UTF-8, so that a failure prints the text.
static std::string normalize(const PackSet& pack, const char* ipa) {
    return u32ToUtf8(nvsp_frontend::internal::normalizeIpaText(pack, ipa));
}

static PackSet packWithRule(const std::u32string& from, const std::u32string& to) {
    PackSet pack;
    ReplacementRule rule;
    rule.from = from;
    rule.to.push_back(to);
    pack.lang.replacements.push_back(rule);
    return pack;
}

TEST_CASE("tie guard: a rule does not start on an affricate's tie bar (#179)") {
    const PackSet pack = packWithRule(U"ʃʃ", U"ʃː");
    // cs + s: the affricate stays whole and the s after it stays its own.
    CHECK(normalize(pack, "bˈølt͡ʃʃeːɡ") == "bˈølt͡ʃʃeːɡ");
    CHECK(normalize(pack, "t͡ʃʃ") == "t͡ʃʃ");
    // The stress mark between does not hide the affricate either.
    CHECK(normalize(pack, "ɒt͡ʃˈʃɒ") == "ɒt͡ʃˈʃɒ");
}

TEST_CASE("tie guard: the rule still fires where no affricate is in the way (#179)") {
    const PackSet pack = packWithRule(U"ʃʃ", U"ʃː");
    CHECK(normalize(pack, "kˈiʃʃɛbː") == "kˈiʃːɛbː");
    // Two fricatives after the affricate's own: only those two merge.
    CHECK(normalize(pack, "ɒt͡ʃʃʃɒ") == "ɒt͡ʃʃːɒ");
    // A tie bar elsewhere in the text switches the loose match on; the
    // doubled fricative with no tie bar before it is still merged.
    CHECK(normalize(pack, "t͡ʃˈɒʃʃɒ") == "t͡ʃˈɒʃːɒ");
}

TEST_CASE("tie guard: every doubled fricative of an affricate is left alone (#179)") {
    CHECK(normalize(packWithRule(U"ss", U"sː"), "ɲˈolt͡ssor") == "ɲˈolt͡ssor");
    CHECK(normalize(packWithRule(U"ʒʒ", U"ʒː"), "ɒd͡ʒʒɒ") == "ɒd͡ʒʒɒ");
    CHECK(normalize(packWithRule(U"zz", U"zː"), "ɒd͡zzɒ") == "ɒd͡zzɒ");
}

TEST_CASE("tie guard: a match may still start on the tie bar after a vowel (#179)") {
    // en-us ties a vowel to its r (ɑː͡ɹ).  A rule that starts on that tie bar
    // is not splitting an affricate, and takes the tie bar with it as before.
    const PackSet pack = packWithRule(U"ɹɹ", U"ɹ");
    CHECK(normalize(pack, "stˈɑː͡ɹɹi") == "stˈɑːɹi");
    CHECK(normalize(pack, "ʊ͡ɹɹ") == "ʊɹ");
}

TEST_CASE("tie guard: a match may still start on the tie bar before a vowel (#179)") {
    // Danish: eSpeak NG ties the stød to its vowel (bˈʔ͡ɑj), and da.yaml's
    // ɑj -> aj has always matched from that tie bar.  The stød is not the
    // first half of an affricate.
    const PackSet pack = packWithRule(U"ɑj", U"aj");
    CHECK(normalize(pack, "bˈˀ͡ɑj") == "bˈˀaj");
    CHECK(normalize(pack, "fˀ͡ɑjlˈɑɡti") == "fˀajlˈɑɡti");
}

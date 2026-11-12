// Pronunciation dictionary entries match whatever the capitals (#164).
//
// Dictionary-editing.md says from_text matches "case-insensitive by default".
// The lowercase key was built byte by byte with std::tolower, which folds
// only A to Z: an entry for "ész" was used for "ész" but not for "Ész", so
// it was skipped for the first word of almost every sentence in Hungarian,
// German, Spanish, Russian and the rest.
//
// The same byte-wise fold follows the C locale.  NVDA's Python sets it to
// the Windows code page, where std::tolower rewrites the bytes of every
// non-ASCII letter: "ablakból" was garbled before it was looked up, so no
// suffix with an accent ever stripped (Hungarian "ból", "ért", "ról").

#include <clocale>
#include <string>
#include <unordered_map>
#include <vector>

#include "doctest.h"

#include "dict_lookup.h"
#include "pack.h"

namespace {

using nvsp_frontend::dictReplaceInText;
using nvsp_frontend::PronDict;

void add(PronDict& dict, const std::string& from, const std::string& toText, const std::string& toIpa = "") {
    nvsp_frontend::DictEntry e;
    e.fromText = from;
    e.toText = toText;
    e.toIpa = toIpa;
    dict.entries[from] = e;
}

PronDict accentedWords() {
    PronDict dict;
    add(dict, "\xC3\xA9sz", "ESZ_R");                                  // hu "ész"
    add(dict, "\xC3\xBC" "ber", "UBER_R");                             // de "über"
    add(dict, "\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82", "PRIVET_R");  // ru "привет"
    add(dict, "\xC3\xB1" "and\xC3\xBA", "NANDU_R");                    // es "ñandú"
    add(dict, "\xC5\x91sz", "OSZ_R");                                  // hu "ősz"
    add(dict, "ablak", "ABLAK_R");
    add(dict, "hello", "HELLO_R");
    return dict;
}

// Hungarian case endings, longest first, as packs/lang/hu.yaml lists them.
const std::vector<std::string> kHuSuffixes = {"b\xC3\xB3l", "\xC3\xA9rt", "r\xC3\xB3l", "ban"};

void checkAccentedWords() {
    const PronDict dict = accentedWords();
    // As written in the dictionary: always matched.
    CHECK(dictReplaceInText("\xC3\xA9sz", dict) == "ESZ_R");
    // Capitalised and all capitals.
    CHECK(dictReplaceInText("\xC3\x89sz n\xC3\xA9lk\xC3\xBCl", dict) == "ESZ_R n\xC3\xA9lk\xC3\xBCl");
    CHECK(dictReplaceInText("\xC3\x89SZ", dict) == "ESZ_R");
    CHECK(dictReplaceInText("\xC3\x9C" "ber alles", dict) == "UBER_R alles");
    CHECK(dictReplaceInText("\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82 \xD0\xBC\xD0\xB8\xD1\x80", dict) ==
          "PRIVET_R \xD0\xBC\xD0\xB8\xD1\x80");
    CHECK(dictReplaceInText("\xC3\x91" "and\xC3\xBA.", dict) == "NANDU_R.");
    CHECK(dictReplaceInText("\xC5\x90SZ van", dict) == "OSZ_R van");  // Ő, Latin Extended-A
    // Plain ASCII words are matched as they always were.
    CHECK(dictReplaceInText("Hello HELLO hello, world", dict) == "HELLO_R HELLO_R HELLO_R, world");
    // A suffix with an accent strips, whatever the capitals.
    CHECK(dictReplaceInText("ablakban", dict, kHuSuffixes) == "ABLAK_Rban");
    CHECK(dictReplaceInText("ablakb\xC3\xB3l", dict, kHuSuffixes) == "ABLAK_Rb\xC3\xB3l");
    CHECK(dictReplaceInText("ablak\xC3\xA9rt", dict, kHuSuffixes) == "ABLAK_R\xC3\xA9rt");
    CHECK(dictReplaceInText("Ablakr\xC3\xB3l n\xC3\xA9z", dict, kHuSuffixes) == "ABLAK_Rr\xC3\xB3l n\xC3\xA9z");
    CHECK(dictReplaceInText("ABLAKB\xC3\x93L", dict, kHuSuffixes) == "ABLAK_RB\xC3\x93L");
    // Each part of a hyphenated word is looked up the same way.
    CHECK(dictReplaceInText("j\xC3\xB3-\xC3\x89sz-f\xC3\xA9le", dict) == "j\xC3\xB3-ESZ_R-f\xC3\xA9le");
    // No entry: untouched, byte for byte.
    CHECK(dictReplaceInText("\xC3\x81rv\xC3\xADzt\xC5\xB1r\xC5\x91 T\xC3\x9CK\xC3\x96R", dict, kHuSuffixes) ==
          "\xC3\x81rv\xC3\xADzt\xC5\xB1r\xC5\x91 T\xC3\x9CK\xC3\x96R");
}

}  // namespace

TEST_CASE("dictionary: a capitalised accented or non-Latin word matches its entry (#164)") {
    checkAccentedWords();
}

TEST_CASE("dictionary: the same under the Windows code-page locale NVDA runs in (#164)") {
    // The test binary starts in the "C" locale, which hides the problem.
    const char* set = std::setlocale(LC_CTYPE, "English_United States.1252");
    if (!set) set = std::setlocale(LC_CTYPE, "en_US.ISO-8859-1");
    if (!set) {
        MESSAGE("no single-byte Latin locale on this system; not checked");
        return;
    }
    checkAccentedWords();
    std::setlocale(LC_CTYPE, "C");
}

TEST_CASE("dictionary: the exact case still wins over the lowercase entry (#164)") {
    PronDict dict;
    add(dict, "\xC3\x89sz", "PROPER");   // "Ész" as a name
    add(dict, "\xC3\xA9sz", "COMMON");   // "ész" the word
    add(dict, "Parton", "PROPER_EN");
    add(dict, "parton", "COMMON_EN");
    CHECK(dictReplaceInText("\xC3\x89sz", dict) == "PROPER");
    CHECK(dictReplaceInText("\xC3\xA9sz", dict) == "COMMON");
    CHECK(dictReplaceInText("\xC3\x89SZ", dict) == "COMMON");
    CHECK(dictReplaceInText("Parton parton PARTON", dict) == "PROPER_EN COMMON_EN COMMON_EN");
}

TEST_CASE("dictionary: a masked entry is skipped in either case (#164)") {
    PronDict dict;
    add(dict, "\xC3\xA9sz", "ESZ_R");
    dict.entries["\xC3\xA9sz"].masked = true;
    CHECK(dictReplaceInText("\xC3\x89sz \xC3\xA9sz", dict) == "\xC3\x89sz \xC3\xA9sz");
}

TEST_CASE("dictionary: an IPA entry is recorded under the lowercase word (#164)") {
    PronDict dict;
    add(dict, "am\xC3\xA9rica", "", "a m E r i k a");
    for (const char* locale : {"C", "English_United States.1252"}) {
        if (!std::setlocale(LC_CTYPE, locale)) continue;
        std::unordered_map<std::string, std::string> overrides;
        // The word stays in the text for the phonemizer; the IPA is spliced in later.
        CHECK(dictReplaceInText("a Am\xC3\xA9rica", dict, {}, &overrides) == "a Am\xC3\xA9rica");
        CHECK(overrides.count("am\xC3\xA9rica") == 1);
    }
    std::setlocale(LC_CTYPE, "C");
}

TEST_CASE("dictionary: a letter whose lowercase has fewer bytes keeps its suffix whole (#164)") {
    // Turkish capital dotted I (U+0130, two bytes) folds to "i" (one byte).
    PronDict dict;
    add(dict, "izmir", "IZMIR_R");
    add(dict, "\xC4\xB0stanbul", "ISTANBUL_R");  // entered with its capital
    const std::vector<std::string> suffixes = {"de", "da"};
    CHECK(dictReplaceInText("\xC4\xB0zmir", dict, suffixes) == "IZMIR_R");
    CHECK(dictReplaceInText("\xC4\xB0zmirde", dict, suffixes) == "IZMIR_Rde");
    CHECK(dictReplaceInText("\xC4\xB0stanbulda", dict, suffixes) == "ISTANBUL_Rda");  // as before
}

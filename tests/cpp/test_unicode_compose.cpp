// Decomposed accented text is composed on every platform (#169).
//
// "é" can arrive as one code point or as "e" followed by U+0301 (text from a
// Mac, input methods that type combining marks, Vietnamese).  The
// dictionaries are written with the one code point.  Windows and Apple
// normalize with the system's own NFKC; Android and Linux returned the text
// unchanged, so there the decomposed form missed the pronunciation, stress
// and letter dictionaries.  composeCanonical is the part of that
// normalization the frontend now carries itself: canonical composition for
// Latin, Greek and Cyrillic.
//
// The fixed examples run everywhere.  On Windows the function is also held,
// input by input, to the system's NFC over every code point of the covered
// blocks alone, followed by every combining mark, and under pairs of marks.

#include <cstdio>
#include <string>
#include <vector>

#include "doctest.h"

#include "utf8.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#pragma comment(lib, "normaliz.lib")
#endif

using nvsp_frontend::composeCanonical;

namespace {

std::string u8(std::u32string_view s) { return nvsp_frontend::u32ToUtf8(s); }

}  // namespace

TEST_CASE("compose: a Latin letter and its combining mark become one letter (#169)") {
    CHECK(composeCanonical("e\xCC\x81") == "\xC3\xA9");                    // é, the issue's example
    CHECK(composeCanonical(u8(U"café")) == u8(U"café"));
    CHECK(composeCanonical(u8(U"École")) == u8(U"École"));
    CHECK(composeCanonical(u8(U"mañana")) == u8(U"mañana"));    // es ñ
    CHECK(composeCanonical(u8(U"őű")) == u8(U"őű")); // hu ő ű
    CHECK(composeCanonical(u8(U"öü")) == u8(U"öü")); // ö ü
    CHECK(composeCanonical(u8(U"ão")) == u8(U"ão"));            // pt ão
    CHECK(composeCanonical(u8(U"ç")) == u8(U"ç"));              // ç (mark below)
    CHECK(composeCanonical(u8(U"čžš")) == u8(U"čžš"));  // cs č ž š
    CHECK(composeCanonical(u8(U"ąęż")) == u8(U"ąęż")); // pl ą ę ż
    CHECK(composeCanonical(u8(U"å")) == u8(U"å"));              // å
    CHECK(composeCanonical(u8(U"șț")) == u8(U"șț")); // ro ș ț
}

TEST_CASE("compose: two marks on one letter, in either order (#169)") {
    // Vietnamese ệ = e + dot below + circumflex.  The marks are of different
    // classes, so both orders are the same letter.
    CHECK(composeCanonical(u8(U"ệ")) == u8(U"ệ"));
    CHECK(composeCanonical(u8(U"ệ")) == u8(U"ệ"));
    CHECK(composeCanonical(u8(U"Việt")) == u8(U"Việt"));
    // ế = e + circumflex + acute: same class, the order is part of the letter.
    CHECK(composeCanonical(u8(U"ế")) == u8(U"ế"));
    CHECK(composeCanonical(u8(U"é̂")) == u8(U"é̂"));
    // A mark added to a letter that already carries one.
    CHECK(composeCanonical(u8(U"ế")) == u8(U"ế"));        // ê + acute
    CHECK(composeCanonical(u8(U"ẹ́")) == u8(U"ẹ́"));  // é + dot below
    // u + diaeresis + macron (pinyin ǖ).
    CHECK(composeCanonical(u8(U"ǖ")) == u8(U"ǖ"));
}

TEST_CASE("compose: Greek and Cyrillic (#169)") {
    CHECK(composeCanonical(u8(U"ά")) == u8(U"ά"));        // ά
    CHECK(composeCanonical(u8(U"ΐ")) == u8(U"ΐ"));  // ΐ
    CHECK(composeCanonical(u8(U"Ά")) == u8(U"Ά"));        // Ά
    CHECK(composeCanonical(u8(U"ώ")) == u8(U"ώ"));        // ώ
    CHECK(composeCanonical(u8(U"й")) == u8(U"й"));        // й
    CHECK(composeCanonical(u8(U"ё")) == u8(U"ё"));        // ё
    CHECK(composeCanonical(u8(U"Й")) == u8(U"Й"));        // Й
    CHECK(composeCanonical(u8(U"ї")) == u8(U"ї"));        // uk ї
    CHECK(composeCanonical(u8(U"ў")) == u8(U"ў"));        // be ў
    // The polytonic acute (oxia) is the same letter as the modern one (tonos).
    CHECK(composeCanonical(u8(U"ά")) == u8(U"ά"));
}

TEST_CASE("compose: what has nothing to compose comes back byte for byte (#169)") {
    const char* same[] = {
        "",
        "plain ASCII, with punctuation: 1,234.5!",
        "\xC3\xA9\xC3\xB1\xC5\x91\xC5\xB1 \xC3\x81rv\xC3\xADzt\xC5\xB1r\xC5\x91",  // precomposed Latin
        "\xCE\xAC\xCE\xBB\xCF\x86\xCE\xB1 \xD0\xB9\xD0\xBE\xD0\xB4",              // precomposed Greek, Cyrillic
        "\xE4\xBD\xA0\xE5\xA5\xBD\xEF\xBC\x8C\xE4\xB8\x96\xE7\x95\x8C\xEF\xBC\x81", // Chinese, fullwidth punctuation
        "\xED\x95\x9C\xEA\xB5\xAD\xEC\x96\xB4",                                    // Korean
        "\xD8\xA7\xD9\x84\xD8\xB9\xD9\x8E\xD8\xB1\xD9\x8E\xD8\xA8\xD9\x90\xD9\x8A\xD9\x91\xD8\xA9", // Arabic with vowel marks
        "\xF0\x9F\x98\x80\xEF\xB8\x8F \xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD",          // emoji, selector, skin tone
        // Compatibility forms are not this function's: the fi ligature,
        // fullwidth letters, superscript two, the ordinal indicators.
        "\xEF\xAC\x81n \xEF\xBC\xA1\xEF\xBC\xA2 m\xC2\xB2 1\xC2\xBA 2\xC2\xAA",
        "x\xCC\x81",          // x + acute: no such letter, the mark stays
        "\xCC\x81" "e",       // a mark with no letter before it
        "e \xCC\x81",         // a space is not a letter to put it on
    };
    for (const char* s : same) {
        CAPTURE(s);
        CHECK(composeCanonical(s) == s);
    }
    // A letter composes inside text of another script without touching it.
    CHECK(composeCanonical(u8(U"你好 é 한")) == u8(U"你好 é 한"));
}

TEST_CASE("normalizeNFKC composes a decomposed letter on every platform (#169)") {
    // The system's normalization on Windows and Apple; composeCanonical on
    // Android and Linux, where this was the identity.
    CHECK(nvsp_frontend::normalizeNFKC("e\xCC\x81") == "\xC3\xA9");
    CHECK(nvsp_frontend::normalizeNFKC(u8(U"árvíztűrő")) ==
          u8(U"árvíztűrő"));
    CHECK(nvsp_frontend::normalizeText(u8(U"й")) == u8(U"й"));
}

#ifdef _WIN32

namespace {

// The system's NFC (NormalizationC = 1), or false when it rejects the input.
bool systemNFC(const std::u32string& in, std::string& out) {
    std::wstring wide;
    for (char32_t c : in) {
        if (c >= 0x10000) {
            wide.push_back(static_cast<wchar_t>(0xD800 + ((c - 0x10000) >> 10)));
            wide.push_back(static_cast<wchar_t>(0xDC00 + ((c - 0x10000) & 0x3FF)));
        } else {
            wide.push_back(static_cast<wchar_t>(c));
        }
    }
    wchar_t buf[64];
    const int n = NormalizeString(NormalizationC, wide.c_str(), static_cast<int>(wide.size()), buf, 64);
    if (n <= 0) return false;
    char narrow[256];
    const int m = WideCharToMultiByte(CP_UTF8, 0, buf, n, narrow, 256, nullptr, nullptr);
    if (m <= 0) return false;
    out.assign(narrow, static_cast<size_t>(m));
    return true;
}

// The blocks the function covers: Basic Latin to Cyrillic Supplement, Latin
// Extended Additional, Greek Extended.
bool covered(char32_t c) {
    return (c >= 0x20 && c <= 0x52F) || (c >= 0x1E00 && c <= 0x1FFF);
}

struct Tally {
    int compared = 0;
    int changed = 0;   // inputs the system's NFC alters: the ones that fail while nothing composes
    int wrong = 0;
    std::string firstWrong;

    void check(const std::u32string& in) {
        std::string want;
        if (!systemNFC(in, want)) return;  // unassigned code point
        const std::string given = u8(in);
        const std::string got = composeCanonical(given);
        ++compared;
        if (want != given) ++changed;
        if (got != want && wrong++ == 0) {
            firstWrong = "input:";
            char hex[16];
            for (char32_t c : in) {
                snprintf(hex, sizeof(hex), " U+%04X", static_cast<unsigned>(c));
                firstWrong += hex;
            }
            firstWrong += "  got \"" + got + "\"  system NFC \"" + want + "\"";
        }
    }
};

std::vector<char32_t> combiningMarks() {
    std::vector<char32_t> marks;
    for (char32_t m = 0x300; m <= 0x36F; ++m) marks.push_back(m);
    for (char32_t m = 0x483; m <= 0x487; ++m) marks.push_back(m);
    return marks;
}

}  // namespace

TEST_CASE("compose: every covered code point alone is the system's NFC (#169)") {
    Tally t;
    for (char32_t c = 0x20; c <= 0x1FFF; ++c) {
        if (covered(c)) t.check(std::u32string(1, c));
    }
    INFO(t.firstWrong);
    CHECK(t.wrong == 0);
    CHECK(t.compared > 1700);
    CHECK(t.changed > 20);  // the Greek oxia letters and the other singletons
}

TEST_CASE("compose: every covered code point with every combining mark is the system's NFC (#169)") {
    Tally t;
    const std::vector<char32_t> marks = combiningMarks();
    for (char32_t c = 0x20; c <= 0x1FFF; ++c) {
        if (!covered(c)) continue;
        for (char32_t m : marks) t.check({c, m});
    }
    INFO(t.firstWrong);
    CHECK(t.wrong == 0);
    CHECK(t.compared > 200000);
    CHECK(t.changed > 1000);
}

TEST_CASE("compose: letters under two and three marks are the system's NFC (#169)") {
    Tally t;
    const std::vector<char32_t> marks = combiningMarks();
    // Bases that take stacked marks: Latin and Greek vowels, a few
    // consonants, Cyrillic, and letters that already carry a mark.
    const std::u32string bases = U"aeiouyAEOUcsnêôơưéẹ"
                                 U"αηιυωΑάἀ"
                                 U"иеуа";
    for (char32_t b : bases) {
        for (char32_t m1 : marks) {
            for (char32_t m2 : marks) t.check({b, m1, m2});
        }
    }
    // Three marks, from the ones that stack in Vietnamese and Greek.
    const std::u32string stack = U"̛̣̀́̂̃̄̆̈̉̓̔͂ͅ";
    for (char32_t b : std::u32string(U"aeouαηω")) {
        for (char32_t m1 : stack) {
            for (char32_t m2 : stack) {
                for (char32_t m3 : stack) t.check({b, m1, m2, m3});
            }
        }
    }
    // Two letters in a row, so that one letter's marks never reach the next.
    for (char32_t m1 : stack) {
        for (char32_t m2 : stack) t.check({U'a', m1, U'e', m2, U'o'});
    }
    INFO(t.firstWrong);
    CHECK(t.wrong == 0);
    CHECK(t.compared > 400000);
    CHECK(t.changed > 10000);
}

#endif  // _WIN32

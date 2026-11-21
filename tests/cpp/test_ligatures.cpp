// A typeset ligature is its letters.
//
// PDFs and e-books write "office" with one character for "ffi" (U+FB03).
// The phonemizer has no such letter: handed it, eSpeak reads "o, letter
// F B zero three, c e".  Windows and Apple never showed it, because the
// system's NFKC normalization turns the ligature into its letters before the
// frontend looks at the text.  Android and Linux have no such step, and the
// frontend's own (composeCanonical) leaves compatibility forms alone.
// Found by checking this repository against the audit of the maintainer's
// SSI-263 project (ligatures were lost there too).
//
// foldLigatures is the piece of NFKC that was missing there: the seven Latin
// ligatures of the Alphabetic Presentation Forms block, U+FB00 to U+FB06, to
// the letters of their compatibility decompositions (Unicode Character
// Database, UnicodeData.txt, the <compat> mappings of those code points).
// It is compiled and tested on every platform; only the Android and Linux
// branch of normalizeNFKC calls it.

#include <string>

#include "doctest.h"

#include "utf8.h"

using nvsp_frontend::foldLigatures;
using nvsp_frontend::normalizeText;
using nvsp_frontend::u32ToUtf8;

namespace {
std::string u8(std::u32string_view s) { return u32ToUtf8(s); }
}  // namespace

TEST_CASE("foldLigatures: each Latin ligature becomes the letters it stands for") {
    struct Case { char32_t ligature; const char* letters; };
    static const Case kCases[] = {
        {0xFB00, "ff"}, {0xFB01, "fi"}, {0xFB02, "fl"}, {0xFB03, "ffi"},
        {0xFB04, "ffl"}, {0xFB05, "st"}, {0xFB06, "st"},
    };
    for (const Case& c : kCases) {
        CAPTURE(static_cast<unsigned>(c.ligature));
        CHECK(foldLigatures(u8(std::u32string(1, c.ligature))) == c.letters);
    }
}

TEST_CASE("foldLigatures: inside words, the words come out whole") {
    CHECK(foldLigatures(u8(U"oﬃce")) == "office");
    CHECK(foldLigatures(u8(U"ﬁle")) == "file");
    CHECK(foldLigatures(u8(U"ﬂat")) == "flat");
    CHECK(foldLigatures(u8(U"oﬀ")) == "off");
    CHECK(foldLigatures(u8(U"waﬄe")) == "waffle");
    CHECK(foldLigatures(u8(U"firﬆ ﬁﬁ")) == "first fifi");
    // With other scripts and accented letters around it, those are untouched.
    CHECK(foldLigatures(u8(U"árvíz oﬃce 你好")) == u8(U"árvíz office 你好"));
}

TEST_CASE("foldLigatures: text without one comes back as it is") {
    static const char* const kSame[] = {
        "", "office", "plain text 123",
        "\xC3\xA1rv\xC3\xADzt\xC5\xB1r\xC5\x91",   // árvíztűrő
        "\xEF\xBC\xA1",                            // fullwidth A: not a ligature, left alone
        "\xEF\xAC\x93",                            // U+FB13, an Armenian ligature: outside the seven
        "\xEF\xAB\xBF",                            // U+FAFF, the code point before the block
        "\xEF\xAC\x87",                            // U+FB07, the code point after the seven
    };
    for (const char* s : kSame) {
        CAPTURE(s);
        CHECK(foldLigatures(s) == s);
    }
}

TEST_CASE("normalizeText: a word typeset with a ligature is the word, on every platform") {
    // Windows and Apple: the system's NFKC.  Android and Linux: foldLigatures.
    CHECK(normalizeText(u8(U"oﬃce")) == "office");
    CHECK(normalizeText(u8(U"ﬁle")) == "file");
    CHECK(normalizeText(u8(U"waﬄe")) == "waffle");
    // A ligature and a combining accent after it: the accent lands on the
    // last letter, as NFKC has it ("fi" + acute = "fí").
    CHECK(normalizeText(u8(U"ﬁ́")) == u8(U"fí"));
}

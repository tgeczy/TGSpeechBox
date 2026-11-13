// A no-break or typographic space becomes a plain space on every platform.
//
// stripInvisible turns U+00A0, U+2002 to U+200A, U+202F, U+205F and U+3000
// into U+0020: clocks write "10:00<U+202F>AM", and Hungarian, French, German,
// Polish and Czech group thousands with a no-break or narrow space
// ("10<U+00A0>000 Ft").  It then decided whether anything had changed by
// comparing lengths.  A replaced space keeps the length, so unless the same
// text also held a character that is removed (a zero-width space, a bidi
// mark) the function returned the text as it came and threw the spaces away.
//
// Windows and Apple never showed it: their system NFKC folds the same spaces
// right afterwards.  Android and Linux have no such step, so there "10:00 AM"
// with a narrow no-break space still read "ten o'clock A M" after #166, and
// "10 000 Ft" was read digit by digit.
//
// These call the function itself, so they fail on Windows too.

#include <string>

#include "doctest.h"

#include "utf8.h"

using nvsp_frontend::stripInvisible;
using nvsp_frontend::u32ToUtf8;

namespace {
std::string u8(std::u32string_view s) { return u32ToUtf8(s); }
}  // namespace

TEST_CASE("stripInvisible: a space variant alone becomes a plain space") {
    static const char32_t kSpaces[] = {0x00A0, 0x2002, 0x2003, 0x2004, 0x2005, 0x2006, 0x2007,
                                       0x2008, 0x2009, 0x200A, 0x202F, 0x205F, 0x3000};
    for (char32_t sp : kSpaces) {
        CAPTURE(static_cast<unsigned>(sp));
        std::u32string clock = U"10:00";
        clock += sp;
        clock += U"AM";
        CHECK(stripInvisible(u8(clock)) == "10:00 AM");
        std::u32string grouped = U"10";
        grouped += sp;
        grouped += U"000 Ft";
        CHECK(stripInvisible(u8(grouped)) == "10 000 Ft");
    }
}

TEST_CASE("stripInvisible: spaces are folded with and without a removed character beside them") {
    // U+200B is removed; the fold must not depend on it being there.
    CHECK(stripInvisible(u8(U"5:00 PM")) == "5:00 PM");
    CHECK(stripInvisible(u8(U"5:00 PM​")) == "5:00 PM");
    CHECK(stripInvisible(u8(U"a b c")) == "a b c");
}

TEST_CASE("stripInvisible: text with nothing to do comes back as it is") {
    CHECK(stripInvisible("10:00 AM") == "10:00 AM");
    CHECK(stripInvisible("") == "");
    CHECK(stripInvisible(u8(U"árvíztűrő")) == u8(U"árvíztűrő"));
    // Removal alone still works.
    CHECK(stripInvisible(u8(U"wi­fi​")) == "wifi");
}

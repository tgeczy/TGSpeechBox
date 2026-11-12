// Splitting a long text with many closing brackets takes time in proportion
// to the text (#171).
//
// At a closing bracket followed by a space, the clause splitter asks whether
// the bracket closes one that set something off (#141).  It answered by
// reading the text again from its first byte, every time: twice the text
// with twice the brackets took four times as long.  "word (a) b " repeated
// to 176 KB took ten seconds, which a SAPI program that hands over a whole
// document waits out before any speech starts.
//
// The splitter now walks back from the closing bracket to the one it closes.
// The places it splits at must not move: they are checked here against the
// old rule, written out plainly, on brackets nested, unbalanced and next to
// text in any byte.

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "doctest.h"
#include "nvspFrontend.h"
#include "text_prepare.h"
#include "utf8.h"

namespace {

struct Span {
    size_t start, end, next;
    char type;
    int pauseClass;
    bool operator==(const Span& o) const {
        return start == o.start && end == o.end && next == o.next && type == o.type && pauseClass == o.pauseClass;
    }
};

std::vector<Span> splitAll(std::string_view text) {
    std::vector<Span> out;
    size_t pos = 0;
    nvsp_frontend::ClauseSpan s;
    while (nvsp_frontend::nextClause(text, pos, s)) {
        REQUIRE(s.next > pos);
        out.push_back({s.start, s.end, s.next, s.type, s.pauseClass});
        pos = s.next;
    }
    return out;
}

// ---- The old rule, for texts of letters, spaces and brackets only ----

// One codepoint as the splitter reads it: a malformed byte is a codepoint of
// its own.
char32_t refDecode(std::string_view s, size_t i, size_t& len) {
    if (i >= s.size()) { len = 0; return 0; }
    const unsigned char c = static_cast<unsigned char>(s[i]);
    size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
    if (i + n > s.size()) n = 1;
    char32_t cp = n == 1 ? c : n == 2 ? (c & 0x1F) : n == 3 ? (c & 0x0F) : (c & 0x07);
    for (size_t k = 1; k < n; ++k) {
        const unsigned char cc = static_cast<unsigned char>(s[i + k]);
        if ((cc & 0xC0) != 0x80) { len = 1; return c; }
        cp = (cp << 6) | (cc & 0x3F);
    }
    len = n;
    return cp;
}

bool refSpace(char32_t c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == 0xA0 || c == 0x2028 || c == 0x2029 || c == 0x202F;
}

// Read from the first byte: one entry per unclosed bracket, true if it
// opened at the start of the text, after a space or after another bracket.
bool refClosesSetOffBracket(std::string_view text, size_t i) {
    std::vector<bool> open;
    char32_t prev = 0;
    size_t k = 0, len = 0;
    while (k < i) {
        const char32_t c = refDecode(text, k, len);
        if (c == '(' || c == '[') open.push_back(prev == 0 || refSpace(prev) || prev == '(' || prev == '[');
        else if ((c == ')' || c == ']') && !open.empty()) open.pop_back();
        prev = c;
        k += len;
    }
    return !open.empty() && open.back();
}

std::vector<Span> refSplitAll(std::string_view text) {
    std::vector<Span> out;
    const size_t n = text.size();
    size_t pos = 0, len = 0;
    for (;;) {
        while (pos < n && refSpace(refDecode(text, pos, len))) pos += len;
        if (pos >= n) break;
        const size_t start = pos;
        bool hasContent = false;
        char32_t prev = 0;
        size_t end = n, next = n;
        int pauseClass = 0;
        char type = '.';
        for (size_t i = start; i < n;) {
            const char32_t c = refDecode(text, i, len);
            const size_t j = i + len;
            size_t nextLen = 0;
            const char32_t after = refDecode(text, j, nextLen);
            if ((c == '(' || c == '[') && hasContent && refSpace(prev)) {
                end = next = i; type = ','; pauseClass = 1;
                break;
            }
            if ((c == ')' || c == ']') && i > start && (after == 0 || refSpace(after)) &&
                refClosesSetOffBracket(text, i)) {
                end = next = j; type = ','; pauseClass = 1;
                break;
            }
            if (!nvsp_frontend::isPunctOrSpaceCodepoint(c)) hasContent = true;
            prev = c;
            i = j;
        }
        // The clause without the spaces at its end.
        size_t e = end;
        while (e > start) {
            size_t b = e - 1;
            while (b > start && (static_cast<unsigned char>(text[b]) & 0xC0) == 0x80) --b;
            size_t l = 0;
            if (!refSpace(refDecode(text, b, l))) break;
            e = b;
        }
        out.push_back({start, e, next, type, pauseClass});
        pos = next;
    }
    return out;
}

std::string repeated(const std::string& unit, int times) {
    std::string s;
    s.reserve(unit.size() * static_cast<size_t>(times));
    for (int i = 0; i < times; ++i) s += unit;
    return s;
}

double secondsToSplit(const std::string& text, size_t& clauses) {
    const auto t0 = std::chrono::steady_clock::now();
    clauses = splitAll(text).size();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

}  // namespace

TEST_CASE("a long text with many bracket pairs is split in time proportional to it (#171)") {
    // "word (a) b " 20,000 times: 220 KB, 60,000 clauses.  Reading from the
    // start at every bracket took over ten seconds; walking back takes
    // hundredths of one.  The bound leaves room for a loaded machine.
    const std::string text = repeated("word (a) b ", 20000);
    size_t clauses = 0;
    const double s = secondsToSplit(text, clauses);
    MESSAGE("220 KB of \"word (a) b \": ", clauses, " clauses in ", s, " s");
    CHECK(clauses == 40001);
    CHECK(s < 3.0);

    // Prose with a parenthetical in every sentence, and brackets nested.
    const std::string prose = repeated(
        "The quick brown fox (which was, by all accounts [and there were many], very quick) "
        "jumped over the lazy dog. ", 3000);
    const double p = secondsToSplit(prose, clauses);
    const size_t proseKb = prose.size() / 1024;
    MESSAGE(proseKb, " KB of prose: ", clauses, " clauses in ", p, " s");
    CHECK(p < 3.0);
}

TEST_CASE("brackets split where they always did: known texts (#171)") {
    for (const char* text : {
             "word (a) b word (a) b ",
             "The file (about 2 MB) is ready.",
             "Hola (como est\xC3\xA1s, te escribo esto) espero que bien",
             "f(a, b) = 2",
             "word(s) and f(x) = 2",
             "a) first b) second c) third",
             "((nested) twice) and [mixed (kinds] here) ok",
             "(a (b) c) d (e [f] g) h",
             ") ) ( ( ] [ ) (",
             "\xC3\xA9(x) \xC3\xA9 (y) \xC2\xA0(z) end",
         }) {
        CAPTURE(text);
        // No end marks in these but commas: compare what brackets decide.
        std::string noMarks;
        for (const char* p = text; *p; ++p)
            if (*p != ',' && *p != '.' && *p != '=') noMarks += *p;
        CHECK(splitAll(noMarks) == refSplitAll(noMarks));
    }
}

TEST_CASE("brackets split where they always did: every arrangement (#171)") {
    // Pieces that make brackets nested, unbalanced, glued to words, after
    // spaces of more than one kind, and next to bytes that are not UTF-8.
    const std::vector<std::string> pieces = {
        "(", ")", "[", "]", "(", ")", " ", " ", " ", "a", "bc",
        "\xC3\xA9",      // é
        "\xC2\xA0",      // no-break space
        "\xE2\x80\xAF",  // narrow no-break space
        "\xA0",          // a stray byte that reads as a no-break space
        "\xC3",          // a lead byte with nothing after it
        "\xE2\x80",      // a three-byte character cut short
        std::string(1, '\0'),  // a NUL byte, which the old rule read as the start of the text
    };
    std::uint32_t seed = 171;
    auto rnd = [&seed](std::uint32_t n) {
        seed = seed * 1664525u + 1013904223u;
        return (seed >> 8) % n;
    };
    int checked = 0;
    for (int round = 0; round < 4000; ++round) {
        std::string text;
        const std::uint32_t count = 1 + rnd(round < 3900 ? 24 : 400);
        for (std::uint32_t k = 0; k < count; ++k) text += pieces[rnd(static_cast<std::uint32_t>(pieces.size()))];
        const auto got = splitAll(text);
        const auto want = refSplitAll(text);
        if (!(got == want)) {
            std::string hex;
            for (unsigned char c : text) {
                static const char* d = "0123456789ABCDEF";
                hex += d[c >> 4];
                hex += d[c & 15];
                hex += ' ';
            }
            FAIL_CHECK("clauses differ for bytes: " << hex);
            break;
        }
        ++checked;
    }
    CHECK(checked == 4000);
}

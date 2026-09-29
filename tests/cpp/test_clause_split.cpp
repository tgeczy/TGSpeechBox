// One clause splitter for every platform (#133).
//
// Every host used to carry its own copy of the loop that cuts text into
// clauses and picks the pause after each one, and the copies disagreed: NVDA
// gave colons and semicolons a sentence pause and split only where
// punctuation was followed by a space; SAPI, Android, iOS and Linux gave them
// a comma pause and split "example.com" and "Hello,world".  29-Bloo asked for
// pauses around parentheses and dashes, and before Spanish ¿ and ¡, which no
// copy had.  nvspFrontend_nextClause is the one rule, NVDA's, plus those.
#include <ostream>
#include <string>
#include <vector>

#include "doctest.h"
#include "nvspFrontend.h"

namespace {

struct Clause {
    std::string text;
    char type;
    double pauseMs;
    bool operator==(const Clause& o) const {
        return text == o.text && type == o.type && pauseMs == o.pauseMs;
    }
};

std::ostream& operator<<(std::ostream& os, const Clause& c) {
    return os << "{\"" << c.text << "\", '" << c.type << "', " << c.pauseMs << "}";
}

std::vector<Clause> split(const std::string& text, int pauseMode = 1) {
    std::vector<Clause> out;
    int pos = 0;
    for (int guard = 0; guard < 1000; ++guard) {
        int start = -1, end = -1;
        char type = 0;
        double pauseMs = -1.0;
        const int next = nvspFrontend_nextClause(text.c_str(), static_cast<int>(text.size()), pos,
                                                 pauseMode, &start, &end, &type, &pauseMs);
        if (next < 0) break;
        REQUIRE(next > pos);
        out.push_back({text.substr(start, end - start), type, pauseMs});
        pos = next;
    }
    return out;
}

// Short pauses (the default): 35 ms after a sentence mark, 25 ms after a comma.
constexpr double S = 35.0, C = 25.0, N = 0.0;

}  // namespace

TEST_CASE("sentence and comma punctuation, as NVDA has always split it") {
    CHECK(split("Hello, world. How are you? Fine!") ==
          std::vector<Clause>{{"Hello,", ',', C}, {"world.", '.', S}, {"How are you?", '?', S}, {"Fine!", '!', S}});
    CHECK(split("Documents") == std::vector<Clause>{{"Documents", '.', N}});
    CHECK(split("   ") == std::vector<Clause>{});
    CHECK(split("") == std::vector<Clause>{});
    // Closing quotes and brackets stay with the mark before them.
    CHECK(split("He said \"great.\" Then left.") ==
          std::vector<Clause>{{"He said \"great.\"", '.', S}, {"Then left.", '.', S}});
    CHECK(split("Really?! No.") == std::vector<Clause>{{"Really?!", '!', S}, {"No.", '.', S}});
}

TEST_CASE("colons and semicolons: split before a space, a sentence pause, their own type") {
    CHECK(split("Note: this; that") ==
          std::vector<Clause>{{"Note:", ':', S}, {"this;", ';', S}, {"that", '.', N}});
    CHECK(split("At 5:44 today") == std::vector<Clause>{{"At 5:44 today", '.', N}});
}

TEST_CASE("punctuation inside a word or number does not split") {
    CHECK(split("Visit example.com today") == std::vector<Clause>{{"Visit example.com today", '.', N}});
    CHECK(split("Hello,world") == std::vector<Clause>{{"Hello,world", '.', N}});
    CHECK(split("It costs 3.14 or 65,543.") == std::vector<Clause>{{"It costs 3.14 or 65,543.", '.', S}});
    // A dot after a digit never splits ("3. Mai").
    CHECK(split("am 3. Mai") == std::vector<Clause>{{"am 3. Mai", '.', N}});
}

TEST_CASE("an ellipsis ends a clause, even straight into the next word") {
    CHECK(split("Wait... what") == std::vector<Clause>{{"Wait...", '.', S}, {"what", '.', N}});
    CHECK(split("Wait...what") == std::vector<Clause>{{"Wait...", '.', S}, {"what", '.', N}});
    CHECK(split("Wait\xE2\x80\xA6 what") == std::vector<Clause>{{"Wait\xE2\x80\xA6", '.', S}, {"what", '.', N}});
}

TEST_CASE("parentheses and brackets set off what they hold (#133)") {
    CHECK(split("The file (about 2 MB) is ready.") ==
          std::vector<Clause>{{"The file", ',', C}, {"(about 2 MB)", ',', C}, {"is ready.", '.', S}});
    CHECK(split("(see above) then go") ==
          std::vector<Clause>{{"(see above)", ',', C}, {"then go", '.', N}});
    CHECK(split("Done [3 items], next") ==
          std::vector<Clause>{{"Done", ',', C}, {"[3 items],", ',', C}, {"next", '.', N}});
    CHECK(split("Is it (really?) true") ==
          std::vector<Clause>{{"Is it", ',', C}, {"(really?)", '?', S}, {"true", '.', N}});
    // Not a parenthesis that sets anything off: no space before it.
    CHECK(split("word(s) and f(x) = 2") == std::vector<Clause>{{"word(s) and f(x) = 2", '.', N}});
}

TEST_CASE("dashes between words pause like a comma (#133)") {
    const std::vector<Clause> want{{"wait \xE2\x80\x94", ',', C}, {"what now", '.', N}};
    CHECK(split("wait \xE2\x80\x94 what now") == want);                                       // em dash
    CHECK(split("wait\xE2\x80\x94what now") == std::vector<Clause>{{"wait\xE2\x80\x94", ',', C}, {"what now", '.', N}});
    CHECK(split("wait \xE2\x80\x93 what now") == std::vector<Clause>{{"wait \xE2\x80\x93", ',', C}, {"what now", '.', N}});
    CHECK(split("wait - what now") == std::vector<Clause>{{"wait -", ',', C}, {"what now", '.', N}});
    CHECK(split("wait -- what now") == std::vector<Clause>{{"wait --", ',', C}, {"what now", '.', N}});
    CHECK(split("wait--what now") == std::vector<Clause>{{"wait--", ',', C}, {"what now", '.', N}});
}

TEST_CASE("hyphens and ranges that are not dashes do not split") {
    CHECK(split("a well-known pages 1\xE2\x80\x93" "5 and -5 degrees") ==
          std::vector<Clause>{{"a well-known pages 1\xE2\x80\x93" "5 and -5 degrees", '.', N}});
    CHECK(split("run it with --help now") == std::vector<Clause>{{"run it with --help now", '.', N}});
    // A dash that opens the text sets nothing off.
    CHECK(split("\xE2\x80\x94 Tamas") == std::vector<Clause>{{"\xE2\x80\x94 Tamas", '.', N}});
}

TEST_CASE("Spanish \xC2\xBF and \xC2\xA1 open a new clause (#133)") {
    CHECK(split("Si vienes \xC2\xBFme avisas?") ==
          std::vector<Clause>{{"Si vienes", ',', C}, {"\xC2\xBFme avisas?", '?', S}});
    CHECK(split("dijo \xC2\xA1" "basta! ya") ==
          std::vector<Clause>{{"dijo", ',', C}, {"\xC2\xA1" "basta!", '!', S}, {"ya", '.', N}});
    CHECK(split("Hola, \xC2\xBFqu\xC3\xA9 tal?") ==
          std::vector<Clause>{{"Hola,", ',', C}, {"\xC2\xBFqu\xC3\xA9 tal?", '?', S}});
    CHECK(split("\xC2\xBFQu\xC3\xA9?") == std::vector<Clause>{{"\xC2\xBFQu\xC3\xA9?", '?', S}});
}

TEST_CASE("pause lengths follow the pause setting") {
    CHECK(split("One, two.", 0) == std::vector<Clause>{{"One,", ',', 0.0}, {"two.", '.', 0.0}});
    CHECK(split("One, two.", 2) == std::vector<Clause>{{"One,", ',', 50.0}, {"two.", '.', 60.0}});
}

TEST_CASE("the split never loses or reorders text") {
    const std::string text =
        "Hi (there) \xE2\x80\x94 so: \xC2\xBFok? \xC2\xA1s\xC3\xAD! a - b... c\xE2\x80\xA6 [x], d";
    std::string joined;
    for (const Clause& c : split(text)) joined += c.text;
    std::string squeezed;
    for (char ch : text)
        if (ch != ' ') squeezed += ch;
    std::string joinedSqueezed;
    for (char ch : joined)
        if (ch != ' ') joinedSqueezed += ch;
    CHECK(joinedSqueezed == squeezed);
}

// Money amounts read the way people say them (#83).
//
// eSpeak reads "$1.25" as "dollar one point two five" and "€5.99" as "five
// point nine nine" with no unit at all.  moneyAsWords (on for English packs)
// rewrites the unit words and their order; the numbers stay digits.  A price
// is also never split as a year ("$1995" was "$19 95").
#include <string>

#include "doctest.h"
#include "text_prepare.h"

using nvsp_frontend::moneyAsWords;
using nvsp_frontend::splitYears;

TEST_CASE("money: a sign before the amount") {
    CHECK(moneyAsWords("$1.25") == "1 dollar and 25 cents");
    CHECK(moneyAsWords("$5") == "5 dollars");
    CHECK(moneyAsWords("$1") == "1 dollar");
    CHECK(moneyAsWords("$5.00") == "5 dollars");
    CHECK(moneyAsWords("$0.50") == "50 cents");
    CHECK(moneyAsWords("$0.01") == "1 cent");
    CHECK(moneyAsWords("$2.5") == "2 dollars and 50 cents");
    CHECK(moneyAsWords("€5.99") == "5 euros and 99 cents");
    CHECK(moneyAsWords("£1.01") == "1 pound and 1 penny");
    CHECK(moneyAsWords("£3.50") == "3 pounds and 50 pence");
    CHECK(moneyAsWords("₹250") == "250 rupees");
    CHECK(moneyAsWords("Rs. 499") == "499 rupees");
    CHECK(moneyAsWords("US$20") == "20 dollars");
    CHECK(moneyAsWords("USD 20") == "20 dollars");
}

TEST_CASE("money: the unit after the amount") {
    CHECK(moneyAsWords("20 USD") == "20 dollars");
    CHECK(moneyAsWords("5€") == "5 euros");
    CHECK(moneyAsWords("50¢") == "50 cents");
    CHECK(moneyAsWords("1¢") == "1 cent");
    CHECK(moneyAsWords("100 EUR.") == "100 euros.");
}

TEST_CASE("money: groups, scales, signs and decimals") {
    CHECK(moneyAsWords("$1,234,567") == "1234567 dollars");
    CHECK(moneyAsWords("₹1,23,456") == "123456 rupees");
    CHECK(moneyAsWords("₹12,34,567.50") == "1234567 rupees and 50 paise");
    CHECK(moneyAsWords("$1,234") == "1234 dollars");
    CHECK(moneyAsWords("$2.5M") == "2.5 million dollars");
    CHECK(moneyAsWords("$5k") == "5 thousand dollars");
    CHECK(moneyAsWords("$3 billion") == "3 billion dollars");
    CHECK(moneyAsWords("₹5 crore") == "5 crore rupees");
    CHECK(moneyAsWords("-$4") == "minus 4 dollars");
    CHECK(moneyAsWords("$3.499") == "3.499 dollars");
    CHECK(moneyAsWords("¥1.5") == "1.5 yen");
}

TEST_CASE("money: in running text") {
    CHECK(moneyAsWords("It costs $1.25, not $2.") == "It costs 1 dollar and 25 cents, not 2 dollars.");
    CHECK(moneyAsWords("from $5-$10") == "from 5 dollars-10 dollars");
    CHECK(moneyAsWords("(€5)") == "(5 euros)");
}

TEST_CASE("money: what it leaves alone") {
    // No amount, or the sign is part of something else.
    CHECK(moneyAsWords("$HOME") == "$HOME");
    CHECK(moneyAsWords("$5abc") == "$5abc");
    CHECK(moneyAsWords("a$5") == "a$5");
    CHECK(moneyAsWords("$1.2.3") == "$1.2.3");
    CHECK(moneyAsWords("$6:30") == "$6:30");
    // A decimal comma is ambiguous; a unit glued to a word is not a unit.
    CHECK(moneyAsWords("3,50 €") == "3,50 €");
    CHECK(moneyAsWords("€3,50") == "€3,50");
    CHECK(moneyAsWords("$1234,567") == "$1234,567");
    CHECK(moneyAsWords("$1,234,56,789") == "$1,234,56,789");
    CHECK(moneyAsWords("20 USDT") == "20 USDT");
    CHECK(moneyAsWords("5$") == "5$");
    CHECK(moneyAsWords("Rs5") == "5 rupees");
    CHECK(moneyAsWords("Mrs 5") == "Mrs 5");
    CHECK(moneyAsWords("page 12") == "page 12");
    CHECK(moneyAsWords("no digits here") == "no digits here");
}

TEST_CASE("year splitting: a price is not a year") {
    CHECK(splitYears("in 1995", "oh") == "in 19 95");
    CHECK(splitYears("$1995", "oh") == "$1995");
    CHECK(splitYears("1995 dollars", "oh") == "1995 dollars");
    CHECK(splitYears("USD 1995.", "oh") == "USD 1995.");
    CHECK(splitYears("1995 was cheap", "oh") == "19 95 was cheap");
    CHECK(splitYears(moneyAsWords("$1,995"), "oh") == "1995 dollars");
    CHECK(splitYears(moneyAsWords("In 1995 it was $1995."), "oh") == "In 19 95 it was 1995 dollars.");
}

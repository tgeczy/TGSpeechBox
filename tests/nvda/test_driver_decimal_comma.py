"""Hungarian "1,500" is fifteen hundred, and a pack can say otherwise (#167).

A pack can name the comma as its decimal mark (decimalSeparator: ","), and a
number with one comma is then a decimal.  Hungarian was the first candidate
(A magyar helyesírás szabályai, 12th edition, Akadémiai Kiadó 2015, rules
274, 291 b and 293 write decimals with a comma), and the maintainer, a native
speaker, decided against it: English-style numbers are what a screen reader
user meets most, so "1,500" stays "ezer ötszáz" and "10,000" "tízezer".

Run through the driver's own path: prepareText, then NVDA's eSpeak.
tests/test_text_preparation.py has the prepared text for every case, the
setting on a copy of the pack, and every other language.
"""
from __future__ import annotations

import pytest

WHOLE = "ɛɡeːs"      # "egész": what eSpeak NG's Hungarian says for the decimal comma
THOUSAND = "ɛzɛr"    # "ezer"
MILLION = "millioː"  # "millió"


def _ipa(driver, text):
    return driver._espeakTextToIPA(driver._frontend.prepareText(text))


@pytest.mark.parametrize("text", ["1,500", "Az ára 1,500 Ft.", "1,213", "65,543", "10,000", "250,000", "1 500", "1500", "12 500"])
def test_a_comma_before_three_digits_is_thousands(harness, text):
    harness.driver.language = "hu"
    ipa = _ipa(harness.driver, text)
    assert THOUSAND in ipa and WHOLE not in ipa, f"{text!r} -> {ipa}"


@pytest.mark.parametrize("text", ["3,14", "0,5", "0,125"])
def test_a_comma_that_cannot_group_thousands_is_the_decimal_comma(harness, text):
    harness.driver.language = "hu"
    ipa = _ipa(harness.driver, text)
    assert WHOLE in ipa and THOUSAND not in ipa, f"{text!r} -> {ipa}"


@pytest.mark.parametrize("text", ["1,234,567", "1 234 567"])
def test_a_number_with_several_commas_is_grouped(harness, text):
    harness.driver.language = "hu"
    ipa = _ipa(harness.driver, text)
    assert MILLION in ipa and WHOLE not in ipa, f"{text!r} -> {ipa}"


@pytest.mark.parametrize("lang, text", [("hu", "1,500 Ft"), ("de", "1,500 Euro"), ("es", "1,500"), ("en-us", "1,500 people")])
def test_every_language_prepares_it_as_before(harness, lang, text):
    harness.driver.language = lang
    prepared = harness.driver._frontend.prepareText(text)
    assert prepared == text.replace(",", "" if lang == "en-us" else " ")

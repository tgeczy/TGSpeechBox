"""Hungarian "1,500" is one and a half, not fifteen hundred (#167).

Hungarian writes decimals with a comma and groups thousands with a space or
a dot (A magyar helyesírás szabályai, 12th edition, Akadémiai Kiadó 2015,
rules 274, 291 b and 293).  Text preparation used to turn the comma of
"1,500" into a space, and eSpeak NG then read "ezer ötszáz".  hu.yaml now
says decimalSeparator: ",", the comma stays, and eSpeak NG reads "egy egész
ötszáz ezred".

Run through the driver's own path: prepareText, then NVDA's eSpeak.
tests/test_text_preparation.py has the prepared text for every case and
every other language.
"""
from __future__ import annotations

import pytest

WHOLE = "ɛɡeːs"      # "egész": what eSpeak NG's Hungarian says for the decimal comma
THOUSAND = "ɛzɛr"    # "ezer"
MILLION = "millioː"  # "millió"


def _ipa(driver, text):
    return driver._espeakTextToIPA(driver._frontend.prepareText(text))


@pytest.mark.parametrize("text", ["1,500", "Az ára 1,500 Ft.", "1,213", "65,543", "250,000", "3,14", "0,5"])
def test_one_comma_is_read_as_the_decimal_comma(harness, text):
    harness.driver.language = "hu"
    ipa = _ipa(harness.driver, text)
    assert WHOLE in ipa and THOUSAND not in ipa, f"{text!r} -> {ipa}"


def test_thousands_grouped_with_a_space_then_decimals(harness):
    """The rule book's example, 20 611,413: twenty thousand ..., and 413 thousandths."""
    harness.driver.language = "hu"
    ipa = _ipa(harness.driver, "20 611,413")
    assert WHOLE in ipa and THOUSAND in ipa and MILLION not in ipa, ipa


@pytest.mark.parametrize("text", ["1 500", "1500", "12 500"])
def test_thousands_are_still_thousands(harness, text):
    harness.driver.language = "hu"
    ipa = _ipa(harness.driver, text)
    assert THOUSAND in ipa and WHOLE not in ipa, f"{text!r} -> {ipa}"


@pytest.mark.parametrize("text", ["1,234,567", "1 234 567"])
def test_a_number_with_several_commas_is_still_grouped(harness, text):
    harness.driver.language = "hu"
    ipa = _ipa(harness.driver, text)
    assert MILLION in ipa and WHOLE not in ipa, f"{text!r} -> {ipa}"


@pytest.mark.parametrize("lang, text", [("de", "1,500 Euro"), ("es", "1,500"), ("en-us", "1,500 people")])
def test_other_languages_prepare_it_as_before(harness, lang, text):
    harness.driver.language = lang
    prepared = harness.driver._frontend.prepareText(text)
    assert prepared == text.replace(",", "" if lang == "en-us" else " ")

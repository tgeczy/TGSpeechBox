"""Spanish capitals and "tw" read as Eloquence and OneCore read them (#146).

Greg: "descargar WinRAR para PC" spelled out "r a r", and so did "el hombre
MAS fuerte", "es UN color", "navegando por la WEB"; "Shania Twain" came out
"T wain", and "twa, twe, twi, two, twu" letter by letter.  eSpeak NG's
Spanish spells most short all-caps words while it reads others ("ONU",
"NASA") as words, and has no reading for "tw".

For languages that ask (es.yaml), the text preparation every platform runs
before eSpeak now reads an all-caps word as a word when Spanish could
pronounce it, and leaves it to be spelled when it couldn't ("PC", "DNI",
"BBC", "ONG"); Roman numerals are left alone.  "tw" before a vowel is read
"tu" (Twain -> Tuain).

Run through the driver's own path: prepareText, then NVDA's eSpeak.
"""
from __future__ import annotations

import pytest

# Letter names eSpeak says when it spells: erre, eme, ese, uve doble, ene, be...
SPELLED = ("ˌere", "ˌeme", "ˌese", "ðˌoβle", "ˌene", "ˌuβe", "ˌeˌu")


def _ipa(driver, text):
    return driver._espeakTextToIPA(driver._frontend.prepareText(text))


@pytest.mark.parametrize("text,word", [
    ("descargar WinRAR para PC", "rˈaɾ"),
    ("el hombre MAS fuerte", "mˈas"),
    ("es UN color", "ˈun"),
    ("navegando por la WEB", "wˈeb"),
    ("Shania Twain", "twˈa"),
    ("twa twe twi two twu", "twˈa"),
    ("la NASA", "nˈasa"),
])
def test_pronounceable_words_are_read_as_words(harness, text, word):
    harness.driver.language = "es"
    ipa = _ipa(harness.driver, text)
    assert word in ipa.replace("ˌ", "ˈ") or word in ipa, f"{text!r} -> {ipa}"
    assert not any(s in ipa for s in ("ðˌoβle", "ˌereˌa", "ˌemeˌa", "ˌuˈɛne")), f"{text!r} still spelled: {ipa}"


@pytest.mark.parametrize("text", ["PC", "DNI", "BBC", "ONG", "ADN", "TV"])
def test_unpronounceable_ones_are_still_spelled(harness, text):
    harness.driver.language = "es"
    assert harness.driver._frontend.prepareText(text) == text


@pytest.mark.parametrize("text", ["Carlos III", "el siglo XXI", "Juan Pablo II", "IV"])
def test_roman_numerals_are_left_alone(harness, text):
    harness.driver.language = "es"
    assert harness.driver._frontend.prepareText(text) == text


def test_only_where_the_pack_asks(harness):
    harness.driver.language = "en-us"
    assert harness.driver._frontend.prepareText("the WEB and Twain") == "the WEB and Twain"

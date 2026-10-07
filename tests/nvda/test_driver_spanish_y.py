"""Spanish "y" before a consonant is the vowel /i/ (#145).

Greg: "Yndio" (the band) should be "indio", as Eloquence and the OneCore
voices say it; TGSpeechBox said something like "dyndió".  eSpeak NG reads a
word-initial "y" before a consonant as the palatal stop [ɟ] and, with no
vowel in its first syllable, stresses the wrong one: "Yndio" became
ɟndjˈo.  In Spanish spelling such a "y" is the vowel i (Yndio, Ybarra,
Yrigoyen, Ypacaraí), so the frontend's text preparation, which every
platform runs before eSpeak, writes it as "i" for languages that ask
(es.yaml: initialYBeforeConsonantAsI).

Run through the driver's own path: prepareText, then NVDA's eSpeak.
"""
from __future__ import annotations

import pytest

CASES = [
    ("Yndio", "ˈindjo"),
    ("yndio", "ˈindjo"),
    ("Ybarra", "iβˈara"),
    ("Yrigoyen", "iɾiɣˈoʝen"),
]


def _ipa(driver, text):
    return driver._espeakTextToIPA(driver._frontend.prepareText(text))


@pytest.mark.parametrize("lang", ["es", "es-mx"])
@pytest.mark.parametrize("word,want", CASES, ids=[c[0] for c in CASES])
def test_initial_y_before_a_consonant_is_i(harness, lang, word, want):
    harness.driver.language = lang
    ipa = _ipa(harness.driver, word)
    assert "ɟ" not in ipa and want in ipa.replace("ˌ", ""), f"{word} ({lang}) -> {ipa}"


@pytest.mark.parametrize("text", ["ya", "hoy y mañana", "rey", "YPF", "Yo"])
def test_other_ys_are_untouched(harness, text):
    harness.driver.language = "es"
    assert harness.driver._frontend.prepareText(text) == text


def test_only_where_the_pack_asks(harness):
    harness.driver.language = "en-us"
    assert harness.driver._frontend.prepareText("Yndio") == "Yndio"

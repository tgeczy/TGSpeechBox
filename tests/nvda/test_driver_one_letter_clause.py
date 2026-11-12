"""The NVDA driver reads a one-letter clause of a longer text as a word (#158).

The driver looks letter names up itself, before it calls the frontend's text
pass, and the frontend looks them up again.  Both named any clause that was a
lone letter: Portuguese "Sim. É. Claro." was spoken "Sim. é agudo. Claro.",
Spanish "Y, sin embargo, se fue." was "i griega, sin embargo, se fue."  A
letter is named only when it is the whole text NVDA sent (character
navigation, a typed character, a line that is one letter next to a symbol,
#122).

Run through the driver's own speak(): what is checked is the text of each
clause as it reaches NVDA's eSpeak.
"""
from __future__ import annotations

import pytest


def _spoken(harness, monkeypatch, lang, text):
    """The text of each clause the driver hands to eSpeak for `text`."""
    driver = harness.driver
    driver.language = lang
    seen = []
    real = driver._espeakTextToIPA_scriptAware

    def spy(chunk, *a, **k):
        seen.append(chunk)
        return real(chunk, *a, **k)

    monkeypatch.setattr(driver, "_espeakTextToIPA_scriptAware", spy)
    harness.speak([text])
    return seen


@pytest.mark.parametrize("lang, text, expected", [
    ("pt-br", "É, eu sei.", ["É,", "eu sei."]),
    ("pt-br", "Sim. É. Claro.", ["Sim.", "É.", "Claro."]),
    ("pt-br", "Você vai? É.", ["Você vai?", "É."]),
    ("pt-br", "Sim, é", ["Sim,", "é"]),
    ("es", "Y, sin embargo, se fue.", ["Y,", "sin embargo,", "se fue."]),
    ("es", "No. ¿Y? Nada.", ["No.", "¿Y?", "Nada."]),
])
def test_a_one_letter_clause_of_a_longer_text_is_a_word(harness, monkeypatch, lang, text, expected):
    assert _spoken(harness, monkeypatch, lang, text) == expected


@pytest.mark.parametrize("lang, text, expected", [
    ("pt-br", "é", ["é agudo"]),
    ("pt-br", "É.", ["é agudo."]),
    ("es", "y", ["i griega"]),
    ("es", "Y", ["i griega"]),
    ("es", "ó.", ["o acentuada."]),
    ("es", "r?", ["ere?"]),
    ("es", "¿r", ["¿ere"]),
])
def test_a_letter_that_is_the_whole_text_keeps_its_name(harness, monkeypatch, lang, text, expected):
    """#122 stays fixed."""
    assert _spoken(harness, monkeypatch, lang, text) == expected


def test_a_longer_clause_is_prepared_as_before(harness, monkeypatch):
    assert _spoken(harness, monkeypatch, "pt-br", "Ele é bom.") == ["Ele é bom."]
    assert _spoken(harness, monkeypatch, "es", "Fui a casa y, después, dormí.") == [
        "Fui a casa y,", "después,", "dormí."]

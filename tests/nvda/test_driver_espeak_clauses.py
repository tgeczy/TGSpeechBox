"""Words either side of a dash, ¿ or ¡ stay separate words (#133).

eSpeak ends a clause at an en or em dash and at Spanish ¿ and ¡, and hands
the text back one clause per espeak_TextToPhonemes call.  The driver joined
those clauses with nothing between them, so "wait — what now" reached the
frontend as one word, "wˈe͡ɪtwˌʌt", and "dijo ¡basta! ya" as one long one.
Android, iOS and Linux have always joined them with a space.
"""
from __future__ import annotations

import pytest

CASES = [
    ("en-us", "wait — what now", ["wˈe͡ɪt", "wˌʌt", "nˈa͡ʊ"]),
    ("en-us", "wait – what now", ["wˈe͡ɪt", "wˌʌt", "nˈa͡ʊ"]),
    ("es", "hola ¿qué tal?", ["ˈola", "kˈe", "tˈal"]),
    ("es", "dijo ¡basta! ya", ["dˈixo", "bˈasta", "ʝˈa"]),
    ("es", "sí — claro", ["sˈi", "klˈaɾo"]),
]


@pytest.mark.parametrize("lang,text,words", CASES, ids=[c[1] for c in CASES])
def test_words_around_a_clause_mark_stay_apart(harness, lang, text, words):
    harness.driver.language = lang
    ipa = harness.driver._espeakTextToIPA(text)
    assert ipa.split() == words, f"{text!r} -> {ipa!r}"

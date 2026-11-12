"""The NVDA driver's own character lookup takes the same keys as the frontend (#170).

The driver names a character from the language's letters file before it
calls the frontend, with its own copy of the rule.  Like the frontend's, it
wanted exactly one character: an emoji followed by a variation selector
(U+FE0F, which keyboards add) was two, and a key of more than one letter
("ll") could never be found.

The entries here are a user's: they are put in the driver's cache of the
letters file and in the frontend's dictionary, as an edited file would be
loaded by both.
"""
from __future__ import annotations

import ctypes

import pytest

HEART = "❤"
VS16 = "️"
VS15 = "︎"
USER_ENTRIES = {HEART: "corazón", "ll": "elle"}


@pytest.fixture
def spoken(harness, monkeypatch):
    """spoken(text): the text of each clause the driver hands to eSpeak, in
    Spanish with USER_ENTRIES in the character dictionary."""
    import synthDrivers.tgSpeechBox.speech_pipeline as pipeline

    driver = harness.driver
    driver.language = "es"
    names = dict(pipeline._loadLetterNames(driver._packsDir, driver._frontendLangTag))
    names.update(USER_ENTRIES)
    monkeypatch.setitem(pipeline._letterNameCache, driver._frontendLangTag, names)
    fe = driver._frontend
    fe._dll.nvspFrontend_setData.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_char_p,
                                             ctypes.c_char_p, ctypes.c_char_p]
    fe._dll.nvspFrontend_setData.restype = ctypes.c_int
    for key, name in USER_ENTRIES.items():
        assert fe._dll.nvspFrontend_setData(fe._h, 2, b"character:es", key.encode("utf-8"), name.encode("utf-8"))

    seen = []
    real = driver._espeakTextToIPA_scriptAware

    def spy(chunk, *a, **k):
        seen.append(chunk)
        return real(chunk, *a, **k)

    monkeypatch.setattr(driver, "_espeakTextToIPA_scriptAware", spy)

    def _spoken(text):
        del seen[:]
        harness.speak([text])
        return list(seen)

    yield _spoken
    for key in USER_ENTRIES:
        fe._dll.nvspFrontend_setData(fe._h, 2, b"character:es", key.encode("utf-8"), b"")


@pytest.mark.parametrize("text, expected", [
    (HEART, ["corazón"]),
    (HEART + VS16, ["corazón"]),
    (HEART + VS15, ["corazón"]),
    (HEART + VS16 + ".", ["corazón."]),
    ("ll", ["elle"]),
    ("LL", ["elle"]),
    ("¿ll?", ["¿elle?"]),
])
def test_an_emoji_with_a_selector_and_a_two_letter_key_have_their_names(spoken, text, expected):
    assert spoken(text) == expected


@pytest.mark.parametrize("text, expected", [
    ("y", ["i griega"]),
    ("ó.", ["o acentuada."]),
    ("calle", ["calle"]),
    ("llama", ["llama"]),
    # One clause of a longer text is not named (#158).
    ("Hola. ll. Adiós.", ["Hola.", "ll.", "Adiós."]),
])
def test_everything_else_is_read_as_before(spoken, text, expected):
    assert spoken(text) == expected

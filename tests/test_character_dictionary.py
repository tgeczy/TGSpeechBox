"""Character dictionary entries a user adds (#170).

The character dictionary names a character that is read on its own.  Two
kinds of entry never took effect:

* An emoji typed with a variation selector.  Keyboards and iOS put U+FE0F
  after an emoji so that it shows in colour; "❤️" is then two codepoints,
  and the lookup wanted exactly one.  An entry for ❤ was skipped.
* A key longer than one character.  Dictionary-editing.md allows "a single
  character (or short string)" and gives Hungarian "gy" as an example, and
  the editors take such a key, but the lookup only ever built a
  one-character key.

Entries go in the way the editors add them (nvspFrontend_setData).
"""
from __future__ import annotations

import pytest

HEART = "❤"
SMILE = "☺"
VS15 = "︎"  # text presentation
VS16 = "️"  # emoji presentation


@pytest.fixture
def letters(frontend, pack_dir):
    """letters(lang, entries) -> prepare(text, whole_text=True)."""
    handles = []

    def _open(lang, entries):
        h = frontend.create_handle(pack_dir)
        handles.append(h)
        frontend.set_language(h, lang)
        for key, name in entries.items():
            frontend.set_dict_entry(h, lang, key, name, dict_type="character")

        def prepare(text, whole_text=True):
            return frontend.prepare_clause(h, text, whole_text)

        return prepare

    yield _open
    for h in handles:
        frontend.destroy_handle(h)


ES_ENTRIES = {HEART: "corazón", SMILE: "cara sonriente", "ll": "elle", "ch": "che"}


@pytest.mark.parametrize("text, expected", [
    (HEART, "corazón"),                      # the control: this always worked
    (HEART + VS16, "corazón"),
    (HEART + VS15, "corazón"),
    (HEART + VS16 + ".", "corazón."),
    (" " + HEART + VS16 + " ", " corazón "),
    ("¿" + HEART + VS16 + "?", "¿corazón?"),
    (SMILE + VS16, "cara sonriente"),
])
def test_an_emoji_with_a_variation_selector_has_its_name(letters, text, expected):
    prepare = letters("es", ES_ENTRIES)
    assert prepare(text) == expected


@pytest.mark.parametrize("text, expected", [
    ("ll", "elle"),
    ("LL", "elle"),
    ("Ll", "elle"),
    ("ll.", "elle."),
    ("¿ch?", "¿che?"),
    (" ll ", " elle "),
])
def test_a_key_of_more_than_one_letter_is_found(letters, text, expected):
    prepare = letters("es", ES_ENTRIES)
    assert prepare(text) == expected


def test_the_hungarian_example_from_the_documentation(letters):
    """Dictionary-editing.md: 'Hungarian gy -> "gyé"'.  Hungarian ships no
    character dictionary; the entry is the user's."""
    prepare = letters("hu", {"gy": "gyé", "ő": "hosszú ö"})
    assert prepare("gy") == "gyé"
    assert prepare("Gy") == "gyé"
    assert prepare("ő") == "hosszú ö"
    assert prepare("gyár") == "gyár"
    assert prepare("Egy gyár.") == "Egy gyár."


@pytest.mark.parametrize("text", [
    "lla", "calle", "ll ll", "llama y chico", HEART + HEART, "te " + HEART + VS16,
])
def test_only_the_whole_text_is_looked_up(letters, text):
    """A key is a name for a text that is nothing else; inside a longer text
    it is left alone."""
    prepare = letters("es", ES_ENTRIES)
    assert prepare(text) == text


@pytest.mark.parametrize("text", ["ll", "ll.", HEART + VS16, "y"])
def test_one_clause_of_a_longer_text_is_not_named(letters, text):
    """As for a one-letter word (#158)."""
    prepare = letters("es", ES_ENTRIES)
    assert prepare(text, whole_text=False) == text


@pytest.mark.parametrize("lang, text, expected", [
    ("es", "y", "i griega"),
    ("es", "Á", "a acentuada"),
    ("es", "ó.", "o acentuada."),
    ("es", "r?", "ere?"),
    ("pt-br", "ç", "cê cedilha"),
    ("pt-br", "É", "é agudo"),
])
def test_shipped_letter_names_are_what_they_were(letters, lang, text, expected):
    prepare = letters(lang, ES_ENTRIES if lang == "es" else {})
    assert prepare(text) == expected


@pytest.mark.parametrize("lang", ["es", "es-mx", "pt-br", "pt", "en-us", "hu", "de"])
@pytest.mark.parametrize("text", [
    "ll", "LL", "ch", "gy", HEART, HEART + VS16, HEART + VS16 + ".", SMILE + VS16, "rr", "rrr", "h", "...", "42",
])
def test_without_such_an_entry_nothing_changes(letters, lang, text):
    """No shipped character dictionary has an emoji or a key of more than one
    letter: every language passes these through, selector and all."""
    prepare = letters(lang, {})
    assert prepare(text) == text

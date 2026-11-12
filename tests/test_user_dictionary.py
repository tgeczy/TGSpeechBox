"""Pronunciation dictionary entries a user adds, through the text pass.

Entries go in the way the editors on every platform add them
(nvspFrontend_setData), and nvspFrontend_prepareText is asked what reaches
the phonemizer.
"""
from __future__ import annotations

import locale

import pytest


@pytest.fixture
def user_dict(frontend, pack_dir):
    """user_dict(lang) -> (add, prepare) for a fresh handle in that language:
    add(word, to_text) adds a respelling, prepare(text) runs the text pass."""
    handles = []

    def _open(lang):
        h = frontend.create_handle(pack_dir)
        handles.append(h)
        frontend.set_language(h, lang)

        def add(word, to_text):
            frontend.set_dict_entry(h, lang, word, '{"toText":"%s"}' % to_text)

        def prepare(text):
            return frontend.prepare_text(h, text)

        return add, prepare

    yield _open
    for h in handles:
        frontend.destroy_handle(h)


@pytest.fixture(params=["C", "English_United States.1252"])
def c_locale(request):
    """The C runtime's character locale: "C" as the phone apps, SAPI and
    Linux run, and the Windows code page that NVDA's Python (and this one)
    sets, where byte-wise tolower() rewrites the bytes of non-ASCII letters."""
    before = locale.setlocale(locale.LC_CTYPE)
    try:
        locale.setlocale(locale.LC_CTYPE, request.param)
    except locale.Error:
        pytest.skip(f"no {request.param} locale on this system")
    yield request.param
    locale.setlocale(locale.LC_CTYPE, before)


# ---------------------------------------------------------------------------
# #164: an entry matches whatever the capitals, in any alphabet.
# ---------------------------------------------------------------------------
# The lowercase key was built with byte-wise tolower(): only A to Z folded,
# so "ész" did not match "Ész" at the start of a sentence.

@pytest.mark.parametrize("lang, word, text, expected", [
    ("hu", "ész", "ész", "ESZ_R"),
    ("hu", "ész", "Ész nélkül", "ESZ_R nélkül"),
    ("hu", "ősz", "ŐSZ van.", "OSZ_R van."),
    ("de", "über", "Über alles", "UBER_R alles"),
    ("ru", "привет", "Привет мир", "PRIVET_R мир"),
    ("es", "ñandú", "Ñandú.", "NANDU_R."),
    ("pt-br", "água", "Água fria", "AGUA_R fria"),
    ("pl", "łódź", "Łódź jest", "LODZ_R jest"),
    ("en-us", "tgsbword", "Tgsbword TGSBWORD tgsbword", "TGSBWORD_R TGSBWORD_R TGSBWORD_R"),
])
def test_an_entry_matches_a_capitalised_word(user_dict, c_locale, lang, word, text, expected):
    add, prepare = user_dict(lang)
    to_text = expected.split()[0].rstrip(".")
    add(word, to_text)
    assert prepare(text) == expected


@pytest.mark.parametrize("text, expected", [
    ("ablakban", "ABLAK_Rban"),
    ("ablakból", "ABLAK_Rból"),
    ("ablakért", "ABLAK_Rért"),
    ("Ablakról néz", "ABLAK_Rról néz"),
    ("ablaktól", "ABLAK_Rtól"),
    ("ablakhoz", "ABLAK_Rhoz"),
])
def test_a_suffix_with_an_accent_strips(user_dict, c_locale, text, expected):
    """Hungarian case endings from packs/lang/hu.yaml (dictSuffixes).  Under
    the code-page locale the word was garbled before the suffix was compared,
    so "ban" stripped and "ból" never did."""
    add, prepare = user_dict("hu")
    add("ablak", "ABLAK_R")
    assert prepare(text) == expected


@pytest.mark.parametrize("lang, text", [
    ("hu", "Ész nélkül, Árvíztűrő TÜKÖRFÚRÓGÉP."),
    ("de", "Über alles, Ärger und Öl."),
    ("ru", "Привет мир"),
    ("es", "Ñandú. ¿Qué tal?"),
    ("pl", "Łódź i Żółć"),
])
def test_without_an_entry_the_text_is_untouched(user_dict, c_locale, lang, text):
    _add, prepare = user_dict(lang)
    assert prepare(text) == text


# ---------------------------------------------------------------------------
# #168: an entry with a hyphen in its word is used.
# ---------------------------------------------------------------------------
# Text preparation turns a hyphen between letters or digits into a space
# ("M5-series" -> "M5 series", #75) before the dictionary runs.  By then
# "wi-fi" was the two words "wi" and "fi", and an entry for "wi-fi" could
# never match: the editors took it, saved it and previewed it, and it did
# nothing.

EN_HYPHEN_ENTRIES = {"wi-fi": "why fy", "x-ray": "ex ray", "state-of-the-art": "stayt uv thee art"}


@pytest.mark.parametrize("text, expected", [
    ("wi-fi", "why fy"),
    ("Use wi-fi now", "Use why fy now"),
    ("Wi-Fi is WI-FI.", "why fy is why fy."),
    ("(wi-fi)", "( why fy )"),
    ("an x-ray machine", "an ex ray machine"),
    ("a state-of-the-art lab", "a stayt uv thee art lab"),
    # With a suffix from the pack (en: "s", "ed", "'s"), as for any other word.
    ("two x-rays", "two ex rays"),
    ("the wi-fi's name", "the why fy's name"),
    # Inside a longer hyphenated word: the entry's own hyphen is kept for it.
    ("wi-fi-enabled", "why fy enabled"),
    ("a non-x-ray scan", "a non ex ray scan"),
])
def test_an_entry_with_a_hyphen_is_used(user_dict, text, expected):
    add, prepare = user_dict("en-us")
    for word, to_text in EN_HYPHEN_ENTRIES.items():
        add(word, to_text)
    assert prepare(text) == expected


@pytest.mark.parametrize("text, expected", [
    ("az e-mail", "az ímél"),
    ("Az E-mail megjött.", "Az ímél megjött."),
    ("az e-mailben", "az ímélben"),      # hu case ending "ben"
    ("az e-mailből", "az ímélből"),      # "ből"
])
def test_a_hungarian_entry_with_a_hyphen_is_used(user_dict, c_locale, text, expected):
    add, prepare = user_dict("hu")
    add("e-mail", "ímél")
    assert prepare(text) == expected


@pytest.mark.parametrize("text, expected", [
    ("M5-series", "M5 series"),
    ("an F-16 and COVID-19", "an F 16 and COVID 19"),
    ("well-known wi-fi M5-series", "well known why fy M5 series"),
    ("wi-fix and fi-wi", "wi fix and fi wi"),
    ("-wi -fi wi- fi", "-wi -fi wi- fi"),
    ("----------", "----------"),
    ("a - b", "a - b"),
    ("2024-2025", "20 24 20 25"),
])
def test_other_hyphens_are_split_as_before(user_dict, text, expected):
    """#75 stays fixed with hyphenated entries in the dictionary."""
    add, prepare = user_dict("en-us")
    for word, to_text in EN_HYPHEN_ENTRIES.items():
        add(word, to_text)
    assert prepare(text) == expected


def test_a_part_of_a_hyphenated_word_is_still_looked_up(user_dict):
    add, prepare = user_dict("en-us")
    add("pentagon", "PENTA")
    assert prepare("pentagon") == "PENTA"
    assert prepare("this-pentagon-official") == "this PENTA official"


@pytest.mark.parametrize("lang, text, expected", [
    ("en-us", "Use wi-fi now, x-ray the M5-series", "Use wi fi now, x ray the M5 series"),
    ("en-gb", "a state-of-the-art e-mail", "a state of the art e mail"),
    ("hu", "az e-mail és az e-mailben", "az e mail és az e mailben"),
    ("de", "die E-Mail-Adresse", "die E Mail Adresse"),
    ("es", "un e-mail hispano-americano", "un e mail hispano americano"),
])
def test_without_a_hyphenated_entry_the_shipped_output_is_unchanged(user_dict, lang, text, expected):
    """No shipped pronunciation dictionary has a hyphen in a key: every
    language's output is byte for byte what it was."""
    _add, prepare = user_dict(lang)
    assert prepare(text) == expected


def test_a_masked_or_disabled_entry_does_not_hold_the_hyphen(frontend, pack_dir):
    h = frontend.create_handle(pack_dir)
    try:
        frontend.set_language(h, "en-us")
        frontend.set_dict_entry(h, "en-us", "wi-fi", '{"toText":"why fy"}')
        assert frontend.prepare_text(h, "wi-fi") == "why fy"
        # The pronunciation dictionary switched off for the language.
        frontend.set_dict_entry(h, "en-us", "pronounce", "false", dict_type="config")
        assert frontend.prepare_text(h, "wi-fi") == "wi fi"
        frontend.set_dict_entry(h, "en-us", "pronounce", "true", dict_type="config")
        # The entry masked.
        frontend.set_dict_entry(h, "en-us", "wi-fi", '{"masked":true}')
        assert frontend.prepare_text(h, "wi-fi") == "wi fi"
    finally:
        frontend.destroy_handle(h)

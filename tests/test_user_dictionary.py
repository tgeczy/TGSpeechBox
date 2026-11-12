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

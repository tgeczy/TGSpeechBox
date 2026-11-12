"""An IPA dictionary entry replaces its own word, accents or not (#165).

A pronunciation entry with to_ipa leaves its word in the text for the
phonemizer and has the frontend swap that word's IPA afterwards: the text's
words are walked against the IPA's words.  The walk kept only what
std::isalpha accepts, so in the C locale:

* a word of only non-ASCII letters (Portuguese "é", "à", Hungarian "ő", any
  Cyrillic word) was skipped as if it were punctuation, and every override
  after it landed one word early: "Ele é edu" replaced "é" and still spoke
  "edu";
* a word that starts or ends with such a letter ("você", "ész") lost it
  before the lookup, so its own entry was never found.

Under the Windows code-page locale NVDA runs in, the same code went wrong in
other places (a dash counted as a word).  Both locales must now agree.

Each case: prepareText on the text (which records the entry), then
queueIPA_ExWithText with the IPA eSpeak NG gives for it, at every rate.
"""
from __future__ import annotations

import locale

import pytest

from _test_frontend import read_frame_trace

SPEEDS = [0.5, 1.0, 1.6, 2.0, 3.0]
STOP3 = ["ʔ", "ʔ", "ʔ"]
STOP2 = ["ʔ", "ʔ"]

# User entries: the word, and IPA no phonemizer would give for it.
PT_ENTRIES = {"edu": "ʔ ʔ ʔ", "você": "ʔ ʔ", "américa": "ʔ ʔ ʔ"}


def _in_locale(name):
    before = locale.setlocale(locale.LC_CTYPE)
    try:
        locale.setlocale(locale.LC_CTYPE, name)
    except locale.Error:
        pytest.skip(f"no {name} locale on this system")
    yield name
    locale.setlocale(locale.LC_CTYPE, before)


@pytest.fixture(params=["C", "English_United States.1252"])
def c_locale(request):
    """The C runtime's character locale: "C" as the phone apps, SAPI and
    Linux run, and the Windows code page NVDA's Python (and this one) sets."""
    yield from _in_locale(request.param)


@pytest.fixture
def plain_c_locale():
    yield from _in_locale("C")


@pytest.fixture
def speak(frontend, pack_dir):
    """speak(lang, entries, text, ipa, speed) -> (phoneme keys, frames)."""
    handles = []

    def _speak(lang, entries, text, ipa, speed=1.0):
        h = frontend.create_handle(pack_dir)
        handles.append(h)
        frontend.set_language(h, lang)
        for word, to_ipa in entries.items():
            frontend.set_dict_entry(h, lang, word, '{"toText":"%s","toIpa":"%s"}' % (word, to_ipa))
        prepared = frontend.prepare_text(h, text)
        frames = frontend.capture_frames(h, ipa, speed=speed, base_pitch=110.0, text=prepared)
        keys = [t.phoneme_key for t in read_frame_trace(frontend, h)]
        return keys, [(f.is_silence, f.duration_ms, f.fade_ms, f.frame_dict) for f in frames]

    yield _speak
    for h in handles:
        frontend.destroy_handle(h)


@pytest.mark.parametrize("speed", SPEEDS)
@pytest.mark.parametrize("text, ipa, expected", [
    # The control: no accented word before the entry's word.
    ("Ele edu", "ˈeli ˈedu", ["e", "l", "i"] + STOP3),
    # A word of one accented letter before it.
    ("Ele é edu", "ˈeli ˈɛ ˈedu", ["e", "l", "i", "ɛ"] + STOP3),
    ("Ele à edu", "ˈeli ˈa ˈedu", ["e", "l", "i", "a"] + STOP3),
    # The entry's own word ends, or starts, with an accented letter.
    ("Ele é você", "ˈeli ˈɛ vosˈe", ["e", "l", "i", "ɛ"] + STOP2),
    ("Você é edu", "vosˈe ˈɛ ˈedu", STOP2 + ["ɛ"] + STOP3),
    # An accent inside the word, and a capital.
    ("a américa", "a amˈɛɾikɐ", ["a"] + STOP3),
    ("A América é edu", "a amˈɛɾikɐ ˈɛ ˈedu", ["a"] + STOP3 + ["ɛ"] + STOP3),
    # Punctuation is still no word: a dash, quotes, an ellipsis.
    ("Ele — é edu", "ˈeli ˈɛ ˈedu", ["e", "l", "i", "ɛ"] + STOP3),
    ("Ele é “edu”…", "ˈeli ˈɛ ˈedu", ["e", "l", "i", "ɛ"] + STOP3),
])
def test_portuguese_entry_replaces_its_own_word(speak, c_locale, speed, text, ipa, expected):
    keys, _frames = speak("pt-br", PT_ENTRIES, text, ipa, speed)
    assert keys == expected


@pytest.mark.parametrize("speed", SPEEDS)
def test_an_accented_word_before_the_entry_changes_nothing_else(speak, c_locale, speed):
    """The same IPA with "é" written "e": every frame is the same, at every
    rate.  (The text only places the override; the IPA is given.)"""
    ipa = "ˈeli ˈɛ ˈedu"
    _k1, with_accent = speak("pt-br", PT_ENTRIES, "Ele é edu", ipa, speed)
    _k2, without = speak("pt-br", PT_ENTRIES, "Ele e edu", ipa, speed)
    assert with_accent == without


@pytest.mark.parametrize("speed", SPEEDS)
def test_shipped_english_entry_after_an_accented_word(speak, c_locale, speed):
    """en-us ships an IPA entry for "knievel".  After "à", its IPA replaced
    "la" and knievel was spoken twice."""
    ipa = "ˌɑː lˌɑː knˈiːvəl"
    keys, frames = speak("en-us", {}, "à la knievel", ipa, speed)
    plain_keys, plain_frames = speak("en-us", {}, "a la knievel", ipa, speed)
    assert keys == plain_keys
    assert keys.count("v") == 1, "knievel is spoken once"
    assert frames == plain_frames


@pytest.mark.parametrize("speed", SPEEDS)
def test_hungarian_entry_after_a_one_letter_word(speak, c_locale, speed):
    keys, _frames = speak("hu", {"ész": "ʔ ʔ ʔ"}, "Az ő Ész", "ɒz ˈøː ˈeːs", speed)
    assert keys == ["ᴒ", "z", "ő"] + STOP3


@pytest.mark.parametrize("speed", SPEEDS)
@pytest.mark.parametrize("entries, expected", [
    ({"мир": "ʔ ʔ ʔ"}, ["p", "r", "ɪ", "v", "e", "t", "t"] + STOP3),
    ({"привет": "ʔ ʔ ʔ"}, STOP3 + ["m", "i", "r"]),
])
def test_cyrillic_words_are_words(speak, plain_c_locale, speed, entries, expected):
    """In the C locale only.  Under the Windows code-page locale an earlier
    step of the text parser (splitMixedTokens) takes some bytes of Cyrillic
    letters for digits and cuts the word in two; that step also lines up
    stress and compound entries, so it is not changed here."""
    keys, _frames = speak("ru", entries, "Привет мир", "prʲɪvʲˈet mʲˈir", speed)
    assert keys == expected


@pytest.mark.parametrize("speed", SPEEDS)
@pytest.mark.parametrize("text, ipa, expected", [
    # A sign the phonemizer says as a word takes that word's IPA.  eSpeak NG
    # (1.52 and 1.53, pt-br): "° edu" is "ɡrˈaʊ edˈu" (grau edu), "× edu" is
    # "vˈezyz edˈu" (vezes edu), "€ edu" is "ˈeʊro edˈu".
    ("° edu", "ɡɾˈaw ˈedu", ["ɡ", "ᵊ", "ɾ", "a", "w"] + STOP3),
    ("× edu", "vˈezis ˈedu", ["v", "e", "z", "i", "s"] + STOP3),
    ("€ edu", "ˈewro ˈedu", None),
    ("§ edu", "paɾˈaɡɾafo ˈedu", None),
    # Marks it does not say are still no word: guillemets, inverted marks, a
    # middle dot, a no-break space, a bullet, a dagger.
    ("« edu »", "ˈedu", STOP3),
    ("¿ edu ?", "ˈedu", STOP3),
    ("Ele • edu", "ˈeli ˈedu", ["e", "l", "i"] + STOP3),
    ("Ele † edu", "ˈeli ˈedu", ["e", "l", "i"] + STOP3),
])
def test_a_spoken_sign_is_a_word_and_a_silent_mark_is_not(speak, c_locale, speed, text, ipa, expected):
    # Found in review of #165: a standalone non-ASCII sign before the entry's
    # word put the entry on the sign's word and left "edu" spoken (it worked
    # by accident under NVDA's code-page locale before).
    keys, _frames = speak("pt-br", PT_ENTRIES, text, ipa, speed)
    assert keys[-3:] == STOP3, f"{text!r}: the entry did not replace its own word: {keys}"
    assert keys.count("ʔ") == 3, f"{text!r}: {keys}"
    if expected is not None:
        assert keys == expected


@pytest.mark.parametrize("speed", SPEEDS)
@pytest.mark.parametrize("text, ipa", [
    # Digits and symbols take no IPA word in this walk, as before.
    ("Ele 25 edu", "ˈeli ˈedu"),
    ("Ele % edu", "ˈeli ˈedu"),
])
def test_digits_and_symbols_are_walked_as_before(speak, c_locale, speed, text, ipa):
    keys, _frames = speak("pt-br", PT_ENTRIES, text, ipa, speed)
    assert keys == ["e", "l", "i"] + STOP3


@pytest.mark.parametrize("speed", SPEEDS)
@pytest.mark.parametrize("lang, text, ipa", [
    ("pt-br", "Ele é bom, você sabe.", "ˈeli ˈɛ bˈoŋ vosˈe sˈabi"),
    ("en-us", "à la carte café menu", "ˌɑː lˌɑː kˈɑːɹt kæfˈeɪ mˈɛnjuː"),
    ("hu", "Az ő Ész", "ɒz ˈøː ˈeːs"),
])
def test_without_an_ipa_entry_in_the_clause_the_text_changes_no_frame(speak, c_locale, speed, lang, text, ipa):
    """No entry of the clause has IPA: the walk does not run, and accented
    words in the text leave the frames what the IPA alone gives them where
    the language has no stress or compound dictionary."""
    _keys, frames = speak(lang, {}, text, ipa, speed)
    _keys2, again = speak(lang, {"zzzunused": "ʔ"}, text, ipa, speed)
    assert frames == again


# ---------------------------------------------------------------------------
# #168: a word with a hyphen in it, and IPA entries.
# ---------------------------------------------------------------------------
# An entry for "wi-fi" or "x-ray" is now used through its respelling
# (tests/test_user_dictionary.py).  One that gives IPA is still not used: the
# IPA is swapped in word for word, and a hyphenated word does not always come
# back from the phonemizer as one word with one stress (eSpeak NG gives
# "x-ray" as "ˈɛksɹˈeɪ", which the text parser cuts in two).  With such an
# entry in the dictionary the text and every frame are what they were.

@pytest.mark.parametrize("speed", SPEEDS)
@pytest.mark.parametrize("text, ipa", [
    ("Use wi-fi now", "jˈuːz wˈaɪ fˈaɪ nˈaʊ"),
    ("An X-ray, then wi-fi.", "ɐn ˈɛks ɹˈeɪ ðˈɛn wˈaɪ fˈaɪ"),
    ("the well-known M5-series", "ðə wˈɛl nˈoʊn ˌɛm fˈaɪv sˈɪɹiːz"),
])
def test_an_ipa_entry_for_a_hyphenated_word_changes_nothing(speak, c_locale, speed, text, ipa):
    keys, frames = speak("en-us", {"wi-fi": "ʔ ʔ ʔ", "x-ray": "ʔ ʔ ʔ"}, text, ipa, speed)
    _plain_keys, plain_frames = speak("en-us", {}, text, ipa, speed)
    assert "ʔ" not in keys
    assert frames == plain_frames

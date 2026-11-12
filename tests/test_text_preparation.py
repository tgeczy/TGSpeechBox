"""Regression tests for nvspFrontend_prepareText().

prepareText runs BEFORE eSpeak phonemization. It handles:
  - Letter dictionary lookups (single codepoint -> letter description)
  - Compound word splitting / merging
  - Number/year/thousands-separator normalization
  - Pronunciation dictionary substitutions

These tests do NOT exercise the synthesis pipeline (no DLL frame callbacks
needed), so they're fast and deterministic. They pin specific historic bugs.
"""
from __future__ import annotations


# ---------------------------------------------------------------------------
# Letter dict: single-codepoint inputs should return the letter description.
# ---------------------------------------------------------------------------

def test_es_mx_letter_y_returns_description(es_mx):
    """Pinned by `b557e41` (letter dict was unwired pre-3.10).

    A bare 'y' in Spanish should expand to "i griega", not be passed through
    to eSpeak as a literal 'y' (which would phonemize as /i/).
    """
    fe, h = es_mx
    out = fe.prepare_text(h, "y")
    assert "i griega" in out.lower(), (
        f"Expected 'i griega' in es-mx letter expansion of 'y', got: {out!r}"
    )


def test_es_mx_letter_enye_returns_description(es_mx):
    """'ñ' in Spanish should expand to "eñe" via the letter dict (es-letters.tsv).

    Complements the 'y' test: covers a non-ASCII single codepoint, exercising
    both the letter dict lookup AND the UTF-8 decoding path in prepareText.
    """
    fe, h = es_mx
    out = fe.prepare_text(h, "ñ")
    assert "eñe" in out.lower(), (
        f"Expected 'eñe' in es-mx letter expansion of 'ñ', got: {out!r}"
    )


def test_es_mx_plain_ascii_letter_passes_through(es_mx):
    """Sanity: a plain ASCII letter NOT in es-letters.tsv passes through unchanged.

    The Spanish letter dict only contains letters that need *special* handling
    (accented vowels, 'ñ', 'y'). Plain letters like 'h' are intentionally not
    listed — eSpeak's default letter spelling handles them.
    """
    fe, h = es_mx
    out = fe.prepare_text(h, "h")
    assert out == "h", f"Plain 'h' should pass through unchanged; got: {out!r}"


def test_en_us_multi_char_input_passes_through(en_us):
    """A multi-character input should NOT trigger letter dict lookup.

    Letter dict only fires for single codepoints. 'hello' must pass through
    as-is for eSpeak to phonemize normally.
    """
    fe, h = en_us
    out = fe.prepare_text(h, "hello")
    assert "hello" in out.lower(), (
        f"Multi-char input shouldn't trigger letter dict; got: {out!r}"
    )


# ---------------------------------------------------------------------------
# Numbers: thousands separators must NOT split clauses.
# ---------------------------------------------------------------------------
# Pinned by `fb95777`+`4de1c5c`: pre-fix, "65,543" was being split into
# clauses ("65" + "543") on all platforms because the comma was treated
# as a clause boundary even between digits.

def test_thousands_separator_comma_no_clause_split_es_mx(es_mx):
    """Issue #74-adjacent: '65,543' should stay as one number."""
    fe, h = es_mx
    out = fe.prepare_text(h, "65,543")
    # The exact form depends on whether thousandsSeparatorCommaToSpace is on,
    # but in NO case should we see a clause-style break that separates 65 from 543.
    # A literal '65,543' or '65 543' both indicate the comma was not treated
    # as a clause splitter.
    assert ("65,543" in out or "65 543" in out or "65543" in out), (
        f"'65,543' should not be clause-split; got: {out!r}"
    )


def test_thousands_separator_period_no_clause_split_en_us(en_us):
    """'3.14' decimal point should not be a clause boundary."""
    fe, h = en_us
    out = fe.prepare_text(h, "3.14")
    # The "." between digits must not become an end-of-clause.
    assert "3.14" in out or "3 point 14" in out, (
        f"'3.14' decimal must not be clause-split; got: {out!r}"
    )


# ---------------------------------------------------------------------------
# Year splitting: pure 4-digit numbers ending in "00" should NOT split.
# ---------------------------------------------------------------------------
# Pinned by `7c742a2`: pre-fix, "4000" split to "40 00" and was read as
# "forty oh zero". The fix: skip year-style splitting when the second pair
# is "00" — eSpeak handles "four thousand" naturally.

def test_year_with_double_zero_not_split_en_us(en_us):
    """Memory: '4000' must not split into '40 00' (would say 'forty oh zero')."""
    fe, h = en_us
    out = fe.prepare_text(h, "4000")
    # Must NOT contain the broken split form.
    # Acceptable: "4000" verbatim, or expansion to "four thousand" / similar.
    assert "40 00" not in out, (
        f"'4000' should not split as '40 00' (would say 'forty oh zero'); got: {out!r}"
    )


def test_year_normal_split_still_works_en_us(en_us):
    """Sanity: '1985' should still split into '19 85' for natural year reading."""
    fe, h = en_us
    out = fe.prepare_text(h, "1985")
    # Either "19 85" (split form) or "1985" (eSpeak handles directly) is fine.
    # We just confirm the function doesn't crash and returns sensible output.
    assert any(c.isdigit() for c in out), (
        f"Expected digits in year output; got: {out!r}"
    )


# ---------------------------------------------------------------------------
# A handle per language, for tests that compare languages.
# ---------------------------------------------------------------------------

import pytest


@pytest.fixture
def prepare(frontend, pack_dir):
    """prepare(lang, text): prepareText's output for `text` in language `lang`."""
    handles = {}

    def _prepare(lang, text):
        if lang not in handles:
            handles[lang] = frontend.create_handle(pack_dir)
            frontend.set_language(handles[lang], lang)
        return frontend.prepare_text(handles[lang], text)

    yield _prepare
    for h in handles.values():
        frontend.destroy_handle(h)


# ---------------------------------------------------------------------------
# #157: a number grouped with a comma is a count, not a year.
# ---------------------------------------------------------------------------
# English strips the grouping comma ("1,234" -> "1234") so eSpeak reads one
# number, and splits four-digit years into pairs ("1985" -> "19 85").  The
# year step ran on the text with the comma already gone, so every count from
# 1,001 to 9,999 was read as a year: "1,234 people" said "twelve thirty-four".

ENGLISH = ["en-us", "en-gb", "en"]


@pytest.mark.parametrize("lang", ENGLISH)
@pytest.mark.parametrize("text, expected", [
    ("1,234 people came", "1234 people came"),
    ("Population 7,345", "Population 7345"),
    ("$2,499", "$2499"),
    ("2,019 votes", "2019 votes"),
    ("in 1,066 AD", "in 1066 AD"),
    ("(1,234)", "( 1234 )"),
    ("1,234.", "1234."),
    ("1,709 and 1709", "1709 and 17 oh 9"),
    ("in 1985, and 1,985 more", "in 19 85, and 1985 more"),
])
def test_comma_grouped_number_is_not_a_year(prepare, lang, text, expected):
    assert prepare(lang, text) == expected


@pytest.mark.parametrize("lang", ENGLISH)
@pytest.mark.parametrize("text, expected", [
    ("1985", "19 85"),
    ("1999-2004", "19 99 2004"),
    ("4000", "4000"),
    ("4,000 people", "4000 people"),
    ("12,345 people", "12345 people"),
    ("1,234,567 items", "1234567 items"),
    ("3,14", "3,14"),
])
def test_years_and_other_numbers_read_as_before(prepare, lang, text, expected):
    assert prepare(lang, text) == expected


@pytest.mark.parametrize("lang", ["hu", "de", "es", "es-mx", "pt-br", "fr", "ru"])
@pytest.mark.parametrize("text, expected", [
    ("1,234 people came", "1 234 people came"),
    ("$2,499", "$2 499"),
    ("2,019 votes", "2 019 votes"),
    ("1985", "1985"),
    ("1,234,567", "1 234 567"),
])
def test_grouped_numbers_in_other_languages_are_unchanged(prepare, lang, text, expected):
    """These languages turn the comma into a space and split no years: their
    output is byte for byte what it was."""
    assert prepare(lang, text) == expected


# ---------------------------------------------------------------------------
# #166: English clock times.
# ---------------------------------------------------------------------------
# English rewrites "6:03" as "6 oh 3" and "5:00" as "5 o'clock" before the
# phonemizer.  Two cases came out worse than eSpeak NG reads them unaided:
#
# * the hour kept its leading zero: "08:15" became "08 15", "zero eight
#   fifteen", where eSpeak NG alone says "eight fifteen";
# * a time with seconds was taken for hours and minutes with something after
#   it: "10:00:00" became "10 o'clock:00", "ten o'clock colon zero zero".

@pytest.mark.parametrize("lang", ENGLISH)
@pytest.mark.parametrize("text, expected", [
    ("Train at 08:15", "Train at 8 15"),
    ("07:05", "7 oh 5"),
    ("09:00", "9 o'clock"),
    ("01:30 and 09:59.", "1 30 and 9 59."),
    # As NVDA sends it when it speaks the colon.
    ("08 colon 15", "8 15"),
    ("07 colon 05", "7 oh 5"),
])
def test_a_leading_zero_on_the_hour_is_not_read(prepare, lang, text, expected):
    assert prepare(lang, text) == expected


@pytest.mark.parametrize("lang", ENGLISH)
@pytest.mark.parametrize("text", [
    "10:00:00",
    "1:00:30",
    "12:34:56",
    "Elapsed 0:05:09 so far",
    "08:15:00",
    "100:00:00",
    "1:02:03:04",
    "10 colon 00 colon 00",
    "1 colon 00 colon 30",
])
def test_a_time_with_seconds_is_left_to_the_phonemizer(prepare, lang, text):
    out = prepare(lang, text)
    assert out == text
    assert "o'clock" not in out and " oh " not in out


@pytest.mark.parametrize("lang", ENGLISH)
@pytest.mark.parametrize("text, expected", [
    ("6:03", "6 oh 3"),
    ("12:45", "12 45"),
    ("5:00", "5 o'clock"),
    ("At 5:44 today", "At 5 44 today"),
    ("5:00: we left", "5 o'clock: we left"),
    ("10:30, then 11:00.", "10 30, then 11 o'clock."),
    ("6 colon 03", "6 oh 3"),
    ("23:59", "23 59"),
    ("24:00", "24:00"),
    ("5:60", "5:60"),
    ("ratio 3:1", "ratio 3:1"),
    # Midnight's hour is not touched: how "00" should be read is not decided here.
    ("00:30", "00 30"),
    ("00:00", "00 o'clock"),
    # Not changed by #166: "o'clock" before AM or PM.
    ("10:00 AM", "10 o'clock A. M."),
    ("5:00 PM", "5 o'clock PM"),
])
def test_other_times_read_as_before(prepare, lang, text, expected):
    assert prepare(lang, text) == expected


@pytest.mark.parametrize("lang", ["de", "hu", "es", "pt-br", "fr", "ru", "pl"])
@pytest.mark.parametrize("text", ["08:15", "07:05", "10:00:00", "1:00:30", "5:00", "08 colon 15"])
def test_times_in_other_languages_are_untouched(prepare, lang, text):
    """Only English rewrites times."""
    assert prepare(lang, text) == text


# ---------------------------------------------------------------------------
# #167: a comma that cannot be a thousands separator is left alone.
# ---------------------------------------------------------------------------
# A digit, a comma and exactly three digits were always taken for a grouped
# number: the comma became a space where the pack sets
# thousandsSeparatorCommaToSpace, and was stripped elsewhere.  In a language
# that writes decimals with a comma that turned three-place decimals into
# thousands: German "0,125" was read "null tausend einhundertfünfundzwanzig".
# "1,500" may be either and stays a grouped number (the 3.01 choice).  Three
# shapes can never be one, in any language:
#
#   "0,125"      a lone zero before the comma
#   "12345,678"  more than three digits before it
#   "1.234,567"  a number already grouped with dots

# Every pack that sets thousandsSeparatorCommaToSpace.
COMMA_TO_SPACE = ["bg", "cs", "da", "de", "es", "fi", "fr", "hr", "hu", "it", "nl", "pl", "pt",
                  "ro", "ru", "sk", "sv", "tr", "uk", "es-mx", "pt-br"]
# Packs that strip a grouping comma, and split no years.
COMMA_STRIPPED = ["zh"]

NEVER_THOUSANDS = [
    "0,125",
    "Zinssatz 0,125 Prozent",
    "0,375 liter",
    "12345,678",
    "1.234,567",
    "1.234.567,891 Euro",
    "(0,125)",
    "0,125.",
    "x 0,125, y 0,250",
]


@pytest.mark.parametrize("lang", COMMA_TO_SPACE + COMMA_STRIPPED + ENGLISH)
@pytest.mark.parametrize("text", NEVER_THOUSANDS)
def test_a_comma_that_cannot_group_thousands_is_kept(prepare, lang, text):
    expected = text.replace("(", "( ").replace(")", " )")  # brackets are spaced out by an earlier step
    assert prepare(lang, text) == expected


GROUPED = [
    # text, with the comma as a space, with the comma stripped
    ("1,213", "1 213", "1213"),
    ("1,500 Euro", "1 500 Euro", "1500 Euro"),
    ("65,543", "65 543", "65543"),
    ("10,000", "10 000", "10000"),
    ("100,000 Ft", "100 000 Ft", "100000 Ft"),
    ("1,234,567", "1 234 567", "1234567"),
    ("1,000,000", "1 000 000", "1000000"),
    ("999,999.", "999 999.", "999999."),
    ("44,100 Hz", "44 100 Hz", "44100 Hz"),
    ("20,000 and 0,125", "20 000 and 0,125", "20000 and 0,125"),
    # Not grouped at all: fewer or more than three digits after the comma.
    ("0,5", "0,5", "0,5"),
    ("3,14", "3,14", "3,14"),
    ("1,2345", "1,2345", "1,2345"),
    ("a, b, 1, 2", "a, b, 1, 2", "a, b, 1, 2"),
    ("3.14", "3.14", "3.14"),
]


@pytest.mark.parametrize("lang", COMMA_TO_SPACE)
@pytest.mark.parametrize("text, spaced, _stripped", GROUPED)
def test_grouped_numbers_are_spaced_as_before(prepare, lang, text, spaced, _stripped):
    assert prepare(lang, text) == spaced


@pytest.mark.parametrize("lang", COMMA_STRIPPED)
@pytest.mark.parametrize("text, _spaced, stripped", GROUPED)
def test_grouped_numbers_are_stripped_as_before(prepare, lang, text, _spaced, stripped):
    assert prepare(lang, text) == stripped


@pytest.mark.parametrize("lang", ENGLISH)
@pytest.mark.parametrize("text, expected", [
    ("0,125", "0,125"),
    ("44,100 Hz", "44100 Hz"),
    ("1,234", "1234"),
    ("1,234,567", "1234567"),
    ("10,000", "10000"),
    ("0,5", "0,5"),
])
def test_english_grouped_numbers(prepare, lang, text, expected):
    assert prepare(lang, text) == expected

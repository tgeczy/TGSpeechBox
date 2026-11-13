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

import pathlib
import shutil
import sys

import pytest

REPO = pathlib.Path(__file__).resolve().parent.parent


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


# Hungarian is not in this list: there one comma is a decimal comma (#167, below).
@pytest.mark.parametrize("lang", ["de", "es", "es-mx", "pt-br", "fr", "ru"])
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
])
def test_other_times_read_as_before(prepare, lang, text, expected):
    assert prepare(lang, text) == expected


@pytest.mark.parametrize("lang", ["de", "hu", "es", "pt-br", "fr", "ru", "pl"])
@pytest.mark.parametrize("text", ["08:15", "07:05", "10:00:00", "1:00:30", "5:00", "08 colon 15"])
def test_times_in_other_languages_are_untouched(prepare, lang, text):
    """Only English rewrites times."""
    assert prepare(lang, text) == text


@pytest.mark.parametrize("lang", ["de", "hu", "es", "pt-br", "fr", "ru", "pl"])
@pytest.mark.parametrize("text", ["10:00 AM", "5:00 PM", "10:00 a.m.", "5:00 pm", "10 colon 00 AM"])
def test_times_before_am_or_pm_in_other_languages_are_untouched(prepare, lang, text):
    """No pack but the English one lists the marks, and only English rewrites times."""
    # Spanish reads an all-caps word it can pronounce as a word (#146): "AM" is "am" there.
    expected = text.replace("AM", "am") if lang == "es" else text
    assert prepare(lang, text) == expected


# The third part of #166: "10:00 AM" said "ten o'clock A. M.".  A full hour
# with the mark of the twelve-hour clock after it is read as the hour alone,
# "ten A M".  The marks are words, so they are not in the C++: they are the
# English pack's twelveHourClockWords list, and the time step only asks
# whether a word of the pack's list follows the time.

ALL_ENGLISH = ENGLISH + ["en-au", "en-ca"]


@pytest.mark.parametrize("lang", ALL_ENGLISH)
@pytest.mark.parametrize("text, expected", [
    # A later step writes an upper-case "AM" as "A. M." so that it is spelled.
    ("10:00 AM", "10 A. M."),
    ("5:00 PM", "5 PM"),
    ("10:00 am", "10 am"),
    ("5:00 pm", "5 pm"),
    ("10:00 Am", "10 Am"),
    ("10:00 a.m.", "10 a.m."),
    ("5:00 p.m.", "5 p.m."),
    ("10:00 A.M.", "10 A.M."),
    ("5:00 P.M. sharp", "5 P.M. sharp"),
    ("Open from 9:00 AM to 5:00 PM.", "Open from 9 A. M. to 5 PM."),
    ("We met at 7:00 p.m.", "We met at 7 p.m."),
    ("09:00 AM", "9 A. M."),
    ("12:00 PM", "12 PM"),
    # Written without a space, and with two.
    ("10:00AM", "10A. M."),
    ("5:00pm", "5pm"),
    ("5:00  PM", "5  PM"),
    # Punctuation after the mark.
    ("(5:00 PM)", "( 5 PM )"),
    ("5:00 PM, then home", "5 PM, then home"),
    ("at 5:00 pm-ish", "at 5 pm ish"),
    ("“come at 5:00 PM”", "“come at 5 PM”"),
    # As NVDA sends it when it speaks the colon.
    ("10 colon 00 AM", "10 A. M."),
    ("5 colon 00 PM", "5 PM"),
    # One time with a mark, one without.
    ("10:00 AM and 11:00", "10 A. M. and 11 o'clock"),
])
def test_a_full_hour_before_am_or_pm_has_no_oclock(prepare, lang, text, expected):
    assert prepare(lang, text) == expected


@pytest.mark.parametrize("lang", ENGLISH)
@pytest.mark.parametrize("space", [" ", " "])
def test_a_no_break_space_before_am_or_pm_is_a_space(prepare, lang, space):
    """Time formats put U+202F or U+00A0 before the mark; the frontend has
    made it a space by the time the time step runs, on every platform
    (stripInvisible; tests/cpp/test_space_variants.cpp).  This was skipped
    off Windows as the host's job, which no host does."""
    assert prepare(lang, f"10:00{space}AM") == "10 A. M."
    assert prepare(lang, f"5:00{space}PM") == "5 PM"


@pytest.mark.parametrize("lang", ALL_ENGLISH)
@pytest.mark.parametrize("text, expected", [
    # The mark is a whole word: these only begin like one.
    ("10:00 Amsterdam time", "10 o'clock Amsterdam time"),
    ("5:00 PMs", "5 o'clock PMs"),
    ("10:00 amid the noise", "10 o'clock amid the noise"),
    ("10:00 AM2", "10 o'clock A. M.2"),
    ("5:00 pmé", "5 o'clock pmé"),
    # Something other than spaces between the time and the mark.
    ("5:00, PM said", "5 o'clock, PM said"),
    ("5:00. AM radio", "5 o'clock. A. M. radio"),
    ("5:00/PM", "5 o'clock/PM"),
    # A spelling the pack does not list.
    ("5:00 pM", "5 o'clock pM"),
    ("5:00 P. M.", "5 o'clock P. M."),
    # No mark at all.
    ("5:00", "5 o'clock"),
    ("5:00 today", "5 o'clock today"),
    ("5:00 ", "5 o'clock "),
])
def test_a_full_hour_before_anything_else_keeps_its_oclock(prepare, lang, text, expected):
    assert prepare(lang, text) == expected


@pytest.mark.parametrize("lang", ALL_ENGLISH)
@pytest.mark.parametrize("text, expected", [
    # Only the full hour had an "o'clock" to lose.
    ("10:30 AM", "10 30 A. M."),
    ("10:05 PM", "10 oh 5 PM"),
    ("6:03 a.m.", "6 oh 3 a.m."),
    ("10:00:00 AM", "10:00:00 A. M."),
    ("10 AM", "10 A. M."),
    ("5 PM", "5 PM"),
    ("I AM here at 5:00", "I A. M. here at 5 o'clock"),
])
def test_other_times_with_am_or_pm_read_as_before(prepare, lang, text, expected):
    assert prepare(lang, text) == expected


def _lang_yaml_without(packs_root, lang, key):
    """Take the setting `key` (one line, or a key and its list items) out of a
    copy of a pack; returns the lines taken out."""
    path = packs_root / "packs" / "lang" / f"{lang}.yaml"
    kept, taken, in_list = [], [], False
    for line in path.read_text(encoding="utf-8").splitlines(keepends=True):
        if line.startswith(f"  {key}:"):
            taken.append(line)
            in_list = line.strip() == f"{key}:"
        elif in_list and line.startswith("    - "):
            taken.append(line)
        else:
            in_list = False
            kept.append(line)
    path.write_text("".join(kept), encoding="utf-8", newline="")
    return taken


@pytest.fixture
def prepare_with_packs(frontend, tmp_path):
    """prepare(lang, text) on a copy of the packs that `edit(packs_root)` has changed."""
    handles = []

    def _make(edit):
        root = tmp_path / f"packs{len(handles)}"
        shutil.copytree(REPO / "packs", root / "packs")
        edit(root)

        def _prepare(lang, text):
            h = frontend.create_handle(str(root))
            handles.append(h)
            frontend.set_language(h, lang)
            return frontend.prepare_text(h, text)
        return _prepare

    yield _make
    for h in handles:
        frontend.destroy_handle(h)


@pytest.mark.parametrize("lang", ENGLISH)
def test_without_the_packs_list_every_full_hour_keeps_its_oclock(prepare_with_packs, lang):
    """The words are the pack's: take the list out and the time step knows none."""
    taken = []
    prepare = prepare_with_packs(lambda root: taken.extend(_lang_yaml_without(root, "en", "twelveHourClockWords")))
    assert len(taken) > 4, "packs/lang/en.yaml has no twelveHourClockWords list"
    assert prepare(lang, "10:00 AM") == "10 o'clock A. M."
    assert prepare(lang, "5:00 PM") == "5 o'clock PM"
    assert prepare(lang, "5:00 p.m.") == "5 o'clock p.m."
    assert prepare(lang, "10:30 AM") == "10 30 A. M."


@pytest.mark.parametrize("lang", ENGLISH)
def test_the_time_step_knows_only_the_words_of_the_pack(prepare_with_packs, lang):
    """A pack with other words, in any script, gets those and no others."""
    def edit(root):
        _lang_yaml_without(root, "en", "twelveHourClockWords")
        path = root / "packs" / "lang" / "en.yaml"
        text = path.read_text(encoding="utf-8")
        assert "\n  yearSplittingEnabled: true\n" in text
        path.write_text(text.replace(
            "\n  yearSplittingEnabled: true\n",
            '\n  yearSplittingEnabled: true\n  twelveHourClockWords: ["xm", "π.μ.", "de la tarde"]\n', 1),
            encoding="utf-8", newline="")

    prepare = prepare_with_packs(edit)
    assert prepare(lang, "5:00 xm") == "5 xm"
    assert prepare(lang, "5:00 π.μ.") == "5 π.μ."
    assert prepare(lang, "5:00 de la tarde") == "5 de la tarde"
    assert prepare(lang, "5:00 PM") == "5 o'clock PM"
    assert prepare(lang, "5:00 xmas") == "5 o'clock xmas"
    assert prepare(lang, "5:00 π.μ.α") == "5 o'clock π.μ.α"


def test_only_the_english_pack_lists_twelve_hour_clock_words():
    """No list, no change: every other pack reads times as it did."""
    with_list = sorted(p.stem for p in (REPO / "packs" / "lang").glob("*.yaml")
                       if "twelveHourClockWords" in p.read_text(encoding="utf-8"))
    assert with_list == ["en"]


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


COMMA_GROUPS = COMMA_TO_SPACE + ["es-es", "pt-pt"]


@pytest.mark.parametrize("lang", COMMA_GROUPS)
@pytest.mark.parametrize("text, spaced, _stripped", GROUPED)
def test_grouped_numbers_are_spaced_as_before(prepare, lang, text, spaced, _stripped):
    assert prepare(lang, text) == spaced


@pytest.mark.parametrize("lang", COMMA_STRIPPED)
@pytest.mark.parametrize("text, _spaced, stripped", GROUPED)
def test_grouped_numbers_are_stripped_as_before(prepare, lang, text, _spaced, stripped):
    assert prepare(lang, text) == stripped


@pytest.mark.parametrize("lang", ALL_ENGLISH)
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


# ---------------------------------------------------------------------------
# #167: where a pack says the comma is its decimal mark, "1,500" is a decimal.
# ---------------------------------------------------------------------------
# What #167 left open.  A digit, a comma and three digits ("1,500") can be a
# grouped number or a decimal, and which one is a matter of the language: a
# pack can say so with decimalSeparator.  With "," a number with one comma
# keeps it for the phonemizer.  A number with several commas ("1,234,567",
# "1,234,56"), or with a dot and more digits after the group ("1,234.56"), is
# a decimal in no language and is grouped as before.
#
# No shipped pack sets it.  Hungarian was the first candidate: its rule book
# writes decimals with a comma and groups thousands with a space or a dot (A
# magyar helyesírás szabályai, 12th edition, Akadémiai Kiadó 2015, rules 274,
# 291 b and 293).  The maintainer, a native speaker, decided against it:
# English-style numbers are what a screen reader user meets most, and "10,000"
# read as "tíz egész nulla ezred" would confuse people.  So the tests below
# run on a copy of the Hungarian pack with the setting added.

def _hu_with_decimal_comma(prepare_with_packs):
    return prepare_with_packs(lambda root: _set_in_lang_yaml(root, "hu", 'decimalSeparator: ","'))


ONE_COMMA = [
    "1,500",
    "1,213",
    "1,500 Ft",
    "Az ára 1,500 Ft.",
    "65,543",
    "10,000",
    "100,000 Ft",
    "999,999.",
    "44,100 Hz",
    "250,000",
    "$2,499",
    "2,019 votes",
    "1,234 people came",
    "(1,500)",
    "1,500, 2,750",
    "20,000 and 0,125",
    "1,500 és 2,750",
    # A dot that ends the sentence, or an ordinal's, is no decimal point.
    "Az ára 1,500.",
    "1,500. Utána",
    # The rule book's own example: thousands with a space, then the decimals.
    "20 611,413",
]


def test_one_comma_is_a_decimal_comma_where_the_pack_says_so(prepare_with_packs):
    prepare = _hu_with_decimal_comma(prepare_with_packs)
    for text in ONE_COMMA:
        expected = text.replace("(", "( ").replace(")", " )")  # brackets are spaced out by an earlier step
        assert prepare("hu", text) == expected


NOT_A_DECIMAL = [
    # Several commas: not a decimal, grouped as before.
    ("1,234,567", "1 234 567"),
    ("1,000,000", "1 000 000"),
    ("12,345,678 Ft", "12 345 678 Ft"),
    ("1,234,567,890", "1 234 567 890"),
    ("(1,234,567)", "( 1 234 567 )"),
    # Another comma after the group, whatever follows it (found in review:
    # these kept their first comma and were read "egy egész ... ezred").
    ("1,234,56", "1 234,56"),
    ("1,500,25", "1 500,25"),
    ("1,234,5678", "1 234,5678"),
    ("1,23,456", "1,23 456"),
    ("1,500,2,750", "1 500,2 750"),
    ("1,500,00", "1 500,00"),
    # A dot and more digits after the group: the dot is the decimal mark
    # there (an English number), so the comma is not.
    ("1,234.56", "1 234.56"),
    ("$1,500.00", "$1 500.00"),
    ("12,345,678.90", "12 345 678.90"),
    # Decimals that were kept already.
    ("3,14", "3,14"),
    ("0,5", "0,5"),
    ("1,2345", "1,2345"),
    ("0,375 liter", "0,375 liter"),
    ("12345,678", "12345,678"),
    ("1.234,567", "1.234,567"),
    # No comma in the number.
    ("1 500", "1 500"),
    ("1.500", "1.500"),
    ("1500", "1500"),
    ("1 234 567", "1 234 567"),
    ("3.14", "3.14"),
    ("a, b, 1, 2", "a, b, 1, 2"),
    ("1985", "1985"),
]


def test_other_numbers_where_the_comma_is_the_decimal_mark_are_unchanged(prepare_with_packs):
    prepare = _hu_with_decimal_comma(prepare_with_packs)
    got = {text: prepare("hu", text) for text, _ in NOT_A_DECIMAL}
    assert got == dict(NOT_A_DECIMAL)


@pytest.mark.parametrize("text, expected", [(t, e) for t, e in NOT_A_DECIMAL if "," in t and t != e] + [
    ("1,500 Ft", "1 500 Ft"), ("1,213", "1 213"), ("10,000", "10 000"), ("100,000 Ft", "100 000 Ft"),
    ("44,100 Hz", "44 100 Hz"), ("250,000", "250 000"), ("$2,499", "$2 499"), ("0,375 liter", "0,375 liter"),
    ("3,14", "3,14"),
])
def test_hungarian_as_shipped_reads_a_comma_before_three_digits_as_thousands(prepare, text, expected):
    """The maintainer's decision: "10,000" is ten thousand in Hungarian, as it has been."""
    assert prepare("hu", text) == expected


def _packs_that_set(key):
    return sorted(p.stem for p in (REPO / "packs" / "lang").glob("*.yaml")
                  if any(line.startswith(f"  {key}:") for line in p.read_text(encoding="utf-8").splitlines()))


def test_no_shipped_pack_sets_a_decimal_separator():
    """Every language keeps its reading until its speakers say otherwise."""
    assert _packs_that_set("decimalSeparator") == []


def _set_in_lang_yaml(packs_root, lang, line):
    """Add a line to the settings of a copy of a pack."""
    path = packs_root / "packs" / "lang" / f"{lang}.yaml"
    text = path.read_text(encoding="utf-8")
    assert text.count("\nsettings:\n") == 1
    path.write_text(text.replace("\nsettings:\n", f"\nsettings:\n  {line}\n", 1), encoding="utf-8", newline="")


@pytest.mark.parametrize("lang", ["de", "fr", "pt-br"])
def test_any_pack_can_say_its_decimal_mark_is_the_comma(prepare_with_packs, lang):
    """The setting is the pack's, not a list of languages in the code."""
    base = {"pt-br": "pt"}.get(lang, lang)
    prepare = prepare_with_packs(lambda root: _set_in_lang_yaml(root, base, 'decimalSeparator: ","'))
    assert prepare(lang, "1,500 Euro") == "1,500 Euro"
    assert prepare(lang, "65,543") == "65,543"
    assert prepare(lang, "1,234,567") == "1 234 567"
    assert prepare(lang, "0,125") == "0,125"
    assert prepare(lang, "3,14") == "3,14"


@pytest.mark.parametrize("lang", ENGLISH)
def test_a_decimal_comma_in_a_pack_that_strips_grouping_commas(prepare_with_packs, lang):
    """English strips a grouping comma and splits years; with the comma as its
    decimal mark it would keep one comma, and still strip several."""
    prepare = prepare_with_packs(lambda root: _set_in_lang_yaml(root, "en", 'decimalSeparator: ","'))
    assert prepare(lang, "1,500 people") == "1,500 people"
    assert prepare(lang, "1,234,567 items") == "1234567 items"
    assert prepare(lang, "in 1985") == "in 19 85"


@pytest.mark.parametrize("value", ['"."', '""', '";"', '", "'])
def test_only_the_comma_changes_how_commas_are_read(prepare_with_packs, value):
    """A dot (or anything else) as the decimal mark leaves the comma a grouping comma."""
    prepare = prepare_with_packs(lambda root: _set_in_lang_yaml(root, "hu", f"decimalSeparator: {value}"))
    assert prepare("hu", "1,500 Ft") == "1 500 Ft"
    assert prepare("hu", "3,14") == "3,14"

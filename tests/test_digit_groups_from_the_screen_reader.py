"""Digits a screen reader has already grouped reach the phonemizer as it grouped them.

A screen reader add-on can rewrite numbers before the synthesizer sees them.
NVDA's Number Patterns add-on does: with the pattern 4-3-4 the phone number
"09123456789" arrives as "0912 345 6789", or as "0912, 345, 6789" when its
"pause between groups" is on.  How text is read is the screen reader's
decision, so text preparation must not regroup those digits.

One setting does, on purpose: year splitting (on in the English pack) reads
any four-digit number as two pairs, so a four-digit group becomes "67 89".
That stays as it is for 3.10; Tuning.md tells people to turn it off if it
gets in the way.  These tests pin both halves: with year splitting off, in
English and in every other language, the groups pass through untouched; with
it on, only a four-digit group changes, and only in the documented way.
"""
from __future__ import annotations

import pathlib
import re
import shutil

import pytest

REPO = pathlib.Path(__file__).resolve().parent.parent

# As the add-on writes them: patterns 4-3-4, 4-2-2-3, 3-3-3, fixed groups of
# three, with and without its pause, and inside a sentence.
GROUPED = [
    "0912 345 6789",
    "0912, 345, 6789",
    "0912 34 56 789",
    "123 456 789",
    "123, 456, 789",
    "12 345",
    "007 123",
    "Call 0912 345 6789 now.",
]

# Every language pack but the English ones: year splitting is on only in
# en.yaml, which the English varieties inherit.
OTHER_LANGUAGES = sorted(p.stem for p in (REPO / "packs" / "lang").glob("*.yaml")
                         if p.stem != "default" and not p.stem.startswith("en"))


def _handle(frontend, root, lang):
    h = frontend.create_handle(str(root))
    frontend.set_language(h, lang)
    return h


@pytest.mark.parametrize("lang", OTHER_LANGUAGES)
@pytest.mark.parametrize("text", GROUPED)
def test_groups_pass_through_in_languages_without_year_splitting(frontend, pack_dir, lang, text):
    h = _handle(frontend, pack_dir, lang)
    try:
        assert frontend.prepare_text(h, text) == text
    finally:
        frontend.destroy_handle(h)


@pytest.fixture(scope="module")
def packs_without_year_splitting(tmp_path_factory):
    """The repository's packs with year splitting turned off in English, as the NVDA checkbox does."""
    root = tmp_path_factory.mktemp("no_year_splitting")
    shutil.copytree(REPO / "packs", root / "packs")
    en = root / "packs" / "lang" / "en.yaml"
    data = en.read_bytes()
    assert data.count(b"yearSplittingEnabled: true") == 1
    en.write_bytes(data.replace(b"yearSplittingEnabled: true", b"yearSplittingEnabled: false"))
    return root


@pytest.mark.parametrize("lang", ["en-us", "en-gb", "en-ca", "en-au"])
@pytest.mark.parametrize("text", GROUPED)
def test_groups_pass_through_in_english_with_year_splitting_off(frontend, packs_without_year_splitting, lang, text):
    h = _handle(frontend, packs_without_year_splitting, lang)
    try:
        assert frontend.prepare_text(h, text) == text
    finally:
        frontend.destroy_handle(h)


@pytest.mark.parametrize("lang", ["en-us", "en-gb"])
@pytest.mark.parametrize("text, expected", [
    ("0912 345 6789", "0912 345 67 89"),          # the four-digit group is read as pairs; "0912" has a leading zero and is left
    ("0912, 345, 6789", "0912, 345, 67 89"),
    ("0912 34 56 789", "0912 34 56 789"),         # no four-digit group without a leading zero: nothing to split
    ("123 456 789", "123 456 789"),
    ("12 345", "12 345"),
    ("007 123", "007 123"),
])
def test_with_year_splitting_on_only_a_four_digit_group_changes(frontend, pack_dir, lang, text, expected):
    h = _handle(frontend, pack_dir, lang)
    try:
        out = frontend.prepare_text(h, text)
        assert out == expected
        # Whatever it does to a group, it never joins two groups into one number.
        assert re.sub(r"\D", "", out) == re.sub(r"\D", "", text)
        assert len(out.split()) >= len(text.split())
    finally:
        frontend.destroy_handle(h)

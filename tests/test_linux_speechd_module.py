"""The Speech Dispatcher module takes each kind of message as the server sends it.

Two faults in sd_tgsb's handling of what the server sends, found by checking
this repository against the audit of the maintainer's SSI-263 project (whose
module had the first of them).

A CHAR or a KEY is not SSML: speech-dispatcher sends the character or the
key name as it is (0.11.1, src/server/output.c).  The module sent SPEAK,
CHAR and KEY alike through its tag remover, so a "<" in one was taken for
the start of a tag and everything up to the next ">" was dropped.

A SPEAK is SSML, and in it the server writes "<", ">" and "&" as "&lt;",
"&gt;" and "&amp;" (src/server/index_marking.c, insert_index_marks).  The
module removed the tags and left the entities, so every "&" in running text
was read as "and amp" and every "<" as "and l t".

What is compared here is the number of samples the module sends back, which
is exact for the same text (the audio itself is not: the voice has breath
noise in it).  The same text sent as CHAR, which is raw, and sent the way
SPEAK carries it must come out the same length.

Not covered, because it is not so: a "<" or ">" alone is still silent, like
"." or "-" alone.  The phonemizer passes over punctuation in running text
and nothing in the module asks it for the character's name.  That is a gap
of its own, older than these faults and not caused by them.

Linux only: the module is built by `make -f Makefile.linux` into build/, and
uses the system's espeak-ng.  tests/cpp/test_sd_ssml.cpp holds the rules of
the conversion itself, on every platform.
"""
from __future__ import annotations

import pathlib
import sys

import pytest

from _sd_module import SdModule

REPO = pathlib.Path(__file__).resolve().parents[1]
MODULE = REPO / "build" / "sd_tgsb"

pytestmark = pytest.mark.skipif(
    not sys.platform.startswith("linux") or not MODULE.is_file(),
    reason="needs Linux and build/sd_tgsb (make -f Makefile.linux)")


@pytest.fixture
def sd(tmp_path):
    with SdModule(MODULE, REPO, tmp_path) as module:
        yield module


@pytest.mark.parametrize("command", ["CHAR", "KEY"])
def test_text_between_angle_brackets_in_a_char_or_key_is_not_dropped(sd, command):
    """"x <yes> z" lost "<yes>": the tag remover took it for markup."""
    whole = sd.message(command, "x <yes> z")
    without = sd.message(command, "x z")
    assert whole.ended == "END"
    assert without.samples > 2000
    assert whole.samples > without.samples * 1.3, "the word between the brackets was not spoken"
    # And it is the text SPEAK says for the same characters.
    as_speak = sd.message("SPEAK", "<speak>x &lt;yes&gt; z</speak>")
    assert whole.samples == as_speak.samples


@pytest.mark.parametrize("command", ["CHAR", "KEY"])
def test_what_follows_a_less_than_sign_in_a_char_or_key_is_not_dropped(sd, command):
    """With no ">" after it, everything after the "<" was gone."""
    whole = sd.message(command, "a < b")
    without = sd.message(command, "a")
    assert without.samples > 2000
    assert whole.samples > without.samples * 1.3


def test_speak_says_an_ampersand_entity_as_the_ampersand(sd):
    """CHAR is raw, so it is the measure: the character through SPEAK,
    written the way the server writes it, is the same length."""
    as_char = sd.message("CHAR", "&")
    as_speak = sd.message("SPEAK", "<speak>&amp;</speak>")
    assert as_char.samples > 2000
    assert as_char.loud()
    assert as_speak.samples == as_char.samples


@pytest.mark.parametrize("named,numeric", [
    ("Tom &amp; Jerry", "Tom &#38; Jerry"),
    ("a &lt; b", "a &#60; b"),
    ("it&apos;s &quot;so&quot;", "it's \"so\""),
], ids=["ampersand", "less-than", "apostrophe and quotes"])
def test_an_entity_in_a_sentence_is_one_character_however_it_is_written(sd, named, numeric):
    one = sd.message("SPEAK", f"<speak>{named}</speak>")
    other = sd.message("SPEAK", f"<speak>{numeric}</speak>")
    assert one.samples > 4000
    assert one.samples == other.samples


def test_an_ampersand_between_two_words_is_not_read_as_its_entity_name(sd):
    """"Tom & Jerry" is about as long as "Tom and Jerry".  Read with the
    entity's name in it ("Tom and amp Jerry") it was 40% longer."""
    spoken = sd.message("SPEAK", "<speak>Tom &amp; Jerry</speak>")
    words = sd.message("SPEAK", "<speak>Tom and Jerry</speak>")
    assert words.samples > 4000
    assert spoken.samples < words.samples * 1.15
    assert spoken.samples > words.samples * 0.85


def test_the_servers_own_marks_and_wrapper_leave_the_text_as_it_was(sd):
    """The server puts a <mark/> after each sentence; it is not text."""
    plain = sd.message("SPEAK", "<speak>One. Two.</speak>")
    marked = sd.message("SPEAK", '<speak>One.<mark name="__spd_0"/> Two.<mark name="__spd_1"/></speak>')
    assert plain.samples > 4000
    assert marked.samples == plain.samples


def test_a_letter_and_a_key_name_are_spoken_as_before(sd):
    assert sd.message("CHAR", "a").samples > 2000
    assert sd.message("KEY", "space").samples > 2000


def test_text_in_angle_brackets_typed_by_the_user_is_read(sd):
    """The user's text "<yes>", which the server sends as "&lt;yes&gt;": it
    is text, and must not vanish as a tag once the entities are decoded."""
    said = sd.message("SPEAK", "<speak>&lt;yes&gt;</speak>")
    word = sd.message("SPEAK", "<speak>yes</speak>")
    assert word.samples > 2000
    assert said.samples == word.samples

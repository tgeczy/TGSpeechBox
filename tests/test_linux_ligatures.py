"""On Linux, a word typeset with a ligature is the word.

PDFs and e-books write "office" with one character for "ffi" (U+FB03).
Windows and Apple turn it into its letters with the system's NFKC; on Linux
and Android the frontend has no system normalizer, and the phonemizer read
the character out as "letter F B zero three" (foldLigatures in utf8.cpp;
tests/cpp/test_ligatures.cpp holds the function on every platform).

Speech Dispatcher's server normalizes text itself before a module sees it,
so its clients were spared; the command-line tools and anything else that
hands the frontend text directly were not.  This test hands the built
module the text directly, as those do, and compares lengths in samples,
which are exact for the same text.

Linux only: needs build/sd_tgsb (make -f Makefile.linux) and espeak-ng.
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

WORDS = [
    ("office", "oﬃce"),      # ffi
    ("waffle", "waﬄe"),      # ffl
    ("file", "ﬁle"),         # fi
    ("flat", "ﬂat"),         # fl
    ("off", "oﬀ"),           # ff
    ("first", "firﬆ"),       # st
]


@pytest.mark.parametrize("plain,typeset", WORDS, ids=[w for w, _ in WORDS])
def test_a_word_with_a_ligature_is_as_long_as_the_word(tmp_path, plain, typeset):
    with SdModule(MODULE, REPO, tmp_path) as sd:
        want = sd.message("SPEAK", f"<speak>{plain}</speak>")
        got = sd.message("SPEAK", f"<speak>{typeset}</speak>")
    assert want.samples > 2000
    assert got.samples == want.samples

"""Every language pack is a SAPI voice in its own language.

The SAPI engine gives each voice the Windows language code (LCID) of its
language pack as the token's Language attribute; programs list and choose
voices by it, and a screen reader's automatic language switching asks for a
voice by it.  A language Windows does not know by the pack's tag would fall
back to 409, US English (lang_tag_to_lcid_hex in src/sapi/tgsb_runtime.cpp):
the voice would be listed as English, never found when its own language is
asked for, and offered when English is.  In the maintainer's SSI-263 project
seventeen voices were registered that way.  Here none is; this keeps it so
when a pack is added.

Windows only: it asks Windows the question the engine asks.
"""
from __future__ import annotations

import ctypes
import pathlib
import sys

import pytest

REPO = pathlib.Path(__file__).resolve().parents[1]

pytestmark = pytest.mark.skipif(sys.platform != "win32", reason="asks Windows for its language codes")

US_ENGLISH = 0x0409
#: The pack that holds what every language shares; it is not a voice.
NOT_A_VOICE = {"default"}


def _tags():
    return sorted(p.stem for p in (REPO / "packs" / "lang").glob("*.yaml") if p.stem not in NOT_A_VOICE)


def _lcid(tag):
    fn = ctypes.windll.kernel32.LocaleNameToLCID
    fn.argtypes, fn.restype = [ctypes.c_wchar_p, ctypes.c_uint], ctypes.c_uint
    return fn(tag, 0)


def test_there_are_packs_to_ask_about():
    assert len(_tags()) >= 25


@pytest.mark.parametrize("tag", _tags())
def test_windows_knows_the_language(tag):
    lcid = _lcid(tag)
    assert lcid != 0, f"Windows has no language code for {tag!r}: its voices would be registered as US English"
    # 0x1000 is "a name Windows accepts but has no code for".
    assert lcid != 0x1000, f"{tag!r} has no code of its own in Windows"
    # A full code has a region in it: a bare language (0x0E for Hungarian)
    # is not what a program asking for Hungarian (0x040E) sends.
    assert (lcid >> 10) & 0x3F, f"{tag!r} gives the bare language {lcid:#x} with no region"


def test_only_english_is_english():
    english = [tag for tag in _tags() if _lcid(tag) == US_ENGLISH]
    assert english and all(tag in ("en", "en-us") for tag in english), english


def test_dialects_of_a_language_have_codes_of_their_own():
    """A regional pack must not be registered under its neighbour's code:
    Portugal is not Brazil, Mexico is not Spain."""
    for a, b in (("pt-pt", "pt-br"), ("es-mx", "es-es"), ("en-gb", "en-us"), ("en-au", "en-ca")):
        assert _lcid(a) != _lcid(b), f"{a} and {b} share a language code"

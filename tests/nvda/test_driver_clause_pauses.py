"""The add-on pauses where every TGSpeechBox platform pauses (#133).

29-Bloo asked for pauses around parentheses and at dashes, and before
Spanish ¿ and ¡; people on Mastodon asked for the parentheses too.  The
driver now splits clauses with the frontend's splitter, the one SAPI,
Android, iOS and Linux use, so the pause after each clause (25 ms after a
comma-like break, 35 ms after a sentence mark, with the default short
pauses) lands in the same places everywhere.

A pause here is the silence frame the driver queues after a clause, as the
DSP receives it.
"""
from __future__ import annotations

import pytest

CASES = [
    ("en-us", "Hello, world. How are you?", [25.0, 35.0, 35.0]),
    ("en-us", "The file (about 2 MB) is ready.", [25.0, 25.0, 35.0]),
    ("en-us", "wait — what now", [25.0]),
    ("en-us", "wait - what now", [25.0]),
    ("en-us", "Note: this; that", [35.0, 35.0]),
    ("en-us", "Visit example.com today", []),
    ("es", "dijo ¡basta! ya", [25.0, 35.0]),
    ("es", "Si vienes ¿me avisas?", [25.0, 35.0]),
]


def _pauses(frames):
    # The last silence frame is the utterance's tail drain (#107), not a pause.
    return [minD for frame, frame_ex, minD, _ in frames[:-1] if frame is None and minD in (25.0, 35.0, 50.0, 60.0)]


@pytest.mark.parametrize("lang,text,want", CASES, ids=[c[1] for c in CASES])
def test_pauses_land_where_every_platform_puts_them(harness, record_frames, lang, text, want):
    harness.driver.language = lang
    harness.driver.cancel()
    frames = record_frames(harness.driver)
    harness.speak([text])
    assert _pauses(frames) == want


def test_long_pauses_setting(harness, record_frames):
    harness.driver._pauseMode = "long"
    harness.driver.cancel()
    frames = record_frames(harness.driver)
    harness.speak(["The file (about 2 MB) is ready."])
    assert _pauses(frames) == [50.0, 50.0, 60.0]

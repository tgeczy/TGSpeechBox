"""The add-on pauses where every TGSpeechBox platform pauses (#133).

29-Bloo asked for pauses around parentheses and at dashes, and before
Spanish ¿ and ¡; people on Mastodon asked for the parentheses too.  The
driver now splits clauses with the frontend's splitter, the one SAPI,
Android, iOS and Linux use, so the pause after each clause (25 ms after a
comma-like break, 35 ms after a sentence mark, with the default short
pauses) lands in the same places everywhere.

A pause here is the silence frame the driver queues after a clause, as the
DSP receives it.

Those lengths are for the default rate.  The pause follows the rate
(scalePauseToRate, on in default.yaml): its length times rate^-0.5, kept
between 0.5 and 1.6 of it, from the frontend's nvspFrontend_scalePause, the
function every host uses.  Above 2x the driver synthesizes at 2x and gives
the rest to the DSP's time stretch, which shortens a queued silence too, so
the frame it queues is longer by the stretch and the pause heard is the
scaled one, not that pause shrunk a second time.
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


def _pauses(frames, lengths=(25.0, 35.0, 50.0, 60.0)):
    # The last silence frame is the utterance's tail drain (#107), not a pause.
    return [minD for frame, frame_ex, minD, _ in frames[:-1] if frame is None and minD in lengths]


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


# ---------------------------------------------------------------------------
# The pause follows the rate
# ---------------------------------------------------------------------------

SPEEDS = [0.5, 1.0, 1.6, 2.0, 3.0]
TEXT = "Hello, world. How are you?"


def _stretch(rate):
    """The DSP time stretch the driver sets: what is left above 2x."""
    return max(rate / 2.0, 1.0)


def _heard(base, rate):
    return base * min(max(rate ** -0.5, 0.5), 1.6)


def _queued(base, rate):
    """The silence frame for a pause of `base` ms at `rate`, as recorded."""
    return round(_heard(base, rate) * _stretch(rate), 3)


def _speak_at(harness, record_frames, rate, mode="short", boost=False):
    harness.driver.language = "en-us"
    harness.driver._curRate = rate
    harness.driver.rateBoost = boost
    harness.driver._pauseMode = mode
    harness.driver.cancel()
    frames = record_frames(harness.driver)
    harness.speak([TEXT])
    return frames


@pytest.mark.parametrize("speed", SPEEDS)
@pytest.mark.parametrize("mode,comma,sentence", [("short", 25.0, 35.0), ("long", 50.0, 60.0)],
                         ids=["short", "long"])
def test_the_pause_queued_after_a_clause_follows_the_rate(harness, record_frames, speed, mode, comma, sentence):
    frames = _speak_at(harness, record_frames, speed, mode)
    want = [_queued(comma, speed), _queued(sentence, speed), _queued(sentence, speed)]
    assert _pauses(frames, set(want)) == want
    if speed == 1.0:
        assert want == [comma, sentence, sentence]  # nothing changes at the default rate


def test_at_three_times_the_rate_the_pause_is_not_shrunk_twice(harness, record_frames):
    # 3x is 2x synthesis and a 1.5x stretch.  A comma pause is heard for
    # 25 * 3^-0.5 = 14.4 ms, so the frame queued on the stretched player is
    # 21.7 ms.  (Before: 25 ms queued, 16.7 heard; scaled but not lengthened
    # for the stretch it would be 9.6.)
    frames = _speak_at(harness, record_frames, 3.0)
    assert _pauses(frames, {21.651, 30.311}) == [21.651, 30.311, 30.311]
    assert round(21.651 / 1.5, 2) == round(25.0 * 3.0 ** -0.5, 2) == 14.43


def test_rate_boost_counts_as_rate(harness, record_frames):
    # Rate boost doubles the rate: 1.5 boosted is heard as 3.0.
    frames = _speak_at(harness, record_frames, 1.5, boost=True)
    want = [_queued(25.0, 3.0), _queued(35.0, 3.0), _queued(35.0, 3.0)]
    assert _pauses(frames, set(want)) == want


def test_the_fastest_rate_keeps_half_the_pause(harness, record_frames):
    # Rate 100 with rate boost is 8x: the pause stops at half its length.
    frames = _speak_at(harness, record_frames, 4.0, boost=True)
    assert _pauses(frames, {50.0, 70.0}) == [50.0, 70.0, 70.0]  # 12.5 and 17.5 ms through a 4x stretch


@pytest.mark.parametrize("speed", SPEEDS)
def test_a_language_with_the_setting_off_pauses_as_before(harness, record_frames, speed):
    from synthDrivers.tgSpeechBox import langPackYaml
    harness.driver.language = "en-us"
    langPackYaml.upsertSetting(harness.driver._packsDir, "en-us", "scalePauseToRate", False)
    harness.driver.reloadLanguagePack()
    frames = _speak_at(harness, record_frames, speed)
    assert _pauses(frames) == [25.0, 35.0, 35.0]
